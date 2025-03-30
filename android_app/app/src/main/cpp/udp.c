#include "tun2http.h"

extern struct ng_session *ng_session;
extern FILE *pcap_file;

int get_udp_timeout(const struct udp_session *u, int sessions, int maxsessions) {
    int timeout;
    
    // Different timeout for different UDP services
    int port = ntohs(u->dest);
    if (port == 53) {
        // DNS - short timeout
        timeout = UDP_TIMEOUT_53;
    } 
    else if (port == 123) {
        // NTP - medium timeout
        timeout = 60; // 60 seconds
    }
    else if (port >= 3478 && port <= 3483) {
        // STUN/TURN - longer timeout
        timeout = 300; // 5 minutes
    }
    else if (port >= 5000 && port <= 5500) {
        // Common media streaming ports - longer timeout
        timeout = 600; // 10 minutes
    }
    else {
        // Default timeout for other UDP services
        timeout = UDP_TIMEOUT_ANY;
    }

    // Scale timeout based on load, but maintain minimum values
    int scale = 100 - sessions * 100 / maxsessions;
    scale = (scale < 30) ? 30 : scale; // Don't go below 30%
    timeout = timeout * scale / 100;

    return timeout;
}

// Identify UDP protocol based on port and initial data
int identify_udp_protocol(const struct udp_session *u, const uint8_t *data, size_t datalen) {
    int port = ntohs(u->dest);
    
    // DNS query (port 53)
    if (port == 53)
        return 53;
    
    // NTP (port 123)
    if (port == 123)
        return 123;
    
    // DHCP client/server (67/68)
    if (port == 67 || port == 68)
        return port;
    
    // STUN/TURN (3478-3483)
    if (port >= 3478 && port <= 3483)
        return 3478;
    
    // RTP/RTCP (common ports)
    if ((port >= 5000 && port <= 5500) || (port >= 16384 && port <= 32767))
        return 5000;
    
    // Check data patterns for protocol fingerprinting if we have data
    if (data && datalen > 4) {
        // Simple heuristics for common protocols
        // More could be added based on protocol specifications
    }
    
    // Default - unknown UDP protocol
    return 0;
}

// Set appropriate socket options based on protocol
void configure_udp_socket(int socket, int protocol) {
    int optval = 1;
    
    // Enable address/port reuse
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        log_android(ANDROID_LOG_WARN, "UDP setsockopt SO_REUSEADDR error %d: %s", 
                   errno, strerror(errno));
    }
    
    // Common settings for all UDP protocols
    if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) < 0) {
        log_android(ANDROID_LOG_WARN, "UDP setsockopt SO_BROADCAST error %d: %s", 
                   errno, strerror(errno));
    }
    
    // Set specific buffer sizes based on protocol
    int rcvbuf = 0;
    int sndbuf = 0;
    
    if (protocol == 53) {
        // DNS needs smaller buffers
        rcvbuf = 32768; // 32KB
        sndbuf = 32768; // 32KB
    } 
    else if (protocol == 5000) {
        // Media streaming needs larger buffers
        rcvbuf = 262144; // 256KB
        sndbuf = 262144; // 256KB
    }
    else {
        // Default size
        rcvbuf = 131072; // 128KB
        sndbuf = 131072; // 128KB
    }
    
    if (rcvbuf > 0) {
        if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
            log_android(ANDROID_LOG_WARN, "UDP setsockopt SO_RCVBUF error %d: %s", 
                       errno, strerror(errno));
        }
    }
    
    if (sndbuf > 0) {
        if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
            log_android(ANDROID_LOG_WARN, "UDP setsockopt SO_SNDBUF error %d: %s", 
                       errno, strerror(errno));
        }
    }
    
    // Set appropriate timeouts
    struct timeval tv;
    tv.tv_sec = (protocol == 53) ? 5 : 30; // 5 seconds for DNS, 30 seconds for others
    tv.tv_usec = 0;
    
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        log_android(ANDROID_LOG_WARN, "UDP setsockopt SO_RCVTIMEO error %d: %s", 
                   errno, strerror(errno));
    }
    
    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        log_android(ANDROID_LOG_WARN, "UDP setsockopt SO_SNDTIMEO error %d: %s", 
                   errno, strerror(errno));
    }
}

int check_udp_session(const struct arguments *args, struct ng_session *s,
                      int sessions, int maxsessions) {
    time_t now = time(NULL);

    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];
    if (s->udp.version == 4) {
        inet_ntop(AF_INET, &s->udp.saddr.ip4, source, sizeof(source));
        inet_ntop(AF_INET, &s->udp.daddr.ip4, dest, sizeof(dest));
    } else {
        inet_ntop(AF_INET6, &s->udp.saddr.ip6, source, sizeof(source));
        inet_ntop(AF_INET6, &s->udp.daddr.ip6, dest, sizeof(dest));
    }

    // Check session timeout
    int timeout = get_udp_timeout(&s->udp, sessions, maxsessions);
    if (s->udp.state == UDP_ACTIVE && s->udp.time + timeout < now) {
        log_android(ANDROID_LOG_WARN, "UDP idle %d/%d sec state %d from %s/%u to %s/%u",
                    now - s->udp.time, timeout, s->udp.state,
                    source, ntohs(s->udp.source), dest, ntohs(s->udp.dest));
        s->udp.state = UDP_FINISHING;
    }

    // Check finished sessions
    if (s->udp.state == UDP_FINISHING) {
        log_android(ANDROID_LOG_INFO, "UDP close from %s/%u to %s/%u socket %d",
                    source, ntohs(s->udp.source), dest, ntohs(s->udp.dest), s->socket);

        if (s->socket >= 0) {
            if (close(s->socket))
                log_android(ANDROID_LOG_ERROR, "UDP close %d error %d: %s",
                            s->socket, errno, strerror(errno));
            s->socket = -1;
        }

        s->udp.time = time(NULL);
        s->udp.state = UDP_CLOSED;
    }

    if (s->udp.state == UDP_CLOSED && (s->udp.sent || s->udp.received)) {
        log_android(ANDROID_LOG_INFO, "UDP closed session traffic stats: sent=%llu received=%llu",
                   s->udp.sent, s->udp.received);
        s->udp.sent = 0;
        s->udp.received = 0;
    }

    // Cleanup lingering sessions
    if ((s->udp.state == UDP_CLOSED || s->udp.state == UDP_BLOCKED) &&
        s->udp.time + UDP_KEEP_TIMEOUT < now) {
        log_android(ANDROID_LOG_INFO, "UDP cleaning up session from %s/%u to %s/%u after %d seconds",
                   source, ntohs(s->udp.source), dest, ntohs(s->udp.dest), 
                   (int)(now - s->udp.time));
        return 1;
    }

    return 0;
}

void check_udp_socket(const struct arguments *args, const struct epoll_event *ev) {
    struct ng_session *s = (struct ng_session *) ev->data.ptr;

    // Check socket error
    if (ev->events & EPOLLERR) {
        s->udp.time = time(NULL);

        int serr = 0;
        socklen_t optlen = sizeof(int);
        int err = getsockopt(s->socket, SOL_SOCKET, SO_ERROR, &serr, &optlen);
        if (err < 0)
            log_android(ANDROID_LOG_ERROR, "UDP getsockopt error %d: %s",
                        errno, strerror(errno));
        else if (serr)
            log_android(ANDROID_LOG_ERROR, "UDP SO_ERROR %d: %s", serr, strerror(serr));

        s->udp.state = UDP_FINISHING;
    } else {
        // Check socket read
        if (ev->events & EPOLLIN) {
            s->udp.time = time(NULL);

            uint8_t *buffer = malloc(s->udp.mss);
            ssize_t bytes = recv(s->socket, buffer, s->udp.mss, 0);
            if (bytes < 0) {
                // Socket error
                log_android(ANDROID_LOG_WARN, "UDP recv error %d: %s",
                            errno, strerror(errno));

                if (errno != EINTR && errno != EAGAIN)
                    s->udp.state = UDP_FINISHING;
            } else if (bytes == 0) {
                log_android(ANDROID_LOG_WARN, "UDP recv eof");
                s->udp.state = UDP_FINISHING;

            } else {
                // Socket read data
                char dest[INET6_ADDRSTRLEN + 1];
                if (s->udp.version == 4)
                    inet_ntop(AF_INET, &s->udp.daddr.ip4, dest, sizeof(dest));
                else
                    inet_ntop(AF_INET6, &s->udp.daddr.ip6, dest, sizeof(dest));
                log_android(ANDROID_LOG_INFO, "UDP recv bytes %d from %s/%u for tun",
                            bytes, dest, ntohs(s->udp.dest));

                s->udp.received += bytes;

                // Process DNS response
                if (ntohs(s->udp.dest) == 53)
                    parse_dns_response(args, &s->udp, buffer, (size_t *) &bytes);

                // Forward to tun
                if (write_udp(args, &s->udp, buffer, (size_t) bytes) < 0)
                    s->udp.state = UDP_FINISHING;
                else {
                    // Prevent too many open files
                    if (ntohs(s->udp.dest) == 53)
                        s->udp.state = UDP_FINISHING;
                }
            }
            free(buffer);
        }
    }
}

int has_udp_session(const struct arguments *args, const uint8_t *pkt, const uint8_t *payload) {
    // Get headers
    const uint8_t version = (*pkt) >> 4;
    const struct iphdr *ip4 = (struct iphdr *) pkt;
    const struct ip6_hdr *ip6 = (struct ip6_hdr *) pkt;
    const struct udphdr *udphdr = (struct udphdr *) payload;

    if (ntohs(udphdr->dest) == 53 && !args->fwd53)
        return 1;

    // Search session
    struct ng_session *cur = ng_session;
    while (cur != NULL &&
           !(cur->protocol == IPPROTO_UDP &&
             cur->udp.version == version &&
             cur->udp.source == udphdr->source && cur->udp.dest == udphdr->dest &&
             (version == 4 ? cur->udp.saddr.ip4 == ip4->saddr &&
                             cur->udp.daddr.ip4 == ip4->daddr
                           : memcmp(&cur->udp.saddr.ip6, &ip6->ip6_src, 16) == 0 &&
                             memcmp(&cur->udp.daddr.ip6, &ip6->ip6_dst, 16) == 0)))
        cur = cur->next;

    return (cur != NULL);
}

void block_udp(const struct arguments *args,
               const uint8_t *pkt, size_t length,
               const uint8_t *payload,
               int uid) {
    // Get headers
    const uint8_t version = (*pkt) >> 4;
    const struct iphdr *ip4 = (struct iphdr *) pkt;
    const struct ip6_hdr *ip6 = (struct ip6_hdr *) pkt;
    const struct udphdr *udphdr = (struct udphdr *) payload;

    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];
    if (version == 4) {
        inet_ntop(AF_INET, &ip4->saddr, source, sizeof(source));
        inet_ntop(AF_INET, &ip4->daddr, dest, sizeof(dest));
    } else {
        inet_ntop(AF_INET6, &ip6->ip6_src, source, sizeof(source));
        inet_ntop(AF_INET6, &ip6->ip6_dst, dest, sizeof(dest));
    }

    log_android(ANDROID_LOG_INFO, "UDP blocked session from %s/%u to %s/%u",
                source, ntohs(udphdr->source), dest, ntohs(udphdr->dest));

    // Register session
    struct ng_session *s = malloc(sizeof(struct ng_session));
    s->protocol = IPPROTO_UDP;

    s->udp.time = time(NULL);
    s->udp.uid = uid;
    s->udp.version = version;

    if (version == 4) {
        s->udp.saddr.ip4 = (__be32) ip4->saddr;
        s->udp.daddr.ip4 = (__be32) ip4->daddr;
    } else {
        memcpy(&s->udp.saddr.ip6, &ip6->ip6_src, 16);
        memcpy(&s->udp.daddr.ip6, &ip6->ip6_dst, 16);
    }

    s->udp.source = udphdr->source;
    s->udp.dest = udphdr->dest;
    s->udp.state = UDP_BLOCKED;
    s->socket = -1;

    s->next = ng_session;
    ng_session = s;
}

jboolean handle_udp(const struct arguments *args,
                    const uint8_t *pkt, size_t length,
                    const uint8_t *payload,
                    int uid,
                    const int epoll_fd) {
    // Get headers
    const uint8_t version = (*pkt) >> 4;
    const struct iphdr *ip4 = (struct iphdr *) pkt;
    const struct ip6_hdr *ip6 = (struct ip6_hdr *) pkt;
    const struct udphdr *udphdr = (struct udphdr *) payload;
    const uint8_t *data = payload + sizeof(struct udphdr);
    const size_t datalen = length - (data - pkt);

    // Get DNS server port
    int dport = ntohs(udphdr->dest);
    int sport = ntohs(udphdr->source);

    // Search session
    struct ng_session *cur = ng_session;
    while (cur != NULL &&
           !(cur->protocol == IPPROTO_UDP &&
             cur->udp.version == version &&
             cur->udp.source == udphdr->source && cur->udp.dest == udphdr->dest &&
             (version == 4 ? cur->udp.saddr.ip4 == ip4->saddr &&
                             cur->udp.daddr.ip4 == ip4->daddr
                           : memcmp(&cur->udp.saddr.ip6, &ip6->ip6_src, 16) == 0 &&
                             memcmp(&cur->udp.daddr.ip6, &ip6->ip6_dst, 16) == 0)))
        cur = cur->next;

    // Create new session if needed
    if (cur == NULL) {
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

        // Check if DNS (possibly to be forwarded)
        if (dport == 53 && !args->fwd53) {
            log_android(ANDROID_LOG_INFO, "UDP blocking DNS (fwd53 disabled) from %s/%u to %s/%u",
                     source, sport, dest, dport);
            block_udp(args, pkt, length, payload, uid);
            return 1;
        }

        // Check if localhost allowed
        if (dport == 53 && args->fwd53 && datalen > 0)
            if (!check_domain(args, NULL, data, (size_t) datalen, 0, 0, NULL)) {
                log_android(ANDROID_LOG_WARN, "UDP blocking localhost from %s/%u to %s/%u",
                         source, sport, dest, dport);
                block_udp(args, pkt, length, payload, uid);
                return 1;
            }

        // Identify protocol
        int protocol = identify_udp_protocol(NULL, data, datalen);
        log_android(ANDROID_LOG_INFO, "UDP new session from %s/%u to %s/%u protocol %d uid %d",
                 source, sport, dest, dport, protocol, uid);

        // Register session
        struct ng_session *s = malloc(sizeof(struct ng_session));
        s->protocol = IPPROTO_UDP;

        s->udp.time = time(NULL);
        s->udp.uid = uid;
        s->udp.version = version;
        s->udp.mss = 0;

        s->udp.sent = 0;
        s->udp.received = 0;

        if (version == 4) {
            s->udp.saddr.ip4 = (__be32) ip4->saddr;
            s->udp.daddr.ip4 = (__be32) ip4->daddr;
        } else {
            memcpy(&s->udp.saddr.ip6, &ip6->ip6_src, 16);
            memcpy(&s->udp.daddr.ip6, &ip6->ip6_dst, 16);
        }

        s->udp.source = udphdr->source;
        s->udp.dest = udphdr->dest;
        s->udp.state = UDP_ACTIVE;

        // Get UDP socket
        struct allowed redirect;
        if (dport == 53 && args->fwd53) {
            // Redirect DNS UDP to the provided proxy server if available
            if (strlen(args->proxyIp) > 0) {
                strcpy(redirect.raddr, args->proxyIp);
                redirect.rport = args->proxyPort;
                s->socket = open_udp_socket(args, &s->udp, &redirect);
            } else {
                // Use direct connection if no proxy specified
                s->socket = open_udp_socket(args, &s->udp, NULL);
            }
        } else {
            // Handle other UDP protocols based on proxy configuration
            if (strlen(args->proxyIp) > 0 && 
                (dport == 123 || dport == 1900 || dport == 67 || dport == 68)) {
                // Redirect specific UDP services through proxy
                strcpy(redirect.raddr, args->proxyIp);
                redirect.rport = args->proxyPort;
                s->socket = open_udp_socket(args, &s->udp, &redirect);
            } else {
                // Direct connection for other UDP
                s->socket = open_udp_socket(args, &s->udp, NULL);
            }
        }

        if (s->socket < 0) {
            log_android(ANDROID_LOG_ERROR, "UDP socket error %d: %s", errno, strerror(errno));
            free(s);
            return 0;
        }

        s->udp.mss = (uint16_t) (s->udp.version == 4 ? UDP4_MAXMSG : UDP6_MAXMSG);
        
        // Configure socket settings based on protocol
        configure_udp_socket(s->socket, protocol);

        // Monitor events
        memset(&s->ev, 0, sizeof(struct epoll_event));
        s->ev.events = EPOLLIN | EPOLLERR;
        s->ev.data.ptr = s;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, s->socket, &s->ev)) {
            log_android(ANDROID_LOG_ERROR, "UDP epoll add error %d: %s", errno, strerror(errno));
            if (close(s->socket))
                log_android(ANDROID_LOG_ERROR, "UDP close %d error %d: %s",
                            s->socket, errno, strerror(errno));
            free(s);
            return 0;
        }

        cur = s;
        s->next = ng_session;
        ng_session = s;
    }

    // Log UDP forward
    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];
    if (version == 4) {
        inet_ntop(AF_INET, &ip4->saddr, source, sizeof(source));
        inet_ntop(AF_INET, &ip4->daddr, dest, sizeof(dest));
    } else {
        inet_ntop(AF_INET6, &ip6->ip6_src, source, sizeof(source));
        inet_ntop(AF_INET6, &ip6->ip6_dst, dest, sizeof(dest));
    }
    log_android(ANDROID_LOG_DEBUG, "UDP forward from tun %s/%u to %s/%u data %d",
                source, sport, dest, dport, datalen);

    // Forward to socket
    int rversion;
    if (cur->udp.version == 4) {
        struct sockaddr_in daddr;
        daddr.sin_family = AF_INET;
        daddr.sin_addr.s_addr = (__be32) cur->udp.daddr.ip4;
        daddr.sin_port = cur->udp.dest;

        rversion = 4;
        struct sockaddr_in addr4;
        addr4.sin_family = AF_INET;
        addr4.sin_addr.s_addr = (__be32) cur->udp.daddr.ip4;
        addr4.sin_port = cur->udp.dest;

        if (sendto(cur->socket, data, (socklen_t) datalen, 0,
                   (const struct sockaddr *) &addr4, sizeof(addr4)) != datalen) {
            log_android(ANDROID_LOG_ERROR, "UDP sendto %s/%u error %d: %s",
                     dest, dport, errno, strerror(errno));
            if (errno != EINTR && errno != EAGAIN) {
                cur->udp.state = UDP_FINISHING;
                return 0;
            }
        }
    } else {
        struct sockaddr_in6 daddr;
        daddr.sin6_family = AF_INET6;
        memcpy(&daddr.sin6_addr, &cur->udp.daddr.ip6, 16);
        daddr.sin6_port = cur->udp.dest;
        daddr.sin6_flowinfo = 0;
        daddr.sin6_scope_id = 0;

        rversion = 6;
        struct sockaddr_in6 addr6;
        addr6.sin6_family = AF_INET6;
        memcpy(&addr6.sin6_addr, &cur->udp.daddr.ip6, 16);
        addr6.sin6_port = cur->udp.dest;
        addr6.sin6_flowinfo = 0;
        addr6.sin6_scope_id = 0;

        if (sendto(cur->socket, data, (socklen_t) datalen, 0,
                   (const struct sockaddr *) &addr6, sizeof(addr6)) != datalen) {
            log_android(ANDROID_LOG_ERROR, "UDP sendto %s/%u error %d: %s",
                     dest, dport, errno, strerror(errno));
            if (errno != EINTR && errno != EAGAIN) {
                cur->udp.state = UDP_FINISHING;
                return 0;
            }
        }
    }

    cur->udp.time = time(NULL);
    cur->udp.sent += datalen;

    return 1;
}

int open_udp_socket(const struct arguments *args,
                    const struct udp_session *cur, const struct allowed *redirect) {
    int sock;
    int version;
    
    int dport = ntohs(cur->dest);
    if (redirect == NULL) {
        version = cur->version;
    } else
        version = (strstr(redirect->raddr, ":") == NULL ? 4 : 6);

    // Get UDP socket
    if ((sock = socket(version == 4 ? PF_INET : PF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        log_android(ANDROID_LOG_ERROR, "UDP socket error %d: %s", errno, strerror(errno));
        return -1;
    }

    // Protect socket
    if (protect_socket(args, sock) < 0)
        return -1;

    // Set non blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        log_android(ANDROID_LOG_ERROR, "UDP fcntl O_NONBLOCK error %d: %s",
                    errno, strerror(errno));
        if (close(sock))
            log_android(ANDROID_LOG_ERROR, "UDP close %d error %d: %s", sock, errno, strerror(errno));
        return -1;
    }

    // Build target address
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    if (redirect == NULL) {
        // Nothing to do
    } else {
        log_android(ANDROID_LOG_WARN, "UDP%d redirect to %s/%u",
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

    // Handle UDP broadcast
    if (dport == 67 || dport == 68) {
        int bradopt = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bradopt, sizeof(bradopt)) < 0) {
            log_android(ANDROID_LOG_WARN, "UDP setsockopt SO_BROADCAST error %d: %s", 
                       errno, strerror(errno));
        }
    }

    // Bind socket to any address
    if (version == 4) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = 0;
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)))
            log_android(ANDROID_LOG_ERROR, "UDP bind error %d: %s", errno, strerror(errno));
    } else {
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = 0;
        addr.sin6_addr = in6addr_any;
        if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)))
            log_android(ANDROID_LOG_ERROR, "UDP bind error %d: %s", errno, strerror(errno));
    }

    // Join multicast groups
    if (cur->version == 4) {
        uint32_t addr = (__be32) cur->daddr.ip4;
        if ((addr & htonl(0xF0000000)) == htonl(0xE0000000)) { // 224.0.0.0/4 (RFC 3171)
            struct ip_mreq mreq4;
            mreq4.imr_multiaddr.s_addr = addr;
            mreq4.imr_interface.s_addr = INADDR_ANY;
            if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq4, sizeof(mreq4)))
                log_android(ANDROID_LOG_ERROR, "UDP setsockopt IP_ADD_MEMBERSHIP error %d: %s",
                            errno, strerror(errno));
        }
    } else {
        // Check for IPv6 multicast (RFC 2375)
        if (cur->daddr.ip6.s6_addr[0] == 0xFF) {
            struct ipv6_mreq mreq6;
            memcpy(&mreq6.ipv6mr_multiaddr, &cur->daddr.ip6, sizeof(struct in6_addr));
            mreq6.ipv6mr_interface = 0;
            if (setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)))
                log_android(ANDROID_LOG_ERROR, "UDP setsockopt IPV6_ADD_MEMBERSHIP error %d: %s",
                            errno, strerror(errno));
        }
    }

    return sock;
}

ssize_t write_udp(const struct arguments *args, const struct udp_session *cur,
                  uint8_t *data, size_t datalen) {
    size_t len;
    u_int8_t *buffer;
    struct udphdr *udp;
    uint16_t csum;
    char source[INET6_ADDRSTRLEN + 1];
    char dest[INET6_ADDRSTRLEN + 1];

    // Build packet
    if (cur->version == 4) {
        len = sizeof(struct iphdr) + sizeof(struct udphdr) + datalen;
        buffer = malloc(len);
        struct iphdr *ip4 = (struct iphdr *) buffer;
        udp = (struct udphdr *) (buffer + sizeof(struct iphdr));
        if (datalen)
            memcpy(buffer + sizeof(struct iphdr) + sizeof(struct udphdr), data, datalen);

        // Build IP4 header
        memset(ip4, 0, sizeof(struct iphdr));
        ip4->version = 4;
        ip4->ihl = sizeof(struct iphdr) >> 2;
        ip4->tot_len = htons(len);
        ip4->ttl = IPDEFTTL;
        ip4->protocol = IPPROTO_UDP;
        ip4->saddr = cur->daddr.ip4;
        ip4->daddr = cur->saddr.ip4;

        // Calculate IP4 checksum
        ip4->check = ~calc_checksum(0, (uint8_t *) ip4, sizeof(struct iphdr));

        // Calculate UDP4 checksum
        struct ippseudo pseudo;
        memset(&pseudo, 0, sizeof(struct ippseudo));
        pseudo.ippseudo_src.s_addr = (__be32) ip4->saddr;
        pseudo.ippseudo_dst.s_addr = (__be32) ip4->daddr;
        pseudo.ippseudo_p = ip4->protocol;
        pseudo.ippseudo_len = htons(sizeof(struct udphdr) + datalen);

        csum = calc_checksum(0, (uint8_t *) &pseudo, sizeof(struct ippseudo));
    } else {
        len = sizeof(struct ip6_hdr) + sizeof(struct udphdr) + datalen;
        buffer = malloc(len);
        struct ip6_hdr *ip6 = (struct ip6_hdr *) buffer;
        udp = (struct udphdr *) (buffer + sizeof(struct ip6_hdr));
        if (datalen)
            memcpy(buffer + sizeof(struct ip6_hdr) + sizeof(struct udphdr), data, datalen);

        // Build IP6 header
        memset(ip6, 0, sizeof(struct ip6_hdr));
        ip6->ip6_ctlun.ip6_un1.ip6_un1_flow = 0;
        ip6->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(len - sizeof(struct ip6_hdr));
        ip6->ip6_ctlun.ip6_un1.ip6_un1_nxt = IPPROTO_UDP;
        ip6->ip6_ctlun.ip6_un1.ip6_un1_hlim = IPDEFTTL;
        ip6->ip6_ctlun.ip6_un2_vfc = IPV6_VERSION;
        memcpy(&(ip6->ip6_src), &cur->daddr.ip6, 16);
        memcpy(&(ip6->ip6_dst), &cur->saddr.ip6, 16);

        // Calculate UDP6 checksum
        struct ip6_hdr_pseudo pseudo;
        memset(&pseudo, 0, sizeof(struct ip6_hdr_pseudo));
        memcpy(&pseudo.ip6ph_src, &ip6->ip6_dst, 16);
        memcpy(&pseudo.ip6ph_dst, &ip6->ip6_src, 16);
        pseudo.ip6ph_len = ip6->ip6_ctlun.ip6_un1.ip6_un1_plen;
        pseudo.ip6ph_nxt = ip6->ip6_ctlun.ip6_un1.ip6_un1_nxt;

        csum = calc_checksum(0, (uint8_t *) &pseudo, sizeof(struct ip6_hdr_pseudo));
    }

    // Build UDP header
    memset(udp, 0, sizeof(struct udphdr));
    udp->source = cur->dest;
    udp->dest = cur->source;
    udp->len = htons(sizeof(struct udphdr) + datalen);

    // Continue checksum
    csum = calc_checksum(csum, (uint8_t *) udp, sizeof(struct udphdr));
    csum = calc_checksum(csum, data, datalen);
    udp->check = ~csum;

    inet_ntop(cur->version == 4 ? AF_INET : AF_INET6,
              cur->version == 4 ? &cur->saddr.ip4 : &cur->saddr.ip6, source, sizeof(source));
    inet_ntop(cur->version == 4 ? AF_INET : AF_INET6,
              cur->version == 4 ? &cur->daddr.ip4 : &cur->daddr.ip6, dest, sizeof(dest));

    // Send packet
    log_android(ANDROID_LOG_DEBUG,
                "UDP sending to tun %d from %s/%u to %s/%u data %u",
                args->tun, dest, ntohs(cur->dest), source, ntohs(cur->source), len);

    ssize_t res = write(args->tun, buffer, len);

    free(buffer);

    if (res != len) {
        log_android(ANDROID_LOG_ERROR, "write %d/%d", res, len);
        return -1;
    }

    return res;
}
