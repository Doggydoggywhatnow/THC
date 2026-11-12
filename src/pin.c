#include "pin.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

typedef struct Sha { uint32_t h[8]; uint8_t buf[64]; size_t n; uint64_t total; } Sha;

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(Sha *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(Sha *s)
{
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, iv, sizeof iv);
    s->n = 0;
    s->total = 0;
}

static void sha_update(Sha *s, const uint8_t *d, size_t len)
{
    s->total += len;
    while (len) {
        size_t take = 64 - s->n;
        if (take > len) take = len;
        memcpy(s->buf + s->n, d, take);
        s->n += take; d += take; len -= take;
        if (s->n == 64) { block(s, s->buf); s->n = 0; }
    }
}

static void sha_final(Sha *s, uint8_t out[32])
{
    uint64_t bits = s->total * 8;
    uint8_t pad = 0x80;
    sha_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->n != 56) sha_update(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_update(s, len, 8);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

void sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    Sha s;
    sha_init(&s);
    sha_update(&s, data, len);
    sha_final(&s, out);
}

void hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *data, size_t len, uint8_t out[32])
{
    uint8_t k[64] = { 0 }, ipad[64], opad[64], inner[32];
    if (keylen > 64) sha256(key, keylen, k);
    else if (keylen) memcpy(k, key, keylen);
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha s;
    sha_init(&s); sha_update(&s, ipad, 64); sha_update(&s, data, len); sha_final(&s, inner);
    sha_init(&s); sha_update(&s, opad, 64); sha_update(&s, inner, 32); sha_final(&s, out);
}

void pbkdf2_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen, uint32_t iterations, uint8_t *out, size_t outlen)
{
    uint8_t saltblock[256];
    if (saltlen > sizeof saltblock - 4) saltlen = sizeof saltblock - 4;
    memcpy(saltblock, salt, saltlen);
    for (uint32_t blk = 1; outlen; blk++) {
        saltblock[saltlen] = (uint8_t)(blk >> 24);
        saltblock[saltlen + 1] = (uint8_t)(blk >> 16);
        saltblock[saltlen + 2] = (uint8_t)(blk >> 8);
        saltblock[saltlen + 3] = (uint8_t)blk;
        uint8_t u[32], t[32];
        hmac_sha256(pw, pwlen, saltblock, saltlen + 4, u);
        memcpy(t, u, 32);
        for (uint32_t i = 1; i < iterations; i++) {
            hmac_sha256(pw, pwlen, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        size_t take = outlen < 32 ? outlen : 32;
        memcpy(out, t, take);
        out += take;
        outlen -= take;
    }
}

static void to_hex(const uint8_t *b, size_t n, char *out)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i * 2] = hx[b[i] >> 4]; out[i * 2 + 1] = hx[b[i] & 15]; }
    out[n * 2] = 0;
}

static bool from_hex(const char *s, uint8_t *out, size_t n)
{
    if (strlen(s) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[i * 2 + (size_t)k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + d;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

void pin_make(const char *pin, const uint8_t salt[PIN_SALT_BYTES], char salt_hex[33], char hash_hex[65])
{
    uint8_t h[32];
    pbkdf2_sha256((const uint8_t *)pin, strlen(pin), salt, PIN_SALT_BYTES, PIN_ITERATIONS, h, 32);
    to_hex(salt, PIN_SALT_BYTES, salt_hex);
    to_hex(h, 32, hash_hex);
}

bool pin_verify(const char *pin, const char *salt_hex, const char *hash_hex)
{
    uint8_t salt[PIN_SALT_BYTES], want[32], got[32];
    if (!pin || !from_hex(salt_hex, salt, PIN_SALT_BYTES) || !from_hex(hash_hex, want, 32)) return false;
    pbkdf2_sha256((const uint8_t *)pin, strlen(pin), salt, PIN_SALT_BYTES, PIN_ITERATIONS, got, 32);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(got[i] ^ want[i]);
    return diff == 0;
}

bool nsfw_should_relock(double now, double last_activity, int minutes)
{
    return minutes > 0 && now - last_activity >= (double)minutes * 60.0;
}
