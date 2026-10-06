#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>

#include "auth.h"

#define AUTH_FIELD_MAX 256

#define DIGEST_ALG_MD5    0
#define DIGEST_ALG_SHA256 1

struct auth_state {
    char username[AUTH_FIELD_MAX];
    char password[AUTH_FIELD_MAX];
    int scheme;
    // Digest
    int algorithm;
    int sess;
    int qop_auth;
    char realm[AUTH_FIELD_MAX];
    char nonce[AUTH_FIELD_MAX];
    char opaque[AUTH_FIELD_MAX];
    uint32_t nc;
};

// Only used from the single native event thread
static struct auth_state auth;

/*
 * MD5 (RFC 1321)
 */

struct md5_ctx {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
};

#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static const uint32_t md5_k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const uint8_t md5_r[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static void md5_transform(struct md5_ctx *ctx, const uint8_t *block) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t) block[i * 4] | ((uint32_t) block[i * 4 + 1] << 8) |
               ((uint32_t) block[i * 4 + 2] << 16) | ((uint32_t) block[i * 4 + 3] << 24);
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + MD5_ROTL(a + f + md5_k[i] + w[g], md5_r[i]);
        a = tmp;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

static void md5_init(struct md5_ctx *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
}

static void md5_update(struct md5_ctx *ctx, const uint8_t *data, size_t len) {
    size_t off = (size_t) (ctx->count % 64);
    ctx->count += len;
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[off++] = data[i];
        if (off == 64) {
            md5_transform(ctx, ctx->buffer);
            off = 0;
        }
    }
}

static void md5_final(struct md5_ctx *ctx, uint8_t *digest) {
    uint64_t bits = ctx->count * 8;
    uint8_t pad = 0x80;
    md5_update(ctx, &pad, 1);
    pad = 0x00;
    while (ctx->count % 64 != 56) {
        md5_update(ctx, &pad, 1);
    }
    uint8_t length[8];
    for (int i = 0; i < 8; i++) {
        length[i] = (uint8_t) (bits >> (8 * i));
    }
    md5_update(ctx, length, 8);
    for (int i = 0; i < 4; i++) {
        digest[i * 4] = (uint8_t) ctx->state[i];
        digest[i * 4 + 1] = (uint8_t) (ctx->state[i] >> 8);
        digest[i * 4 + 2] = (uint8_t) (ctx->state[i] >> 16);
        digest[i * 4 + 3] = (uint8_t) (ctx->state[i] >> 24);
    }
}

/*
 * SHA-256 (FIPS 180-4)
 */

struct sha256_ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
};

#define SHA_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const uint32_t sha256_k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(struct sha256_ctx *ctx, const uint8_t *block) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t) block[i * 4] << 24) | ((uint32_t) block[i * 4 + 1] << 16) |
               ((uint32_t) block[i * 4 + 2] << 8) | (uint32_t) block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = SHA_ROTR(w[i - 15], 7) ^ SHA_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = SHA_ROTR(w[i - 2], 17) ^ SHA_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = SHA_ROTR(e, 6) ^ SHA_ROTR(e, 11) ^ SHA_ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = SHA_ROTR(a, 2) ^ SHA_ROTR(a, 13) ^ SHA_ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(struct sha256_ctx *ctx) {
    static const uint32_t init[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(ctx->state, init, sizeof(init));
    ctx->count = 0;
}

static void sha256_update(struct sha256_ctx *ctx, const uint8_t *data, size_t len) {
    size_t off = (size_t) (ctx->count % 64);
    ctx->count += len;
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[off++] = data[i];
        if (off == 64) {
            sha256_transform(ctx, ctx->buffer);
            off = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t *digest) {
    uint64_t bits = ctx->count * 8;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    pad = 0x00;
    while (ctx->count % 64 != 56) {
        sha256_update(ctx, &pad, 1);
    }
    uint8_t length[8];
    for (int i = 0; i < 8; i++) {
        length[i] = (uint8_t) (bits >> (56 - 8 * i));
    }
    sha256_update(ctx, length, 8);
    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t) (ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t) (ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t) (ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t) ctx->state[i];
    }
}

// Hex digest of the given strings joined by ':'
static void digest_hex(int algorithm, char *out, int count, ...) {
    static const char hexchars[] = "0123456789abcdef";
    uint8_t digest[32];
    size_t digest_len;
    struct md5_ctx md5;
    struct sha256_ctx sha256;

    if (algorithm == DIGEST_ALG_SHA256) {
        sha256_init(&sha256);
    } else {
        md5_init(&md5);
    }

    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count; i++) {
        const char *s = va_arg(ap, const char *);
        if (i > 0) {
            if (algorithm == DIGEST_ALG_SHA256) {
                sha256_update(&sha256, (const uint8_t *) ":", 1);
            } else {
                md5_update(&md5, (const uint8_t *) ":", 1);
            }
        }
        if (algorithm == DIGEST_ALG_SHA256) {
            sha256_update(&sha256, (const uint8_t *) s, strlen(s));
        } else {
            md5_update(&md5, (const uint8_t *) s, strlen(s));
        }
    }
    va_end(ap);

    if (algorithm == DIGEST_ALG_SHA256) {
        sha256_final(&sha256, digest);
        digest_len = 32;
    } else {
        md5_final(&md5, digest);
        digest_len = 16;
    }

    for (size_t i = 0; i < digest_len; i++) {
        out[i * 2] = hexchars[digest[i] >> 4];
        out[i * 2 + 1] = hexchars[digest[i] & 0x0f];
    }
    out[digest_len * 2] = '\0';
}

/*
 * Base64
 */

int base64_encode(const uint8_t *data, size_t len, char *out, size_t size) {
    static const char table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((len + 2) / 3) * 4;
    if (need + 1 > size) return -1;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t) data[i] << 16;
        if (i + 1 < len) v |= (uint32_t) data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out[o++] = table[(v >> 18) & 0x3f];
        out[o++] = table[(v >> 12) & 0x3f];
        out[o++] = (i + 1 < len) ? table[(v >> 6) & 0x3f] : '=';
        out[o++] = (i + 2 < len) ? table[v & 0x3f] : '=';
    }
    out[o] = '\0';
    return (int) o;
}

/*
 * Credentials
 */

void auth_init(const char *username, const char *password) {
    memset(&auth, 0, sizeof(auth));
    if (username != NULL) {
        strncpy(auth.username, username, sizeof(auth.username) - 1);
    }
    if (password != NULL) {
        strncpy(auth.password, password, sizeof(auth.password) - 1);
    }
    auth.scheme = AUTH_SCHEME_UNKNOWN;
}

int auth_enabled() {
    return auth.username[0] != '\0';
}

const char *auth_username() {
    return auth.username;
}

const char *auth_password() {
    return auth.password;
}

int auth_scheme() {
    return auth.scheme;
}

/*
 * Challenge parser
 */

struct challenge {
    int scheme;
    int algorithm;
    int sess;
    int qop_auth;
    int stale;
    int supported;
    char realm[AUTH_FIELD_MAX];
    char nonce[AUTH_FIELD_MAX];
    char opaque[AUTH_FIELD_MAX];
};

static int token_equals(const char *p, size_t len, const char *token) {
    return strlen(token) == len && strncasecmp(p, token, len) == 0;
}

// Does the comma separated list contain the token
static int list_contains(const char *list, const char *token) {
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        if (p > start && token_equals(start, (size_t) (p - start), token)) {
            return 1;
        }
    }
    return 0;
}

// Parse a single challenge: <scheme> [param=value, ...]
static void parse_challenge_line(const char *p, const char *end, struct challenge *c) {
    memset(c, 0, sizeof(*c));

    while (p < end && (*p == ' ' || *p == '\t')) p++;
    const char *scheme = p;
    while (p < end && *p != ' ' && *p != '\t') p++;
    size_t scheme_len = (size_t) (p - scheme);

    if (token_equals(scheme, scheme_len, "Basic")) {
        c->scheme = AUTH_SCHEME_BASIC;
        c->supported = 1;
    } else if (token_equals(scheme, scheme_len, "Digest")) {
        c->scheme = AUTH_SCHEME_DIGEST;
        c->algorithm = DIGEST_ALG_MD5;
        c->supported = 1;
    } else {
        return;
    }

    int has_qop = 0;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
        const char *key = p;
        while (p < end && *p != '=' && *p != ',' && *p != ' ') p++;
        size_t key_len = (size_t) (p - key);
        while (p < end && *p == ' ') p++;
        if (p >= end || *p != '=') {
            continue;
        }
        p++; // '='
        while (p < end && *p == ' ') p++;

        char value[AUTH_FIELD_MAX];
        size_t vlen = 0;
        if (p < end && *p == '"') {
            p++;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) p++;
                if (vlen < sizeof(value) - 1) value[vlen++] = *p;
                p++;
            }
            if (p < end) p++; // closing quote
        } else {
            while (p < end && *p != ',' && *p != ' ' && *p != '\t') {
                if (vlen < sizeof(value) - 1) value[vlen++] = *p;
                p++;
            }
        }
        value[vlen] = '\0';

        if (token_equals(key, key_len, "realm")) {
            strcpy(c->realm, value);
        } else if (token_equals(key, key_len, "nonce")) {
            strcpy(c->nonce, value);
        } else if (token_equals(key, key_len, "opaque")) {
            strcpy(c->opaque, value);
        } else if (token_equals(key, key_len, "stale")) {
            c->stale = (strcasecmp(value, "true") == 0);
        } else if (token_equals(key, key_len, "qop")) {
            has_qop = 1;
            c->qop_auth = list_contains(value, "auth");
        } else if (token_equals(key, key_len, "algorithm")) {
            if (strcasecmp(value, "MD5") == 0) {
                c->algorithm = DIGEST_ALG_MD5;
            } else if (strcasecmp(value, "MD5-sess") == 0) {
                c->algorithm = DIGEST_ALG_MD5;
                c->sess = 1;
            } else if (strcasecmp(value, "SHA-256") == 0) {
                c->algorithm = DIGEST_ALG_SHA256;
            } else if (strcasecmp(value, "SHA-256-sess") == 0) {
                c->algorithm = DIGEST_ALG_SHA256;
                c->sess = 1;
            } else {
                c->supported = 0;
            }
        }
    }

    if (c->scheme == AUTH_SCHEME_DIGEST) {
        // Only qop=auth (or the legacy RFC 2069 mode without qop) is supported
        if (c->nonce[0] == '\0' || (has_qop && !c->qop_auth)) {
            c->supported = 0;
        }
    }
}

// Preference: Digest SHA-256 > Digest MD5 > Basic
static int challenge_rank(const struct challenge *c) {
    if (!c->supported) return 0;
    if (c->scheme == AUTH_SCHEME_BASIC) return 1;
    return c->algorithm == DIGEST_ALG_SHA256 ? 3 : 2;
}

int auth_parse_challenge(const char *response, size_t len, int *stale) {
    static const char header[] = "Proxy-Authenticate:";
    const size_t header_len = sizeof(header) - 1;
    struct challenge best;
    struct challenge cur;
    memset(&best, 0, sizeof(best));

    if (stale != NULL) *stale = 0;

    const char *p = response;
    const char *end = response + len;
    while (p < end) {
        const char *line_end = p;
        while (line_end < end && *line_end != '\r' && *line_end != '\n') line_end++;
        if (line_end == p && p != response) {
            break; // end of headers
        }
        if ((size_t) (line_end - p) > header_len && strncasecmp(p, header, header_len) == 0) {
            parse_challenge_line(p + header_len, line_end, &cur);
            if (challenge_rank(&cur) > challenge_rank(&best)) {
                best = cur;
            }
        }
        p = line_end;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    if (!best.supported) {
        return 0;
    }

    auth.scheme = best.scheme;
    if (best.scheme == AUTH_SCHEME_DIGEST) {
        if (strcmp(auth.nonce, best.nonce) != 0) {
            auth.nc = 0;
        }
        auth.algorithm = best.algorithm;
        auth.sess = best.sess;
        auth.qop_auth = best.qop_auth;
        strcpy(auth.realm, best.realm);
        strcpy(auth.nonce, best.nonce);
        strcpy(auth.opaque, best.opaque);
        if (stale != NULL) *stale = best.stale;
    }
    return 1;
}

/*
 * Authorization header
 */

// Copy a quoted-string value, escaping '"' and '\'
static void quote_value(const char *in, char *out, size_t size) {
    size_t o = 0;
    for (const char *p = in; *p && o + 2 < size; p++) {
        if (*p == '"' || *p == '\\') out[o++] = '\\';
        out[o++] = *p;
    }
    out[o] = '\0';
}

int auth_build_header(const char *method, const char *uri, char *out, size_t size) {
    if (!auth_enabled()) return 0;

    int len;
    if (auth.scheme == AUTH_SCHEME_BASIC) {
        char credentials[AUTH_FIELD_MAX * 2 + 2];
        char encoded[sizeof(credentials) * 4 / 3 + 4];
        snprintf(credentials, sizeof(credentials), "%s:%s", auth.username, auth.password);
        if (base64_encode((const uint8_t *) credentials, strlen(credentials),
                          encoded, sizeof(encoded)) < 0)
            return 0;
        len = snprintf(out, size, "Proxy-Authorization: Basic %s\r\n", encoded);
    } else if (auth.scheme == AUTH_SCHEME_DIGEST) {
        char ha1[65], ha2[65], response[65];
        char cnonce[17];
        char nc[9];
        const char *algorithm;

        uint8_t rnd[8];
        arc4random_buf(rnd, sizeof(rnd));
        for (int i = 0; i < 8; i++) {
            snprintf(cnonce + i * 2, 3, "%02x", rnd[i]);
        }
        auth.nc++;
        snprintf(nc, sizeof(nc), "%08x", auth.nc);

        digest_hex(auth.algorithm, ha1, 3, auth.username, auth.realm, auth.password);
        if (auth.sess) {
            char tmp[65];
            strcpy(tmp, ha1);
            digest_hex(auth.algorithm, ha1, 3, tmp, auth.nonce, cnonce);
        }
        digest_hex(auth.algorithm, ha2, 2, method, uri);
        if (auth.qop_auth) {
            digest_hex(auth.algorithm, response, 6, ha1, auth.nonce, nc, cnonce, "auth", ha2);
        } else {
            digest_hex(auth.algorithm, response, 3, ha1, auth.nonce, ha2);
        }

        if (auth.algorithm == DIGEST_ALG_SHA256) {
            algorithm = auth.sess ? "SHA-256-sess" : "SHA-256";
        } else {
            algorithm = auth.sess ? "MD5-sess" : "MD5";
        }

        char username[AUTH_FIELD_MAX * 2];
        char realm[AUTH_FIELD_MAX * 2];
        quote_value(auth.username, username, sizeof(username));
        quote_value(auth.realm, realm, sizeof(realm));

        len = snprintf(out, size,
                       "Proxy-Authorization: Digest username=\"%s\", realm=\"%s\", "
                       "nonce=\"%s\", uri=\"%s\", algorithm=%s, response=\"%s\"",
                       username, realm, auth.nonce, uri, algorithm, response);
        if (len > 0 && (size_t) len < size && auth.qop_auth) {
            len += snprintf(out + len, size - len, ", qop=auth, nc=%s, cnonce=\"%s\"",
                            nc, cnonce);
        }
        if (len > 0 && (size_t) len < size && auth.opaque[0] != '\0') {
            len += snprintf(out + len, size - len, ", opaque=\"%s\"", auth.opaque);
        }
        if (len > 0 && (size_t) len < size) {
            len += snprintf(out + len, size - len, "\r\n");
        }
    } else {
        return 0;
    }

    if (len <= 0 || (size_t) len >= size) {
        out[0] = '\0';
        return 0;
    }
    return len;
}

int auth_build_socks5_request(uint8_t *buf, size_t size) {
    size_t ulen = strlen(auth.username);
    size_t plen = strlen(auth.password);
    if (ulen == 0 || ulen > 255 || plen > 255 || 3 + ulen + plen > size) return -1;

    int len = 0;
    buf[len++] = 0x01; // VER
    buf[len++] = (uint8_t) ulen;
    memcpy(buf + len, auth.username, ulen);
    len += (int) ulen;
    buf[len++] = (uint8_t) plen;
    memcpy(buf + len, auth.password, plen);
    len += (int) plen;
    return len;
}
