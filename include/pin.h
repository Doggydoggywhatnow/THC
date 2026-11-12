/* pin.h: SHA-256, HMAC-SHA-256 and PBKDF2 for the NSFW lock's PIN (#13). Public domain (the Unlicense), written for
 * THC. The PIN is never stored: THC keeps a random salt and the PBKDF2 hash. This is a courtesy lock, not security:
 * media files and THC's data files are not encrypted. */
#ifndef THC_PIN_H
#define THC_PIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIN_ITERATIONS 50000
#define PIN_SALT_BYTES 16

void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *data, size_t len, uint8_t out[32]);
void pbkdf2_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen, uint32_t iterations, uint8_t *out, size_t outlen);

/* Make the stored form of a PIN: `salt` (16 random bytes from the caller) as 32 hex digits and the hash as 64. */
void pin_make(const char *pin, const uint8_t salt[PIN_SALT_BYTES], char salt_hex[33], char hash_hex[65]);
/* Check a PIN against the stored form. Takes the same time whether or not the PIN is right. */
bool pin_verify(const char *pin, const char *salt_hex, const char *hash_hex);

/* Auto-relock: has the portal been idle for `minutes` (0 = never)? Times in seconds. */
bool nsfw_should_relock(double now, double last_activity, int minutes);

#endif
