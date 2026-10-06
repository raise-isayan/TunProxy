#ifndef TUN2HTTP_AUTH_H
#define TUN2HTTP_AUTH_H

#include <stddef.h>
#include <stdint.h>

#define AUTH_SCHEME_UNKNOWN 0
#define AUTH_SCHEME_BASIC   1
#define AUTH_SCHEME_DIGEST  2

// Set the proxy credentials, empty username disables authentication
void auth_init(const char *username, const char *password);

int auth_enabled();

const char *auth_username();

const char *auth_password();

int auth_scheme();

// Parse the Proxy-Authenticate headers of a proxy response (or a raw header block)
// Returns 1 when a supported challenge was found, 0 otherwise
// *stale is set to 1 when the digest nonce was only expired
int auth_parse_challenge(const char *response, size_t len, int *stale);

// Build "Proxy-Authorization: ...\r\n" for the request, returns the length or 0
int auth_build_header(const char *method, const char *uri, char *out, size_t size);

// RFC 1929 username/password request, returns the length or -1
int auth_build_socks5_request(uint8_t *buf, size_t size);

int base64_encode(const uint8_t *data, size_t len, char *out, size_t size);

#endif //TUN2HTTP_AUTH_H
