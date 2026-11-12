#include "pin.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>

static void hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
}

int main(void)
{
    uint8_t d[64];
    char h[200];
    /* SHA-256 (FIPS 180-4 examples) */
    sha256((const uint8_t *)"abc", 3, d);
    hex(d, 32, h);
    CHECK_STR(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256((const uint8_t *)"", 0, d);
    hex(d, 32, h);
    CHECK_STR(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256((const uint8_t *)two, strlen(two), d);
    hex(d, 32, h);
    CHECK_STR(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    /* HMAC-SHA-256 (RFC 4231 test case 2) */
    hmac_sha256((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, d);
    hex(d, 32, h);
    CHECK_STR(h, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    /* PBKDF2-HMAC-SHA-256 */
    pbkdf2_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 1, d, 32);
    hex(d, 32, h);
    CHECK_STR(h, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    pbkdf2_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 2, d, 32);
    hex(d, 32, h);
    CHECK_STR(h, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    pbkdf2_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 4096, d, 32);
    hex(d, 32, h);
    CHECK_STR(h, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");

    /* the stored form: salted and iterated, the PIN is not in it */
    uint8_t salt1[PIN_SALT_BYTES], salt2[PIN_SALT_BYTES];
    for (int i = 0; i < PIN_SALT_BYTES; i++) { salt1[i] = (uint8_t)i; salt2[i] = (uint8_t)(i + 1); }
    char s1[33], h1[65], s2[33], h2[65], s3[33], h3[65];
    pin_make("1234", salt1, s1, h1);
    pin_make("1234", salt2, s2, h2);
    pin_make("1235", salt1, s3, h3);
    CHECK(strlen(s1) == 32 && strlen(h1) == 64);
    CHECK(strcmp(h1, h2) != 0);                       /* the same PIN with another salt: another hash */
    CHECK(strcmp(h1, h3) != 0);
    CHECK(!strstr(s1, "1234") && !strstr(h1, "1234"));
    CHECK(PIN_ITERATIONS >= 20000);                   /* "a few tens of thousands" */
    CHECK(pin_verify("1234", s1, h1));
    CHECK(!pin_verify("1235", s1, h1) && !pin_verify("", s1, h1) && !pin_verify("12345", s1, h1));
    CHECK(!pin_verify("1234", s2, h1));               /* a different salt */
    CHECK(!pin_verify("1234", "zz", h1) && !pin_verify("1234", s1, "short") && !pin_verify(NULL, s1, h1));
    CHECK(pin_verify("a long passphrase with spaces and \xC3\xA9", s1, (pin_make("a long passphrase with spaces and \xC3\xA9", salt1, s3, h3), h3)));
    /* the hash really is the iterated PBKDF2 of the PIN with that salt */
    uint8_t chk[32];
    pbkdf2_sha256((const uint8_t *)"1234", 4, salt1, PIN_SALT_BYTES, PIN_ITERATIONS, chk, 32);
    hex(chk, 32, h);
    CHECK_STR(h, h1);

    /* relock timer with a fake clock */
    CHECK(!nsfw_should_relock(100, 0, 0));            /* 0 = never */
    CHECK(!nsfw_should_relock(599.9, 0, 10));
    CHECK(nsfw_should_relock(600, 0, 10));
    CHECK(nsfw_should_relock(10000, 9000, 10));
    CHECK(!nsfw_should_relock(10000, 9900, 10));      /* activity resets it */
    TEST_DONE();
}
