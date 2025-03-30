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
 *  -1   - Incomplete request
 *  -2   - No Host header included in this request
 *  -3   - Invalid hostname pointer
 *  -4   - malloc failure
 *  < -4 - Invalid HTTP request
 *
 */

#include "tun2http.h"

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
           && (*data)[header_len + 1] != '\n')
        header_len++;

    return header_len;
}

uint8_t *find_data(uint8_t *data, size_t data_len, const char *value) {
    if (!data || !value || data_len == 0)
        return NULL;

    int found = 0;
    int value_length = strlen(value);
    
    // Make sure value is not longer than data
    if (value_length > data_len)
        return NULL;

    size_t pos = 0;
    while (!found && pos + value_length <= data_len) {
        if (strncasecmp((char *)&data[pos], value, value_length) == 0) {
            found = 1;
            return &data[pos];
        }
        pos++;
    }

    return NULL;
}

// Static buffer for HTTP patching with safe size (avoid global array overflows)
// Using 2*MTU ensures we have enough space for the original request plus any additions
static uint8_t patch_buffer[2*MTU];

uint8_t *patch_http_url(uint8_t *data, size_t *data_len) {
    if (!data || !data_len || *data_len == 0 || *data_len > MTU) {
        LOG("patch_http_url: Invalid input data");
        return NULL;
    }

    __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, "patch_http_url start (data length: %zu)", *data_len);

    // Extract hostname from Host header
    char hostname[512] = {0};
    uint8_t *host_header = find_data(data, *data_len, "Host: ");
    if (!host_header) {
        LOG("patch_http_url: No Host header found");
        return NULL;
    }
    
    // Extract hostname from header (skip "Host: " and stop at CR or space)
    host_header += 6; // Skip "Host: "
    size_t hostname_len = 0;
    size_t max_offset = *data_len - (host_header - data);
    
    while (hostname_len < sizeof(hostname) - 1 && 
           hostname_len < max_offset && 
           host_header[hostname_len] != '\r' && 
           host_header[hostname_len] != '\n' &&
           host_header[hostname_len] != ' ') {
        hostname[hostname_len] = host_header[hostname_len];
        hostname_len++;
    }
    hostname[hostname_len] = '\0';
    
    if (hostname_len == 0) {
        LOG("patch_http_url: Empty hostname");
        return NULL;
    }
    
    __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, "patch_http_url: Found hostname: %s", hostname);

    // Find HTTP method
    const char *http_methods[] = {
        "GET ", "POST ", "PUT ", "DELETE ", "HEAD ", "OPTIONS ", 
        "PATCH ", "TRACE ", "CONNECT ", "PROPFIND ", "PROPPATCH ", 
        "MKCOL ", "COPY ", "MOVE ", "LOCK ", "UNLOCK ", "LINK ", "UNLINK "
    };
    
    uint8_t *method_pos = NULL;
    const char *method_str = NULL;
    size_t method_len = 0;
    
    for (size_t i = 0; i < sizeof(http_methods) / sizeof(http_methods[0]); i++) {
        uint8_t *pos = find_data(data, *data_len, http_methods[i]);
        if (pos) {
            method_pos = pos;
            method_str = http_methods[i];
            method_len = strlen(method_str);
            break;
        }
    }
    
    if (!method_pos) {
        LOG("patch_http_url: No HTTP method found");
        return NULL;
    }
    
    __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, "patch_http_url: Found method: %s", method_str);
    
    // Find the URL part (after the method)
    size_t url_offset = method_pos - data + method_len;
    if (url_offset >= *data_len) {
        LOG("patch_http_url: URL parsing error");
        return NULL;
    }
    
    // Check if URL already starts with http://
    if (*data_len > url_offset + 7 && 
        strncasecmp((char*)&data[url_offset], "http://", 7) == 0) {
        LOG("patch_http_url: URL already has http://");
        return NULL;
    }
    
    // Check if URL already starts with https://
    if (*data_len > url_offset + 8 && 
        strncasecmp((char*)&data[url_offset], "https://", 8) == 0) {
        LOG("patch_http_url: URL already has https://");
        return NULL;
    }
    
    // Check if URL is a relative path (starts with / or ./)
    bool is_relative = (*data_len > url_offset && 
                       (data[url_offset] == '/' || 
                        (data[url_offset] == '.' && *data_len > url_offset + 1 && data[url_offset + 1] == '/')));
    
    // Calculate new data length and ensure it fits in our buffer
    size_t http_prefix_len = 7; // "http://"
    size_t new_data_len = 0;
    
    if (is_relative) {
        // For relative URLs, we need: [beginning][http://hostname][relative_path][rest]
        new_data_len = url_offset + http_prefix_len + hostname_len + (*data_len - url_offset);
    } else {
        // For absolute URLs, we need: [beginning][http://hostname/][absolute_path][rest]
        new_data_len = url_offset + http_prefix_len + hostname_len + 1 + (*data_len - url_offset);
    }
    
    if (new_data_len > sizeof(patch_buffer)) {
        LOG("patch_http_url: New data too large for buffer");
        return NULL;
    }
    
    // Create the patched request
    uint8_t *new_data = patch_buffer;
    
    // Copy everything up to the URL
    memcpy(new_data, data, url_offset);
    
    // Add http:// and hostname
    memcpy(new_data + url_offset, "http://", http_prefix_len);
    memcpy(new_data + url_offset + http_prefix_len, hostname, hostname_len);
    
    // Handle relative vs absolute URL
    if (is_relative) {
        // For relative URLs, just append the URL as-is
        memcpy(new_data + url_offset + http_prefix_len + hostname_len, 
               data + url_offset, *data_len - url_offset);
    } else {
        // For absolute URLs, add a slash after hostname
        new_data[url_offset + http_prefix_len + hostname_len] = '/';
        
        // Then add the absolute URL
        memcpy(new_data + url_offset + http_prefix_len + hostname_len + 1, 
               data + url_offset, *data_len - url_offset);
    }
    
    // Update data length
    *data_len = new_data_len;
    
    __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, "patch_http_url: Successfully patched URL");
    return new_data;
};
