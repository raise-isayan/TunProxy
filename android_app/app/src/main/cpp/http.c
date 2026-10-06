
#include <stdio.h>
#include <stdlib.h> /* malloc() */
#include <string.h> /* strncpy() */
#include <strings.h> /* strncasecmp() */
#include <ctype.h> /* isblank() */

#include <android/log.h>

#define LOG_TAG "Tun2Http_HTTP"
#define LOG(v) {__android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, v);}


static const char http_503[] =
        "HTTP/1.1 503 Service Temporarily Unavailable\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n\r\n"
        "Backend not available";


/*
 * Parses a HTTP request for the Host: header
 *
 * Returns:
 *  >=0  - length of the hostname and updates *hostname
 *         caller is responsible for freeing *hostname
 *  -1   - Incomplete request
 *  -2   - No Host header included in this request
 *  -3   - Invalid hostname pointer
 *  -4   - malloc failure
 *  < -4 - Invalid HTTP request
 *
 */

#include "tun2http.h"
#include "auth.h"

int get_header(const char *header, const char *data, size_t data_len, char *value) {
    int len, header_len;

    header_len = strlen(header);

    /* loop through headers stopping at first blank line */
    while ((len = next_header(&data, &data_len)) != 0)
        if (len > header_len && strncasecmp(header, data, header_len) == 0) {
            /* Eat leading whitespace */
            while (header_len < len && isblank(data[header_len]))
                header_len++;

            if (value == NULL)
                return -4;

            strncpy(value, data + header_len, len - header_len);
            value[len - header_len] = '\0';

            return len - header_len;
        }

    /* If there is no data left after reading all the headers then we do not
     * have a complete HTTP request, there must be a blank line */
    if (data_len == 0)
        return -1;

    return -2;
}

int next_header(const char **data, size_t *len) {
    int header_len;

    /* perhaps we can optimize this to reuse the value of header_len, rather
     * than scanning twice.
     * Walk our data stream until the end of the header */
    while (*len > 2 && (*data)[0] != '\r' && (*data)[1] != '\n') {
        (*len)--;
        (*data)++;
    }

    /* advanced past the <CR><LF> pair */
    *data += 2;
    *len -= 2;

    /* Find the length of the next header */
    header_len = 0;
    while (*len > header_len + 1
           && (*data)[header_len] != '\r'
           && (*data)[header_len + 1] != '\n') {
        header_len++;
    }

    return header_len;
}

uint8_t *find_data(uint8_t *data, size_t data_len, char *value) {

    int found = 0;
    int value_length = strlen(value);

    while (!found && data_len > 2) {
        while (data[0] != value[0] && data_len > 2) {
            data++;
            data_len--;
        }
        if (strncasecmp(value, data, value_length) == 0) {
            found = 1;
        } else {
            data++;
            data_len--;
        }
    }
    if (found) {
        return data;
    }

    return 0;
}

uint8_t patch_buffer[2 * MTU];

/*
 * Rewrites a HTTP request for the proxy:
 *  - origin-form request target to absolute-form (http://host/path)
 *  - adds Proxy-Authorization when proxy authentication is configured
 *
 * Returns the patched data (data_len updated) or 0 when unchanged
 */
uint8_t *patch_http_url(uint8_t *data, size_t *data_len) {
    __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, "patch_http_url start");

    size_t word_len = 0;
    while (word_len < *data_len && data[word_len] != ' ' && data[word_len] != '\r' &&
           data[word_len] != '\n') {
        word_len++;
    }

    if (word_len == 0 || word_len >= *data_len || data[word_len] != ' ') {
        LOG("patch_http_url no method space");
        return 0;
    }

    if (word_len == strlen("CONNECT") && memcmp(data, "CONNECT", 7) == 0) {
        LOG("patch_http_url skip CONNECT");
        return 0;
    }

    size_t pos1 = word_len + 1;

    // End of the request target and of the request line
    size_t pos2 = pos1;
    while (pos2 < *data_len && data[pos2] != ' ' && data[pos2] != '\r' && data[pos2] != '\n') {
        pos2++;
    }
    size_t line_end = pos2;
    while (line_end + 1 < *data_len && !(data[line_end] == '\r' && data[line_end + 1] == '\n')) {
        line_end++;
    }
    if (pos2 >= *data_len || data[pos2] != ' ' || line_end + 1 >= *data_len) {
        LOG("patch_http_url incomplete request line");
        return 0;
    }
    line_end += 2; // CRLF

    LOG("patch_http_url word found");

    size_t http_len = strlen("http://");
    int absolute = (pos2 - pos1 >= http_len && strncasecmp((char *) data + pos1, "http://", http_len) == 0);

    char hostname[512];
    size_t length = 0;
    if (!absolute) {
        uint8_t *host = find_data(data, *data_len, "Host: ");
        if (host) {
            host += 6;
            while (host < data + *data_len && *host != '\r' && length < sizeof(hostname) - 1) {
                hostname[length] = *host;
                host++;
                length++;
            }
        } else {
            LOG("patch_http_url no host");
            return 0;
        }
    }
    hostname[length] = '\0';

    // Proxy-Authorization for the absolute request target
    char authorization[2048];
    int auth_len = 0;
    if (auth_enabled() && word_len < 32 &&
        find_data(data, *data_len, "Proxy-Authorization:") == 0) {
        char method[32];
        char uri[4096];
        size_t target_len = pos2 - pos1;
        memcpy(method, data, word_len);
        method[word_len] = '\0';
        if ((absolute ? 0 : http_len + length) + target_len < sizeof(uri)) {
            size_t u = 0;
            if (!absolute) {
                memcpy(uri, "http://", http_len);
                memcpy(uri + http_len, hostname, length);
                u = http_len + length;
            }
            memcpy(uri + u, data + pos1, target_len);
            uri[u + target_len] = '\0';
            auth_len = auth_build_header(method, uri, authorization, sizeof(authorization));
            if (auth_len < 0) auth_len = 0;
        }
    }

    if (absolute && auth_len == 0) {
        LOG("patch_http_url already patched");
        return 0;
    }

    size_t prefix_len = absolute ? 0 : http_len + length;
    if (*data_len + prefix_len + auth_len > sizeof(patch_buffer)) {
        LOG("patch_http_url too large");
        return 0;
    }

    uint8_t *new_data = &patch_buffer[0];
    LOG("patch_http_url start patch");

    // METHOD SP [http://host]target SP version CRLF [Proxy-Authorization] headers...
    size_t o = 0;
    memcpy(new_data, data, pos1);
    o += pos1;
    if (!absolute) {
        memcpy(new_data + o, "http://", http_len);
        o += http_len;
        memcpy(new_data + o, hostname, length);
        o += length;
    }
    memcpy(new_data + o, data + pos1, line_end - pos1);
    o += line_end - pos1;
    memcpy(new_data + o, authorization, (size_t) auth_len);
    o += auth_len;
    memcpy(new_data + o, data + line_end, *data_len - line_end);
    o += *data_len - line_end;

    *data_len = o;

    LOG("patch_http_url end patch");

    return new_data;
};
