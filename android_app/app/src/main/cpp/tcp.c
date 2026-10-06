#include "tun2http.h"
#include "tls.h"
#include "http.h"
#include "auth.h"

extern struct ng_session *ng_session;

static int should_redirect(const struct arguments *args, int port) {
    if (port == args->proxyPort) return 0;
    if (port == 53 && !args->fwd53) return 0;
    return 1;
}

static int is_http_payload(const uint8_t *data, size_t len) {
    if (len < 10) return 0;

    if (data[0] < 'A' || data[0] > 'Z') return 0;
    if (len >= strlen("CONNECT ") && memcmp(data, "CONNECT ", 8) == 0) return 0;

    for (size_t i = 3; i < len - 8; i++) {
        if (data[i] == '\r' || data[i] == '\n') break;
        if (memcmp(data + i, " HTTP/1.", 8) == 0) {
            return 1;
        }
    }
    return 0;
}

// Returns 4 or 6 when host is an IP literal (addr filled in), otherwise 0
static int parse_ip_literal(const char *host, uint8_t *addr) {
    if (inet_pton(AF_INET, host, addr) == 1) return 4;
    if (inet_pton(AF_INET6, host, addr) == 1) return 6;
    return 0;
}

// SOCKS4 / SOCKS4a CONNECT request, returns the length or -1 (IPv6 is not supported)
static int build_socks4_request(const struct tcp_session *t, uint8_t *buf, size_t size) {
    uint8_t addr[16];
    int type = parse_ip_literal(t->hostname, addr);
    size_t hostlen = strlen(t->hostname);
    if (type == 6 || (type == 0 && hostlen == 0) || 9 + hostlen + 1 > size) return -1;

    int len = 0;
    buf[len++] = 0x04; // VN
    buf[len++] = 0x01; // CD: CONNECT
    memcpy(buf + len, &t->dest, 2); // DSTPORT
    len += 2;
    if (type == 4) {
        memcpy(buf + len, addr, 4); // DSTIP
        len += 4;
        buf[len++] = 0x00; // USERID (empty)
    } else {
        // SOCKS4a: DSTIP 0.0.0.x (x != 0), hostname follows USERID
        buf[len++] = 0x00;
        buf[len++] = 0x00;
        buf[len++] = 0x00;
        buf[len++] = 0x01;
        buf[len++] = 0x00; // USERID (empty)
        memcpy(buf + len, t->hostname, hostlen + 1);
        len += (int) hostlen + 1;
    }
    return len;
}

// SOCKS5 CONNECT request, returns the length or -1
static int build_socks5_request(const struct tcp_session *t, uint8_t *buf, size_t size) {
    uint8_t addr[16];
    int type = parse_ip_literal(t->hostname, addr);
    size_t hostlen = strlen(t->hostname);
    if ((type == 0 && (hostlen == 0 || hostlen > 255)) || 7 + 16 + hostlen > size) return -1;

    int len = 0;
    buf[len++] = 0x05; // VER
    buf[len++] = 0x01; // CMD: CONNECT
    buf[len++] = 0x00; // RSV
    if (type == 4) {
        buf[len++] = 0x01; // ATYP: IPv4
        memcpy(buf + len, addr, 4);
        len += 4;
    } else if (type == 6) {
        buf[len++] = 0x04; // ATYP: IPv6
        memcpy(buf + len, addr, 16);
        len += 16;
    } else {
        buf[len++] = 0x03; // ATYP: DOMAINNAME
        buf[len++] = (uint8_t) hostlen;
        memcpy(buf + len, t->hostname, hostlen);
        len += (int) hostlen;
    }
    memcpy(buf + len, &t->dest, 2); // DST.PORT
    len += 2;
    return len;
}

// Proxy reply parsers: length of a complete successful reply, 0 if incomplete, -1 on failure
static int parse_socks4_reply(const uint8_t *buf, size_t len) {
    // VN(0x00) CD(0x5A = granted) DSTPORT(2) DSTIP(4)
    if (len >= 1 && buf[0] != 0x00) return -1;
    if (len >= 2 && buf[1] != 0x5A) return -1;
    return len >= 8 ? 8 : 0;
}

static int parse_socks5_reply(const uint8_t *buf, size_t len) {
    // VER REP RSV ATYP BND.ADDR BND.PORT
    if (len >= 1 && buf[0] != 0x05) return -1;
    if (len >= 2 && buf[1] != 0x00) return -1;
    if (len < 5) return 0;
    size_t need;
    switch (buf[3]) {
        case 0x01: need = 4 + 4 + 2; break;
        case 0x04: need = 4 + 16 + 2; break;
        case 0x03: need = 4 + 1 + buf[4] + 2; break;
        default: return -1;
    }
    return len >= need ? (int) need : 0;
}

// Remove an already peeked proxy reply from the socket
static int consume_reply(int sock, int len) {
    uint8_t tmp[512];
    while (len > 0) {
        ssize_t n = recv(sock, tmp, (size_t) len < sizeof(tmp) ? (size_t) len : sizeof(tmp), 0);
        if (n <= 0) return 0;
        len -= (int) n;
    }
    return 1;
}

// Length of the HTTP reply header including the blank line, 0 if incomplete
static int http_header_length(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i + 4 <= len; i++) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) return (int) (i + 4);
    }
    return 0;
}

// Status code of a "HTTP/1.x NNN ..." reply, -1 if malformed
static int http_status_code(const uint8_t *buf, size_t len) {
    if (len < 12 || memcmp(buf, "HTTP/1.", 7) != 0 || buf[8] != ' ') return -1;
    if (!isdigit(buf[9]) || !isdigit(buf[10]) || !isdigit(buf[11])) return -1;
    return (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
}

static int send_http_connect(const struct tcp_session *t, int sock) {
    char target[600];
    char authorization[2048];
    char request[3300];
    // IPv6 literal must be enclosed in brackets
    int ipv6 = (strchr(t->hostname, ':') != NULL);
    snprintf(target, sizeof(target), "%s%s%s:%d",
             ipv6 ? "[" : "", t->hostname, ipv6 ? "]" : "", ntohs(t->dest));
    if (auth_build_header("CONNECT", target, authorization, sizeof(authorization)) <= 0) {
        authorization[0] = '\0';
    }
    int len = snprintf(request, sizeof(request), "CONNECT %s HTTP/1.0\r\nHost: %s\r\n%s\r\n",
                       target, target, authorization);
    if (len <= 0 || (size_t) len >= sizeof(request)) return -1;
    return send(sock, request, (size_t) len, MSG_NOSIGNAL) == len ? 0 : -1;
}

static int send_socks5_connect(const struct tcp_session *t, int sock) {
    uint8_t request[300];
    int reqlen = build_socks5_request(t, request, sizeof(request));
    if (reqlen < 0) return -1;
    return send(sock, request, (size_t) reqlen, MSG_NOSIGNAL) == reqlen ? 0 : -1;
}

// The proxy answered 407: learn the challenge and connect again to send the credentials
static int retry_proxy_auth(const struct arguments *args, struct ng_session *s,
                            const uint8_t *reply, size_t len, int epoll_fd) {
    if (!auth_enabled()) return 0;

    int stale = 0;
    if (!auth_parse_challenge((const char *) reply, len, &stale)) return 0;
    // Retry once, more often only when the digest nonce was expired
    if ((s->tcp.auth_retry > 0 && !stale) || s->tcp.auth_retry >= HTTP_AUTH_MAX_RETRY) return 0;

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, s->socket, NULL);
    close(s->socket);

    struct allowed redirect;
    strcpy(redirect.raddr, args->proxyIp);
    redirect.rport = args->proxyPort;
    s->socket = open_tcp_socket(args, &s->tcp, &redirect);
    if (s->socket < 0) return 0;

    s->tcp.auth_retry++;
    s->tcp.connect_sent = TCP_CONNECT_RETRY;

    // Wait until connected
    memset(&s->ev, 0, sizeof(struct epoll_event));
    s->ev.events = EPOLLOUT | EPOLLERR;
    s->ev.data.ptr = s;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, s->socket, &s->ev)) {
        log_android(ANDROID_LOG_ERROR, "epoll add tcp error %d: %s", errno, strerror(errno));
        return 0;
    }
    return 1;
}

void clear_tcp_data(struct tcp_session *cur) {
    struct segment *s = cur->forward;
    while (s != NULL) {
        struct segment *p = s;
        s = s->next;
        free(p->data);
        free(p);
    }
}

int get_tcp_timeout(const struct tcp_session *t, int sessions, int maxsessions) {
    int timeout;
    if (t->state == TCP_LISTEN || t->state == TCP_SYN_RECV) {
        timeout = TCP_INIT_TIMEOUT;
    } else if (t->state == TCP_ESTABLISHED) {
        timeout = TCP_IDLE_TIMEOUT;
    } else {
        timeout = TCP_CLOSE_TIMEOUT;
    }

    int scale = 100 - sessions * 100 / maxsessions;
    timeout = timeout * scale / 100;

    return timeout;
}

int check_tcp_session(const struct arguments *args, struct ng_session *s,
                      int sessions, int maxsessions) {
    time_t now = time(NULL);

    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];
    if (s->tcp.version == 4) {
        inet_ntop(AF_INET, &s->tcp.saddr.ip4, source, sizeof(source));
        inet_ntop(AF_INET, &s->tcp.daddr.ip4, dest, sizeof(dest));
    } else {
        inet_ntop(AF_INET6, &s->tcp.saddr.ip6, source, sizeof(source));
        inet_ntop(AF_INET6, &s->tcp.daddr.ip6, dest, sizeof(dest));
    }

    char session[250];
    sprintf(session, "TCP socket from %s/%u to %s/%u %s socket %d",
            source, ntohs(s->tcp.source), dest, ntohs(s->tcp.dest),
            strstate(s->tcp.state), s->socket);

    int timeout = get_tcp_timeout(&s->tcp, sessions, maxsessions);

    // Check session timeout
    if (s->tcp.state != TCP_CLOSING && s->tcp.state != TCP_CLOSE &&
        s->tcp.time + timeout < now) {
        if (s->tcp.state == TCP_LISTEN) {
            s->tcp.state = TCP_CLOSING;
        } else {
            write_rst(args, &s->tcp);
        }
    }

    // Check closing sessions
    if (s->tcp.state == TCP_CLOSING) {
        // eof closes socket
        if (s->socket >= 0) {
            if (close(s->socket)) {
                log_android(ANDROID_LOG_ERROR, "%s close error %d: %s",
                            session, errno, strerror(errno));
            } else {
                log_android(ANDROID_LOG_WARN, "%s close", session);
            }
            s->socket = -1;
        }

        s->tcp.time = time(NULL);
        s->tcp.state = TCP_CLOSE;
    }

    if ((s->tcp.state == TCP_CLOSING || s->tcp.state == TCP_CLOSE) &&
        (s->tcp.sent || s->tcp.received)) {
        s->tcp.sent = 0;
        s->tcp.received = 0;
    }

    // Cleanup lingering sessions
    if (s->tcp.state == TCP_CLOSE && s->tcp.time + TCP_KEEP_TIMEOUT < now)
        return 1;

    return 0;
}

int monitor_tcp_session(const struct arguments *args, struct ng_session *s, int epoll_fd) {
    int recheck = 0;
    unsigned int events = EPOLLERR;

    if (s->tcp.state == TCP_LISTEN) {

        // Wait for the proxy reply, otherwise check for connected = writable
        if (s->tcp.connect_sent == TCP_CONNECT_SENT) {
            events = events | EPOLLIN;
        } else {
            events = events | EPOLLOUT;
        }
    } else if (s->tcp.state == TCP_ESTABLISHED || s->tcp.state == TCP_CLOSE_WAIT) {

        // Check for incoming data
        if (get_send_window(&s->tcp) > 0) {
            events = events | EPOLLIN;
        } else {
            recheck = 1;

            long long ms = get_ms();
            if (ms - s->tcp.last_keep_alive > EPOLL_MIN_CHECK) {
                s->tcp.last_keep_alive = ms;
                log_android(ANDROID_LOG_WARN, "Sending keep alive to update send window");
                s->tcp.remote_seq--;
                write_ack(args, &s->tcp);
                s->tcp.remote_seq++;
            }
        }

        // Check for outgoing data
        if (s->tcp.forward != NULL) {
            uint32_t buffer_size = (uint32_t) get_receive_buffer(s);
            if (s->tcp.forward->seq + s->tcp.forward->sent == s->tcp.remote_seq &&
                s->tcp.forward->len - s->tcp.forward->sent < buffer_size) {
                events = events | EPOLLOUT;
            } else {
                recheck = 1;
            }
        }
    }

    if (events != s->ev.events) {
        s->ev.events = events;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, s->socket, &s->ev)) {
            s->tcp.state = TCP_CLOSING;
            log_android(ANDROID_LOG_ERROR, "epoll mod tcp error %d: %s", errno, strerror(errno));
        } else {
            log_android(ANDROID_LOG_DEBUG, "epoll mod tcp socket %d in %d out %d",
                        s->socket, (events & EPOLLIN) != 0, (events & EPOLLOUT) != 0);
        }
    }

    return recheck;
}

uint32_t get_send_window(const struct tcp_session *cur) {
    uint32_t behind = (compare_u32(cur->acked, cur->local_seq) <= 0
                       ? cur->local_seq - cur->acked : cur->acked);
    uint32_t window = (behind < cur->send_window ? cur->send_window - behind : 0);
    return window;
}

int get_receive_buffer(const struct ng_session *cur) {
    if (cur->socket < 0) {
        return 0;
    }

    // Get send buffer size
    // /proc/sys/net/core/wmem_default
    int sendbuf = 0;
    int sendbufsize = sizeof(sendbuf);
    if (getsockopt(cur->socket, SOL_SOCKET, SO_SNDBUF, &sendbuf, &sendbufsize) < 0) {
        log_android(ANDROID_LOG_WARN, "getsockopt SO_RCVBUF %d: %s", errno, strerror(errno));
    }

    if (sendbuf == 0) {
        sendbuf = 16384; // Safe default
    }

    // Get unsent data size
    int unsent = 0;
    if (ioctl(cur->socket, SIOCOUTQ, &unsent)) {
        log_android(ANDROID_LOG_WARN, "ioctl SIOCOUTQ %d: %s", errno, strerror(errno));
    }

    return (unsent < sendbuf / 2 ? sendbuf / 2 - unsent : 0);
}

uint32_t get_receive_window(const struct ng_session *cur) {
    // Get data to forward size
    uint32_t toforward = 0;
    struct segment *q = cur->tcp.forward;
    while (q != NULL) {
        toforward += (q->len - q->sent);
        q = q->next;
    }

    uint32_t window = (uint32_t) get_receive_buffer(cur);

    uint32_t max = ((uint32_t) 0xFFFF) << cur->tcp.recv_scale;
    if (window > max) {
        window = max;
    }

    window = (toforward < window ? window - toforward : 0);
    if ((window >> cur->tcp.recv_scale) == 0) {
        window = 0;
    }

    return window;
}

void check_tcp_socket(const struct arguments *args,
                      const struct epoll_event *ev,
                      const int epoll_fd) {
    struct ng_session *s = (struct ng_session *) ev->data.ptr;

    int oldstate = s->tcp.state;
    uint32_t oldlocal = s->tcp.local_seq;
    uint32_t oldremote = s->tcp.remote_seq;

    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];
    if (s->tcp.version == 4) {
        inet_ntop(AF_INET, &s->tcp.saddr.ip4, source, sizeof(source));
        inet_ntop(AF_INET, &s->tcp.daddr.ip4, dest, sizeof(dest));
    } else {
        inet_ntop(AF_INET6, &s->tcp.saddr.ip6, source, sizeof(source));
        inet_ntop(AF_INET6, &s->tcp.daddr.ip6, dest, sizeof(dest));
    }
    char session[250];
    sprintf(session, "TCP socket from %s/%u to %s/%u %s loc %u rem %u",
            source, ntohs(s->tcp.source), dest, ntohs(s->tcp.dest),
            strstate(s->tcp.state),
            s->tcp.local_seq - s->tcp.local_start,
            s->tcp.remote_seq - s->tcp.remote_start);

    // Check socket error
    if (ev->events & EPOLLERR) {
        s->tcp.time = time(NULL);

        int serr = 0;
        socklen_t optlen = sizeof(int);
        int err = getsockopt(s->socket, SOL_SOCKET, SO_ERROR, &serr, &optlen);
        if (err < 0) {
            log_android(ANDROID_LOG_ERROR, "%s getsockopt error %d: %s",
                        session, errno, strerror(errno));
        } else if (serr) {
            log_android(ANDROID_LOG_ERROR, "%s SO_ERROR %d: %s",
                        session, serr, strerror(serr));
        }

        write_rst(args, &s->tcp);

        // Connection refused
        if (0) {
            if (err >= 0 && (serr == ECONNREFUSED || serr == EHOSTUNREACH)) {
                struct icmp icmp;
                memset(&icmp, 0, sizeof(struct icmp));
                icmp.icmp_type = ICMP_UNREACH;
                if (serr == ECONNREFUSED) {
                    icmp.icmp_code = ICMP_UNREACH_PORT;
                } else {
                    icmp.icmp_code = ICMP_UNREACH_HOST;
                }
                icmp.icmp_cksum = 0;
                icmp.icmp_cksum = ~calc_checksum(0, &icmp, 4);

                struct icmp_session sicmp;
                memset(&sicmp, 0, sizeof(struct icmp_session));
                sicmp.version = s->tcp.version;
                if (s->tcp.version == 4) {
                    sicmp.saddr.ip4 = (__be32) s->tcp.saddr.ip4;
                    sicmp.daddr.ip4 = (__be32) s->tcp.daddr.ip4;
                } else {
                    memcpy(&sicmp.saddr.ip6, &s->tcp.saddr.ip6, 16);
                    memcpy(&sicmp.daddr.ip6, &s->tcp.daddr.ip6, 16);
                }
                write_icmp(args, &sicmp, &icmp, 8);
            }
        }
    } else {
        // Assume socket okay
        if (s->tcp.state == TCP_LISTEN) {
            // Check socket connect
            if (ev->events & EPOLLIN) {
                uint8_t buffer[4096];
                // Peek only, so that data sent by the server right after
                // the proxy reply is not consumed together with the reply
                ssize_t bytes = recv(s->socket, buffer, sizeof(buffer) - 1, MSG_PEEK);
                if (bytes <= 0) {
                    log_android(ANDROID_LOG_ERROR, "%s recv error %d: %s",
                                session, errno, strerror(errno));
                    write_rst(args, &s->tcp);
                } else if (args->proxyType == PROXY_TYPE_SOCKS4) {
                    int rlen = parse_socks4_reply(buffer, (size_t) bytes);
                    if (rlen > 0 && consume_reply(s->socket, rlen)) {
                        s->tcp.connect_sent = TCP_CONNECT_ESTABLISHED;
                        s->tcp.state = TCP_SYN_RECV;
                    } else if (rlen != 0) {
                        log_android(ANDROID_LOG_ERROR, "%s SOCKS4 connect failed %d",
                                    session, bytes >= 2 ? buffer[1] : -1);
                        write_rst(args, &s->tcp);
                    }
                } else if (args->proxyType == PROXY_TYPE_SOCKS5) {
                    if (s->tcp.socks5_state == SOCKS5_STATE_GREETING) {
                        if (bytes < 2) {
                            // Wait for the complete reply
                        } else if (buffer[0] == 0x05 && buffer[1] == 0x00 &&
                                   consume_reply(s->socket, 2)) {
                            if (send_socks5_connect(&s->tcp, s->socket) < 0) {
                                log_android(ANDROID_LOG_ERROR,
                                            "%s SOCKS5 connect send error %d: %s",
                                            session, errno, strerror(errno));
                                write_rst(args, &s->tcp);
                            } else {
                                s->tcp.socks5_state = SOCKS5_STATE_CONNECT;
                            }
                        } else if (buffer[0] == 0x05 && buffer[1] == 0x02 && auth_enabled() &&
                                   consume_reply(s->socket, 2)) {
                            // RFC 1929 username/password authentication
                            uint8_t request[600];
                            int reqlen = auth_build_socks5_request(request, sizeof(request));
                            if (reqlen < 0 ||
                                send(s->socket, request, (size_t) reqlen, MSG_NOSIGNAL) != reqlen) {
                                log_android(ANDROID_LOG_ERROR,
                                            "%s SOCKS5 auth send error %d: %s",
                                            session, errno, strerror(errno));
                                write_rst(args, &s->tcp);
                            } else {
                                s->tcp.socks5_state = SOCKS5_STATE_AUTH;
                            }
                        } else {
                            log_android(ANDROID_LOG_ERROR, "%s SOCKS5 greeting failed method %d",
                                        session, buffer[1]);
                            write_rst(args, &s->tcp);
                        }
                    } else if (s->tcp.socks5_state == SOCKS5_STATE_AUTH) {
                        if (bytes < 2) {
                            // Wait for the complete reply
                        } else if (buffer[1] == 0x00 && consume_reply(s->socket, 2)) {
                            if (send_socks5_connect(&s->tcp, s->socket) < 0) {
                                log_android(ANDROID_LOG_ERROR,
                                            "%s SOCKS5 connect send error %d: %s",
                                            session, errno, strerror(errno));
                                write_rst(args, &s->tcp);
                            } else {
                                s->tcp.socks5_state = SOCKS5_STATE_CONNECT;
                            }
                        } else {
                            log_android(ANDROID_LOG_ERROR,
                                        "%s SOCKS5 authentication failed %d", session, buffer[1]);
                            write_rst(args, &s->tcp);
                        }
                    } else if (s->tcp.socks5_state == SOCKS5_STATE_CONNECT) {
                        int rlen = parse_socks5_reply(buffer, (size_t) bytes);
                        if (rlen > 0 && consume_reply(s->socket, rlen)) {
                            s->tcp.connect_sent = TCP_CONNECT_ESTABLISHED;
                            s->tcp.state = TCP_SYN_RECV;
                        } else if (rlen != 0) {
                            log_android(ANDROID_LOG_ERROR, "%s SOCKS5 connect failed %d",
                                        session, bytes >= 2 ? buffer[1] : -1);
                            write_rst(args, &s->tcp);
                        }
                    }
                } else {
                    if (s->tcp.connect_sent == TCP_CONNECT_SENT) {
                        buffer[bytes] = '\0';
                        int hlen = http_header_length(buffer, (size_t) bytes);
                        if (hlen == 0 && (size_t) bytes < sizeof(buffer) - 1) {
                            // Wait for the complete reply header
                        } else {
                            if (hlen == 0) hlen = (int) bytes;
                            int status = http_status_code(buffer, (size_t) bytes);
                            if (status == 200 && consume_reply(s->socket, hlen)) {
                                s->tcp.connect_sent = TCP_CONNECT_ESTABLISHED;
                                s->tcp.state = TCP_SYN_RECV;
                            } else if (status == 407 &&
                                       retry_proxy_auth(args, s, buffer, (size_t) hlen, epoll_fd)) {
                                log_android(ANDROID_LOG_WARN, "%s HTTP CONNECT auth retry %d",
                                            session, s->tcp.auth_retry);
                            } else {
                                log_android(ANDROID_LOG_ERROR, "%s HTTP CONNECT failed: %s",
                                            session, buffer);
                                write_rst(args, &s->tcp);
                            }
                        }
                    } else {
                        write_rst(args, &s->tcp);
                    }
                }
            } else if (s->tcp.connect_sent == TCP_CONNECT_RETRY) {
                // Reconnected to the proxy, send CONNECT again with the credentials
                if (send_http_connect(&s->tcp, s->socket) < 0) {
                    log_android(ANDROID_LOG_ERROR, "%s HTTP CONNECT send error %d: %s",
                                session, errno, strerror(errno));
                    write_rst(args, &s->tcp);
                } else {
                    s->tcp.connect_sent = TCP_CONNECT_SENT;
                }
            } else {
                s->tcp.remote_seq++; // remote SYN
                if (write_syn_ack(args, &s->tcp) >= 0) {
                    s->tcp.time = time(NULL);
                    s->tcp.local_seq++; // local SYN
                    s->tcp.state = TCP_SYN_RECV;
                }
            }
        } else {
            // Always forward data
            int fwd = 0;
            if (ev->events & EPOLLOUT) {
                // Forward data
                uint32_t buffer_size = (uint32_t) get_receive_buffer(s);
                while (s->tcp.forward != NULL &&
                       s->tcp.forward->seq + s->tcp.forward->sent == s->tcp.remote_seq &&
                       s->tcp.forward->len - s->tcp.forward->sent < buffer_size) {
                    log_android(ANDROID_LOG_DEBUG, "%s fwd %u...%u sent %u",
                                session,
                                s->tcp.forward->seq - s->tcp.remote_start,
                                s->tcp.forward->seq + s->tcp.forward->len - s->tcp.remote_start,
                                s->tcp.forward->sent);

                    uint8_t *data = s->tcp.forward->data + s->tcp.forward->sent;
                    size_t len = s->tcp.forward->len - s->tcp.forward->sent;
                    size_t newlen = len;
                    uint8_t *new_data = 0;
                    if (args->proxyType == PROXY_TYPE_HTTP && s->tcp.is_http) {
                        new_data = patch_http_url(data, &newlen);
                        if (new_data) {
                            data = new_data;
                        }
                    }
                    ssize_t sent = send(s->socket,
                                        data,
                                        newlen,
                                        (unsigned int) (MSG_NOSIGNAL | (s->tcp.forward->psh
                                                                        ? 0
                                                                        : MSG_MORE)));
                    if (sent > len) {
                        sent = len;
                    }

                    if (sent < 0) {
                        log_android(ANDROID_LOG_ERROR, "%s send error %d: %s",
                                    session, errno, strerror(errno));
                        if (errno == EINTR || errno == EAGAIN) {
                            // Retry later
                            break;
                        } else {
                            write_rst(args, &s->tcp);
                            break;
                        }
                    } else {
                        fwd = 1;
                        buffer_size -= sent;
                        s->tcp.sent += sent;
                        s->tcp.forward->sent += sent;
                        s->tcp.remote_seq = s->tcp.forward->seq + s->tcp.forward->sent;

                        if (s->tcp.forward->len == s->tcp.forward->sent) {
                            struct segment *p = s->tcp.forward;
                            s->tcp.forward = s->tcp.forward->next;
                            free(p->data);
                            free(p);
                        } else {
                            log_android(ANDROID_LOG_WARN,
                                        "%s partial send %u/%u",
                                        session, s->tcp.forward->sent, s->tcp.forward->len);
                            break;
                        }
                    }
                }

                // Log data buffered
                struct segment *seg = s->tcp.forward;
                while (seg != NULL) {
                    log_android(ANDROID_LOG_WARN, "%s queued %u...%u sent %u",
                                session,
                                seg->seq - s->tcp.remote_start,
                                seg->seq + seg->len - s->tcp.remote_start,
                                seg->sent);
                    seg = seg->next;
                }
            }

            // Get receive window
            uint32_t window = get_receive_window(s);
            uint32_t prev = s->tcp.recv_window;
            s->tcp.recv_window = window;
            if ((prev == 0 && window > 0) || (prev > 0 && window == 0)) {
                log_android(ANDROID_LOG_WARN, "%s recv window %u > %u",
                            session, prev, window);
            }

            // Acknowledge forwarded data
            if (fwd || (prev == 0 && window > 0)) {
                if (fwd && s->tcp.forward == NULL && s->tcp.state == TCP_CLOSE_WAIT) {
                    log_android(ANDROID_LOG_WARN, "%s confirm FIN", session);
                    s->tcp.remote_seq++; // remote FIN
                }
                if (write_ack(args, &s->tcp) >= 0) {
                    s->tcp.time = time(NULL);
                }
            }

            if (s->tcp.state == TCP_ESTABLISHED || s->tcp.state == TCP_CLOSE_WAIT) {
                // Check socket read
                // Send window can be changed in the mean time

                uint32_t send_window = get_send_window(&s->tcp);
                if ((ev->events & EPOLLIN) && send_window > 0) {
                    s->tcp.time = time(NULL);

                    uint32_t buffer_size = (send_window > s->tcp.mss
                                            ? s->tcp.mss : send_window);
                    uint8_t *buffer = malloc(buffer_size);
                    ssize_t bytes = recv(s->socket, buffer, (size_t) buffer_size, 0);
                    if (bytes < 0) {
                        // Socket error
                        log_android(ANDROID_LOG_ERROR, "%s recv error %d: %s",
                                    session, errno, strerror(errno));

                        if (errno != EINTR && errno != EAGAIN) {
                            write_rst(args, &s->tcp);
                        }
                    } else if (bytes == 0) {
                        log_android(ANDROID_LOG_WARN, "%s recv eof", session);

                        if (s->tcp.forward == NULL) {
                            if (write_fin_ack(args, &s->tcp) >= 0) {
                                log_android(ANDROID_LOG_WARN, "%s FIN sent", session);
                                s->tcp.local_seq++; // local FIN
                            }

                            if (s->tcp.state == TCP_ESTABLISHED) {
                                s->tcp.state = TCP_FIN_WAIT1;
                            } else if (s->tcp.state == TCP_CLOSE_WAIT) {
                                s->tcp.state = TCP_LAST_ACK;
                            } else {
                                log_android(ANDROID_LOG_ERROR, "%s invalid close", session);
                            }
                        } else {
                            // There was still data to send
                            log_android(ANDROID_LOG_ERROR, "%s close with queue", session);
                            write_rst(args, &s->tcp);
                        }

                        if (close(s->socket)) {
                            log_android(ANDROID_LOG_ERROR, "%s close error %d: %s",
                                        session, errno, strerror(errno));
                        }
                        s->socket = -1;

                    } else {
                        // Socket read data
                        log_android(ANDROID_LOG_DEBUG, "%s recv bytes %d", session, bytes);
                        s->tcp.received += bytes;

                        // Proxy rejected a plain HTTP request: remember the challenge
                        // so that the following requests are authenticated
                        if (s->tcp.http_plain && auth_enabled() &&
                            http_status_code(buffer, (size_t) bytes) == 407) {
                            int hlen = http_header_length(buffer, (size_t) bytes);
                            auth_parse_challenge((const char *) buffer,
                                                 hlen > 0 ? (size_t) hlen : (size_t) bytes, NULL);
                        }

                        // Forward to tun
                        if (write_data(args, &s->tcp, buffer, (size_t) bytes) >= 0) {
                            s->tcp.local_seq += bytes;
                        }
                    }
                    free(buffer);
                }
            }
        }
    }

    if (s->tcp.state != oldstate || s->tcp.local_seq != oldlocal ||
        s->tcp.remote_seq != oldremote)
        log_android(ANDROID_LOG_DEBUG, "%s new state", session);
}

//#define DNS_LOOKUPS 1
//#define USELESS_DNS_LOOKUPS 1
static void lookup_hostname(struct sockaddr_in *addr, char *hostname, int hostlen, int needed) {
#ifdef DNS_LOOKUPS
    struct hostent	*host;

    if (needed)
    {
        if ((host = gethostbyaddr((char *)&addr->sin_addr,
                                  sizeof(addr->sin_addr), AF_INET)) != NULL) {
            strncpy(hostname, host->h_name, hostlen);
            hostname[hostlen - 1] = '\0';
        }  else {
            strncpy(hostname, inet_ntoa(addr->sin_addr), hostlen);
            hostname[hostlen - 1] = '\0';
        }
    } else{
# ifdef USELESS_DNS_LOOKUPS
        if ((host = gethostbyaddr((char *)&addr->sin_addr,
                                  sizeof(addr->sin_addr), AF_INET)) != NULL) {
            strncpy(hostname, host->h_name, hostlen);
            hostname[hostlen - 1] = '\0';
        } else {
            strncpy(hostname, inet_ntoa(addr->sin_addr), hostlen);
            hostname[hostlen - 1] = '\0';
        }
# else
        strncpy(hostname, inet_ntoa(addr->sin_addr), hostlen);
        hostname[hostlen - 1] = '\0';
# endif
    }
#else
    strncpy(hostname, inet_ntoa(addr->sin_addr), hostlen);
    hostname[hostlen - 1] = '\0';
#endif
}


jboolean handle_tcp(const struct arguments *args,
                    const uint8_t *pkt, size_t length,
                    const uint8_t *payload,
                    int uid,
                    const int epoll_fd) {

    // Get headers
    const uint8_t version = (*pkt) >> 4;
    const struct iphdr *ip4 = (struct iphdr *) pkt;
    const struct ip6_hdr *ip6 = (struct ip6_hdr *) pkt;
    const struct tcphdr *tcphdr = (struct tcphdr *) payload;
    const uint8_t tcpoptlen = (uint8_t) ((tcphdr->doff - 5) * 4);
    const uint8_t *tcpoptions = payload + sizeof(struct tcphdr);
    const uint8_t *data = payload + sizeof(struct tcphdr) + tcpoptlen;
    const uint16_t datalen = (const uint16_t) (length - (data - pkt));

    char hostname[512] = "";
    parse_tls_header((const char *) data, datalen, hostname);
    int len = strlen(hostname);


    int rport = htons(tcphdr->dest);

    struct allowed redirect;
    strcpy(redirect.raddr, args->proxyIp);
    redirect.rport = args->proxyPort;

    // Search session
    struct ng_session *cur = ng_session;
    while (cur != NULL &&
           !(cur->protocol == IPPROTO_TCP &&
             cur->tcp.version == version &&
             cur->tcp.source == tcphdr->source && cur->tcp.dest == tcphdr->dest &&
             (version == 4 ? cur->tcp.saddr.ip4 == ip4->saddr &&
                             cur->tcp.daddr.ip4 == ip4->daddr
                           : memcmp(&cur->tcp.saddr.ip6, &ip6->ip6_src, 16) == 0 &&
                             memcmp(&cur->tcp.daddr.ip6, &ip6->ip6_dst, 16) == 0))) {
        cur = cur->next;
    }


    // Prepare logging
    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];
    if (version == 4) {
        inet_ntop(AF_INET, &ip4->saddr, source, sizeof(source));
        inet_ntop(AF_INET, &ip4->daddr, dest, sizeof(dest));
    } else {
        inet_ntop(AF_INET6, &ip6->ip6_src, source, sizeof(source));
        inet_ntop(AF_INET6, &ip6->ip6_dst, dest, sizeof(dest));
    }

    char flags[10];
    int flen = 0;
    if (tcphdr->syn)
        flags[flen++] = 'S';
    if (tcphdr->ack)
        flags[flen++] = 'A';
    if (tcphdr->psh)
        flags[flen++] = 'P';
    if (tcphdr->fin)
        flags[flen++] = 'F';
    if (tcphdr->rst)
        flags[flen++] = 'R';
    if (tcphdr->urg)
        flags[flen++] = 'U';
    flags[flen] = 0;

    char packet[250];
    sprintf(packet,
            "TCP %s %s/%u > %s/%u seq %u ack %u data %u win %u uid %d",
            flags,
            source, ntohs(tcphdr->source),
            dest, ntohs(tcphdr->dest),
            ntohl(tcphdr->seq) - (cur == NULL ? 0 : cur->tcp.remote_start),
            tcphdr->ack ? ntohl(tcphdr->ack_seq) - (cur == NULL ? 0 : cur->tcp.local_start) : 0,
            datalen, ntohs(tcphdr->window), uid);
    log_android(tcphdr->urg ? ANDROID_LOG_WARN : ANDROID_LOG_DEBUG, packet);


    // Drop URG data
    if (tcphdr->urg)
        goto free;

    // Check session
    if (cur == NULL) {
        if (tcphdr->syn) {
            // Decode options
            // http://www.iana.org/assignments/tcp-parameters/tcp-parameters.xhtml#tcp-parameters-1
            uint16_t mss = get_default_mss(version);
            uint8_t ws = 0;
            int optlen = tcpoptlen;
            const uint8_t *options = tcpoptions;
            while (optlen > 0) {
                uint8_t kind = *options;
                uint8_t len = *(options + 1);
                if (kind == 0) {    // End of options list
                    break;
                }
                if (kind == 2 && len == 4) {
                    mss = ntohs(*((uint16_t *) (options + 2)));
                } else if (kind == 3 && len == 3) {
                    ws = *(options + 2);
                }

                if (kind == 1) {
                    optlen--;
                    options++;
                } else {
                    optlen -= len;
                    options += len;
                }
            }

            log_android(ANDROID_LOG_WARN, "%s new session mss %u ws %u window %u",
                        packet, mss, ws, ntohs(tcphdr->window) << ws);

            // Register session
            struct ng_session *s = malloc(sizeof(struct ng_session));
            s->protocol = IPPROTO_TCP;

            s->tcp.time = time(NULL);
            s->tcp.uid = uid;
            s->tcp.version = version;
            s->tcp.mss = mss;
            s->tcp.recv_scale = ws;
            s->tcp.send_scale = ws;
            s->tcp.send_window = ((uint32_t) ntohs(tcphdr->window)) << s->tcp.send_scale;
            s->tcp.remote_seq = ntohl(tcphdr->seq); // ISN remote
            s->tcp.local_seq = (uint32_t) rand(); // ISN local
            s->tcp.remote_start = s->tcp.remote_seq;
            s->tcp.local_start = s->tcp.local_seq;
            s->tcp.acked = 0;
            s->tcp.last_keep_alive = 0;
            s->tcp.sent = 0;
            s->tcp.received = 0;
            s->tcp.connect_sent = TCP_CONNECT_NOT_SENT;
            s->tcp.is_http = 0;
            s->tcp.is_tls = 0;
            s->tcp.hostname[0] = 0;
            s->tcp.socks5_state = 0;
            s->tcp.auth_retry = 0;
            s->tcp.http_plain = 0;

            if (version == 4) {
                s->tcp.saddr.ip4 = (__be32) ip4->saddr;
                s->tcp.daddr.ip4 = (__be32) ip4->daddr;
            } else {
                memcpy(&s->tcp.saddr.ip6, &ip6->ip6_src, 16);
                memcpy(&s->tcp.daddr.ip6, &ip6->ip6_dst, 16);
            }

            s->tcp.source = tcphdr->source;
            s->tcp.dest = tcphdr->dest;
            s->tcp.state = TCP_LISTEN;
            //  s->tcp.socks5 = SOCKS5_NONE;
            s->tcp.forward = NULL;
            s->next = NULL;

            if (datalen) {
                log_android(ANDROID_LOG_WARN, "%s SYN data", packet);
                s->tcp.forward = malloc(sizeof(struct segment));
                s->tcp.forward->seq = s->tcp.remote_seq;
                s->tcp.forward->len = datalen;
                s->tcp.forward->sent = 0;
                s->tcp.forward->psh = tcphdr->psh;

                s->tcp.forward->data = malloc(datalen);
                memcpy(s->tcp.forward->data, data, datalen);
                s->tcp.forward->next = NULL;
            }

            // Open socket
            s->socket = open_tcp_socket(args, &s->tcp, &redirect);
            if (s->socket < 0) {
                // Remote might retry
                free(s);
                goto free;
            }

            s->tcp.recv_window = get_receive_window(s);

            log_android(ANDROID_LOG_DEBUG, "TCP socket %d lport %d",
                        s->socket, get_local_port(s->socket));

            // Monitor events
            memset(&s->ev, 0, sizeof(struct epoll_event));
            s->ev.events = EPOLLOUT | EPOLLERR;
            s->ev.data.ptr = s;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, s->socket, &s->ev))
                log_android(ANDROID_LOG_ERROR, "epoll add tcp error %d: %s",
                            errno, strerror(errno));

            s->next = ng_session;
            ng_session = s;
        } else {
            log_android(ANDROID_LOG_WARN, "%s unknown session", packet);

            struct tcp_session rst;
            memset(&rst, 0, sizeof(struct tcp_session));
            rst.version = 4;
            rst.local_seq = ntohl(tcphdr->ack_seq);
            rst.remote_seq = ntohl(tcphdr->seq) + datalen + (tcphdr->syn || tcphdr->fin ? 1 : 0);

            if (version == 4) {
                rst.saddr.ip4 = (__be32) ip4->saddr;
                rst.daddr.ip4 = (__be32) ip4->daddr;
            } else {
                memcpy(&rst.saddr.ip6, &ip6->ip6_src, 16);
                memcpy(&rst.daddr.ip6, &ip6->ip6_dst, 16);
            }

            rst.source = tcphdr->source;
            rst.dest = tcphdr->dest;

            write_rst(args, &rst);
            goto free;
        }
    } else {
        int rport = htons(cur->tcp.dest);
        int is_tls = (len > 0) || (datalen > 0 && data[0] == 0x16);
        int is_http = is_http_payload(data, datalen);
        cur->tcp.is_http = is_http;
        cur->tcp.is_tls = is_tls;

        if (should_redirect(args, rport)) {
            if (len > 0) {
                strcpy(cur->tcp.hostname, hostname);
            } else if (cur->tcp.hostname[0] == 0) {
                if (cur->tcp.version == 4) {
                    struct sockaddr_in addr4;
                    addr4.sin_family = AF_INET;
                    addr4.sin_addr.s_addr = (__be32) cur->tcp.daddr.ip4;
                    addr4.sin_port = cur->tcp.dest;
                    lookup_hostname(&addr4, hostname, 512, 1);
                } else {
                    inet_ntop(AF_INET6, &cur->tcp.daddr.ip6, hostname, sizeof(hostname));
                }
                len = strlen(hostname);
                if (len > 0) {
                    strcpy(cur->tcp.hostname, hostname);
                }
            }

            if (cur->tcp.connect_sent == TCP_CONNECT_NOT_SENT && datalen > 0) {
                if (args->proxyType == PROXY_TYPE_SOCKS4) {
                    uint8_t request[300];
                    int reqlen = build_socks4_request(&cur->tcp, request, sizeof(request));
                    if (reqlen < 0) {
                        log_android(ANDROID_LOG_ERROR, "%s SOCKS4 unsupported destination %s",
                                    packet, cur->tcp.hostname);
                        write_rst(args, &cur->tcp);
                    } else if (send(cur->socket, request, (size_t) reqlen, MSG_NOSIGNAL) != reqlen) {
                        log_android(ANDROID_LOG_ERROR, "%s SOCKS4 connect send error %d: %s",
                                    packet, errno, strerror(errno));
                        write_rst(args, &cur->tcp);
                    } else {
                        cur->tcp.connect_sent = TCP_CONNECT_SENT;
                        cur->tcp.state = TCP_LISTEN;
                    }
                } else if (args->proxyType == PROXY_TYPE_SOCKS5) {
                    // Offer username/password authentication (0x02) when configured
                    uint8_t greeting[] = {0x05, 0x02, 0x00, 0x02};
                    size_t greeting_len = auth_enabled() ? 4 : 3;
                    if (!auth_enabled()) greeting[1] = 0x01;
                    ssize_t sent = send(cur->socket, greeting, greeting_len, MSG_NOSIGNAL);
                    if (sent < 0) {
                        log_android(ANDROID_LOG_ERROR, "%s SOCKS5 greeting send error %d: %s",
                                    packet, errno, strerror(errno));
                        write_rst(args, &cur->tcp);
                    } else {
                        cur->tcp.connect_sent = TCP_CONNECT_SENT;
                        cur->tcp.socks5_state = SOCKS5_STATE_GREETING;
                        cur->tcp.state = TCP_LISTEN;
                    }
                } else if (is_tls || rport == 443 || (!is_http && rport != 80)) {
                    if (cur->tcp.hostname[0] != 0) {
                        if (send_http_connect(&cur->tcp, cur->socket) < 0) {
                            write_rst(args, &cur->tcp);
                        } else {
                            cur->tcp.connect_sent = TCP_CONNECT_SENT;
                            cur->tcp.state = TCP_LISTEN;
                        }
                    }
                } else {
                    cur->tcp.connect_sent = TCP_CONNECT_ESTABLISHED;
                    cur->tcp.http_plain = (args->proxyType == PROXY_TYPE_HTTP);
                }
            }
        }
        if (should_redirect(args, rport) && cur->tcp.connect_sent != TCP_CONNECT_ESTABLISHED) {
            char session[250];
            sprintf(session,
                    "%s %s loc %u rem %u acked %u",
                    packet,
                    strstate(cur->tcp.state),
                    cur->tcp.local_seq - cur->tcp.local_start,
                    cur->tcp.remote_seq - cur->tcp.remote_start,
                    cur->tcp.acked - cur->tcp.local_start);
            queue_tcp(args, tcphdr, session, &cur->tcp, data, datalen);
            goto free;
        }


        char session[250];
        sprintf(session,
                "%s %s loc %u rem %u acked %u",
                packet,
                strstate(cur->tcp.state),
                cur->tcp.local_seq - cur->tcp.local_start,
                cur->tcp.remote_seq - cur->tcp.remote_start,
                cur->tcp.acked - cur->tcp.local_start);

        // Session found
        if (cur->tcp.state == TCP_CLOSING || cur->tcp.state == TCP_CLOSE) {
            log_android(ANDROID_LOG_WARN, "%s was closed", session);
            write_rst(args, &cur->tcp);
            goto free;
        } else {
            int oldstate = cur->tcp.state;
            uint32_t oldlocal = cur->tcp.local_seq;
            uint32_t oldremote = cur->tcp.remote_seq;

            log_android(ANDROID_LOG_DEBUG, "%s handling", session);

            cur->tcp.time = time(NULL);
            cur->tcp.send_window = ntohs(tcphdr->window) << cur->tcp.send_scale;

            // Do not change the order of the conditions

            // Queue data to forward
            if (datalen) {
                if (cur->socket < 0) {
                    log_android(ANDROID_LOG_ERROR, "%s data while local closed", session);
                    write_rst(args, &cur->tcp);
                    goto free;
                }
                if (cur->tcp.state == TCP_CLOSE_WAIT) {
                    log_android(ANDROID_LOG_ERROR, "%s data while remote closed", session);
                    write_rst(args, &cur->tcp);
                    goto free;
                }
                queue_tcp(args, tcphdr, session, &cur->tcp, data, datalen);
            }

            if (tcphdr->rst /* +ACK */) {
                // No sequence check
                // http://tools.ietf.org/html/rfc1122#page-87
                log_android(ANDROID_LOG_WARN, "%s received reset", session);
                cur->tcp.state = TCP_CLOSING;
                goto free;
            } else {
                if (!tcphdr->ack || ntohl(tcphdr->ack_seq) == cur->tcp.local_seq) {
                    if (tcphdr->syn) {
                        log_android(ANDROID_LOG_WARN, "%s repeated SYN", session);
                        // The socket is probably not opened yet

                    } else if (tcphdr->fin /* +ACK */) {
                        if (cur->tcp.state == TCP_ESTABLISHED) {
                            log_android(ANDROID_LOG_WARN, "%s FIN received", session);
                            if (cur->tcp.forward == NULL) {
                                cur->tcp.remote_seq++; // remote FIN
                                if (write_ack(args, &cur->tcp) >= 0)
                                    cur->tcp.state = TCP_CLOSE_WAIT;
                            } else {
                                cur->tcp.state = TCP_CLOSE_WAIT;
                            }
                        } else if (cur->tcp.state == TCP_CLOSE_WAIT) {
                            log_android(ANDROID_LOG_WARN, "%s repeated FIN", session);
                            // The socket is probably not closed yet
                        } else if (cur->tcp.state == TCP_FIN_WAIT1) {
                            log_android(ANDROID_LOG_WARN, "%s last ACK", session);
                            cur->tcp.remote_seq++; // remote FIN
                            if (write_ack(args, &cur->tcp) >= 0)
                                cur->tcp.state = TCP_CLOSE;
                        } else {
                            log_android(ANDROID_LOG_ERROR, "%s invalid FIN", session);
                            goto free;
                        }

                    } else if (tcphdr->ack) {
                        cur->tcp.acked = ntohl(tcphdr->ack_seq);

                        if (cur->tcp.state == TCP_SYN_RECV) {
                            cur->tcp.state = TCP_ESTABLISHED;
                        } else if (cur->tcp.state == TCP_ESTABLISHED) {
                            // Do nothing
                        } else if (cur->tcp.state == TCP_LAST_ACK) {
                            cur->tcp.state = TCP_CLOSING;
                        } else if (cur->tcp.state == TCP_CLOSE_WAIT) {
                            // ACK after FIN/ACK
                        } else if (cur->tcp.state == TCP_FIN_WAIT1) {
                            // Do nothing
                        } else {
                            log_android(ANDROID_LOG_ERROR, "%s invalid state", session);
                            goto free;
                        }
                    } else {
                        log_android(ANDROID_LOG_ERROR, "%s unknown packet", session);
                        goto free;
                    }
                } else {
                    uint32_t ack = ntohl(tcphdr->ack_seq);
                    if ((uint32_t) (ack + 1) == cur->tcp.local_seq) {
                        // Keep alive
                        if (cur->tcp.state == TCP_ESTABLISHED) {
                            int on = 1;
                            if (setsockopt(cur->socket, SOL_SOCKET, SO_KEEPALIVE, &on,
                                           sizeof(on))) {
                                log_android(ANDROID_LOG_ERROR,
                                            "%s setsockopt SO_KEEPALIVE error %d: %s",
                                            session, errno, strerror(errno));
                            } else {
                                log_android(ANDROID_LOG_WARN, "%s enabled keep alive", session);
                            }
                        } else {
                            log_android(ANDROID_LOG_WARN, "%s keep alive", session);
                        }

                    } else if (compare_u32(ack, cur->tcp.local_seq) < 0) {
                        if (compare_u32(ack, cur->tcp.acked) <= 0) {
                            log_android(
                                    ack == cur->tcp.acked ? ANDROID_LOG_WARN : ANDROID_LOG_ERROR,
                                    "%s repeated ACK %u/%u",
                                    session,
                                    ack - cur->tcp.local_start,
                                    cur->tcp.acked - cur->tcp.local_start);
                        } else {
                            log_android(ANDROID_LOG_WARN, "%s previous ACK %u",
                                        session, ack - cur->tcp.local_seq);
                            cur->tcp.acked = ack;
                        }

                        goto free;
                    } else {
                        log_android(ANDROID_LOG_ERROR, "%s future ACK", session);
                        write_rst(args, &cur->tcp);
                        goto free;
                    }
                }
            }

            if (cur->tcp.state != oldstate ||
                cur->tcp.local_seq != oldlocal ||
                cur->tcp.remote_seq != oldremote) {
                log_android(ANDROID_LOG_INFO, "%s > %s loc %u rem %u",
                            session,
                            strstate(cur->tcp.state),
                            cur->tcp.local_seq - cur->tcp.local_start,
                            cur->tcp.remote_seq - cur->tcp.remote_start);
            }
        }
    }
    free:

    return 1;
}

void queue_tcp(const struct arguments *args,
               const struct tcphdr *tcphdr,
               const char *session, struct tcp_session *cur,
               const uint8_t *data, uint16_t datalen) {
    uint32_t seq = ntohl(tcphdr->seq);
    if (compare_u32(seq, cur->remote_seq) < 0)
        log_android(ANDROID_LOG_WARN, "%s already forwarded %u..%u",
                    session,
                    seq - cur->remote_start, seq + datalen - cur->remote_start);
    else {
        struct segment *p = NULL;
        struct segment *s = cur->forward;
        while (s != NULL && compare_u32(s->seq, seq) < 0) {
            p = s;
            s = s->next;
        }

        if (s == NULL || compare_u32(s->seq, seq) > 0) {
            log_android(ANDROID_LOG_DEBUG, "%s queuing %u...%u",
                        session,
                        seq - cur->remote_start, seq + datalen - cur->remote_start);
            struct segment *n = malloc(sizeof(struct segment));
            n->seq = seq;
            n->len = datalen;
            n->sent = 0;
            n->psh = tcphdr->psh;
            n->data = malloc(datalen);
            memcpy(n->data, data, datalen);

            n->next = s;
            if (p == NULL) {
                cur->forward = n;
            } else {
                p->next = n;
            }
        } else if (s != NULL && s->seq == seq) {
            if (s->len == datalen) {
                log_android(ANDROID_LOG_WARN, "%s segment already queued %u..%u",
                            session,
                            s->seq - cur->remote_start, s->seq + s->len - cur->remote_start);
            } else if (s->len < datalen) {
                log_android(ANDROID_LOG_WARN, "%s segment smaller %u..%u > %u",
                            session,
                            s->seq - cur->remote_start, s->seq + s->len - cur->remote_start,
                            s->seq + datalen - cur->remote_start);
                free(s->data);
                s->data = malloc(datalen);
                memcpy(s->data, data, datalen);
                s->len = datalen;
            } else {
                log_android(ANDROID_LOG_ERROR, "%s segment larger %u..%u < %u",
                            session,
                            s->seq - cur->remote_start, s->seq + s->len - cur->remote_start,
                            s->seq + datalen - cur->remote_start);
            }
        }
    }
}

int open_tcp_socket(const struct arguments *args,
                    const struct tcp_session *cur, const struct allowed *redirect) {
    int sock;
    int version;

    int rport = htons(cur->dest);
    if (!should_redirect(args, rport)) {
        redirect = NULL;
    }

    if (redirect == NULL) {
        version = cur->version;
    } else {
        version = (strstr(redirect->raddr, ":") == NULL ? 4 : 6);
    }

    // Get TCP socket
    if ((sock = socket(version == 4 ? PF_INET : PF_INET6, SOCK_STREAM, 0)) < 0) {
        log_android(ANDROID_LOG_ERROR, "socket error %d: %s", errno, strerror(errno));
        return -1;
    }

    // Protect
    if (protect_socket(args, sock) < 0)
        return -1;

    // Set non blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        log_android(ANDROID_LOG_ERROR, "fcntl socket O_NONBLOCK error %d: %s",
                    errno, strerror(errno));
        return -1;
    }

    // Build target address
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    if (redirect == NULL) {
        if (version == 4) {
            addr4.sin_family = AF_INET;
            addr4.sin_addr.s_addr = (__be32) cur->daddr.ip4;
            addr4.sin_port = cur->dest;
        } else {
            addr6.sin6_family = AF_INET6;
            memcpy(&addr6.sin6_addr, &cur->daddr.ip6, 16);
            addr6.sin6_port = cur->dest;
        }
    } else {
        log_android(ANDROID_LOG_WARN, "TCP%d redirect to %s/%u",
                    version, redirect->raddr, redirect->rport);

        if (version == 4) {
            addr4.sin_family = AF_INET;
            inet_pton(AF_INET, redirect->raddr, &addr4.sin_addr);
            addr4.sin_port = htons(redirect->rport);
        } else {
            addr6.sin6_family = AF_INET6;
            inet_pton(AF_INET6, redirect->raddr, &addr6.sin6_addr);
            addr6.sin6_port = htons(redirect->rport);
        }
    }

    // Initiate connect
    int err = connect(sock,
                      (const struct sockaddr *) (version == 4 ? &addr4 : &addr6),
                      (socklen_t) (version == 4
                                   ? sizeof(struct sockaddr_in)
                                   : sizeof(struct sockaddr_in6)));
    if (err < 0 && errno != EINPROGRESS) {
        log_android(ANDROID_LOG_ERROR, "connect error %d: %s", errno, strerror(errno));
        return -1;
    }

    return sock;
}

int write_syn_ack(const struct arguments *args, struct tcp_session *cur) {
    if (write_tcp(args, cur, NULL, 0, 1, 1, 0, 0) < 0) {
        cur->state = TCP_CLOSING;
        return -1;
    }
    return 0;
}

int write_ack(const struct arguments *args, struct tcp_session *cur) {
    if (write_tcp(args, cur, NULL, 0, 0, 1, 0, 0) < 0) {
        cur->state = TCP_CLOSING;
        return -1;
    }
    return 0;
}

int write_data(const struct arguments *args, struct tcp_session *cur,
               const uint8_t *buffer, size_t length) {
    if (write_tcp(args, cur, buffer, length, 0, 1, 0, 0) < 0) {
        cur->state = TCP_CLOSING;
        return -1;
    }
    return 0;
}

int write_fin_ack(const struct arguments *args, struct tcp_session *cur) {
    if (write_tcp(args, cur, NULL, 0, 0, 1, 1, 0) < 0) {
        cur->state = TCP_CLOSING;
        return -1;
    }
    return 0;
}

void write_rst(const struct arguments *args, struct tcp_session *cur) {
    // https://www.snellman.net/blog/archive/2016-02-01-tcp-rst/
    int ack = 0;
    if (cur->state == TCP_LISTEN) {
        ack = 1;
        cur->remote_seq++; // SYN
    }
    write_tcp(args, cur, NULL, 0, 0, ack, 0, 1);
    if (cur->state != TCP_CLOSE) {
        cur->state = TCP_CLOSING;
    }
}

ssize_t write_tcp(const struct arguments *args, const struct tcp_session *cur,
                  const uint8_t *data, size_t datalen,
                  int syn, int ack, int fin, int rst) {
    size_t len;
    u_int8_t *buffer;
    struct tcphdr *tcp;
    uint16_t csum;
    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];

    // Build packet
    int optlen = (syn ? 4 + 3 + 1 : 0);
    uint8_t *options;
    if (cur->version == 4) {
        len = sizeof(struct iphdr) + sizeof(struct tcphdr) + optlen + datalen;
        buffer = malloc(len);
        struct iphdr *ip4 = (struct iphdr *) buffer;
        tcp = (struct tcphdr *) (buffer + sizeof(struct iphdr));
        options = buffer + sizeof(struct iphdr) + sizeof(struct tcphdr);
        if (datalen) {
            memcpy(buffer + sizeof(struct iphdr) + sizeof(struct tcphdr) + optlen, data, datalen);
        }

        // Build IP4 header
        memset(ip4, 0, sizeof(struct iphdr));
        ip4->version = 4;
        ip4->ihl = sizeof(struct iphdr) >> 2;
        ip4->tot_len = htons(len);
        ip4->ttl = IPDEFTTL;
        ip4->protocol = IPPROTO_TCP;
        ip4->saddr = cur->daddr.ip4;
        ip4->daddr = cur->saddr.ip4;

        // Calculate IP4 checksum
        ip4->check = ~calc_checksum(0, (uint8_t *) ip4, sizeof(struct iphdr));

        // Calculate TCP4 checksum
        struct ippseudo pseudo;
        memset(&pseudo, 0, sizeof(struct ippseudo));
        pseudo.ippseudo_src.s_addr = (__be32) ip4->saddr;
        pseudo.ippseudo_dst.s_addr = (__be32) ip4->daddr;
        pseudo.ippseudo_p = ip4->protocol;
        pseudo.ippseudo_len = htons(sizeof(struct tcphdr) + optlen + datalen);

        csum = calc_checksum(0, (uint8_t *) &pseudo, sizeof(struct ippseudo));
    } else {
        len = sizeof(struct ip6_hdr) + sizeof(struct tcphdr) + optlen + datalen;
        buffer = malloc(len);
        struct ip6_hdr *ip6 = (struct ip6_hdr *) buffer;
        tcp = (struct tcphdr *) (buffer + sizeof(struct ip6_hdr));
        options = buffer + sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
        if (datalen) {
            memcpy(buffer + sizeof(struct ip6_hdr) + sizeof(struct tcphdr) + optlen, data, datalen);
        }

        // Build IP6 header
        memset(ip6, 0, sizeof(struct ip6_hdr));
        ip6->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(len - sizeof(struct ip6_hdr));
        ip6->ip6_ctlun.ip6_un1.ip6_un1_nxt = IPPROTO_TCP;
        ip6->ip6_ctlun.ip6_un1.ip6_un1_hlim = IPDEFTTL;
        ip6->ip6_ctlun.ip6_un2_vfc = 0x60;
        memcpy(&(ip6->ip6_src), &cur->daddr.ip6, 16);
        memcpy(&(ip6->ip6_dst), &cur->saddr.ip6, 16);

        // Calculate TCP6 checksum
        struct ip6_hdr_pseudo pseudo;
        memset(&pseudo, 0, sizeof(struct ip6_hdr_pseudo));
        memcpy(&pseudo.ip6ph_src, &ip6->ip6_dst, 16);
        memcpy(&pseudo.ip6ph_dst, &ip6->ip6_src, 16);
        pseudo.ip6ph_len = ip6->ip6_ctlun.ip6_un1.ip6_un1_plen;
        pseudo.ip6ph_nxt = ip6->ip6_ctlun.ip6_un1.ip6_un1_nxt;

        csum = calc_checksum(0, (uint8_t *) &pseudo, sizeof(struct ip6_hdr_pseudo));
    }


    // Build TCP header
    memset(tcp, 0, sizeof(struct tcphdr));
    tcp->source = cur->dest;
    tcp->dest = cur->source;
    tcp->seq = htonl(cur->local_seq);
    tcp->ack_seq = htonl((uint32_t) (cur->remote_seq));
    tcp->doff = (__u16) ((sizeof(struct tcphdr) + optlen) >> 2);
    tcp->syn = (__u16) syn;
    tcp->ack = (__u16) ack;
    tcp->fin = (__u16) fin;
    tcp->rst = (__u16) rst;
    tcp->window = htons(cur->recv_window >> cur->recv_scale);

    if (!tcp->ack) {
        tcp->ack_seq = 0;
    }

    // TCP options
    if (syn) {
        *(options) = 2; // MSS
        *(options + 1) = 4; // total option length
        *((uint16_t *) (options + 2)) = get_default_mss(cur->version);

        *(options + 4) = 3; // window scale
        *(options + 5) = 3; // total option length
        *(options + 6) = cur->recv_scale;

        *(options + 7) = 0; // End, padding
    }

    // Continue checksum
    csum = calc_checksum(csum, (uint8_t *) tcp, sizeof(struct tcphdr));
    csum = calc_checksum(csum, options, (size_t) optlen);
    csum = calc_checksum(csum, data, datalen);
    tcp->check = ~csum;

    inet_ntop(cur->version == 4 ? AF_INET : AF_INET6,
              cur->version == 4 ? &cur->saddr.ip4 : &cur->saddr.ip6, source, sizeof(source));
    inet_ntop(cur->version == 4 ? AF_INET : AF_INET6,
              cur->version == 4 ? &cur->daddr.ip4 : &cur->daddr.ip6, dest, sizeof(dest));

    // Send packet
    log_android(ANDROID_LOG_DEBUG,
                "TCP sending%s%s%s%s to tun %s/%u seq %u ack %u data %u",
                (tcp->syn ? " SYN" : ""),
                (tcp->ack ? " ACK" : ""),
                (tcp->fin ? " FIN" : ""),
                (tcp->rst ? " RST" : ""),
                dest, ntohs(tcp->dest),
                ntohl(tcp->seq) - cur->local_start,
                ntohl(tcp->ack_seq) - cur->remote_start,
                datalen);

    ssize_t res = write(args->tun, buffer, len);

    free(buffer);

    if (res != len) {
        log_android(ANDROID_LOG_ERROR, "TCP write %d/%d", res, len);
        return -1;
    }

    return res;
}
