/* test_tags.c: the tag readers, on small files built in the test itself. */
#include "tags.h"
#include "test_util.h"

#include <stdint.h>
#include <stdlib.h>

#define JPEG "\xFF\xD8\xFF\xE0JFIFfakejpegdata"
#define JPEG_LEN 18

typedef struct Buf { unsigned char *d; size_t n, cap; } Buf;

static void put(Buf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->d = (unsigned char *)realloc(b->d, b->cap);
    }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}
static void put_s(Buf *b, const char *s) { put(b, s, strlen(s)); }
static void u8(Buf *b, unsigned v) { unsigned char c = (unsigned char)v; put(b, &c, 1); }
static void be16(Buf *b, unsigned v) { u8(b, v >> 8); u8(b, v); }
static void be24(Buf *b, unsigned v) { u8(b, v >> 16); u8(b, v >> 8); u8(b, v); }
static void be32w(Buf *b, uint32_t v) { u8(b, v >> 24); u8(b, v >> 16); u8(b, v >> 8); u8(b, v); }
static void le32w(Buf *b, uint32_t v) { u8(b, v); u8(b, v >> 8); u8(b, v >> 16); u8(b, v >> 24); }
static void ss32(Buf *b, uint32_t v) { u8(b, (v >> 21) & 0x7F); u8(b, (v >> 14) & 0x7F); u8(b, (v >> 7) & 0x7F); u8(b, v & 0x7F); }

static const char *tmp(const char *name)
{
    static char path[4][256];
    static int i;
    char *p = path[i++ & 3];
    snprintf(p, 256, "tags_test_%s", name);
    return p;
}

static const char *save(const char *name, const Buf *b)
{
    const char *p = tmp(name);
    FILE *f = fopen(p, "wb");
    if (f) {
        fwrite(b->d, 1, b->n, f);
        fclose(f);
    }
    return p;
}

static uint32_t checksum(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    uint32_t h = 2166136261u;
    long n = 0;
    int c;
    while (f && (c = fgetc(f)) != EOF) { h = (h ^ (unsigned)c) * 16777619u; n++; }
    if (f) fclose(f);
    *size = n;
    return h;
}

/* ------------------------------------------------------------------ ID3 */

static void id3_text(Buf *f, const char *id, int enc, const void *txt, size_t n, int ver)
{
    size_t size = n + 1;
    put_s(f, id);
    if (ver == 4) ss32(f, (uint32_t)size); else be32w(f, (uint32_t)size);
    be16(f, 0);
    u8(f, enc);
    put(f, txt, n);
}

static void test_id3v23(void)
{
    Buf body = { 0 };
    /* "Zażółć" in UTF-16 with a BOM */
    const unsigned char title16[] = { 0xFF, 0xFE, 'Z', 0, 'a', 0, 0x7C, 0x01, 0xF3, 0, 0x42, 0x01, 0x07, 0x01 };
    id3_text(&body, "TIT2", 1, title16, sizeof title16, 3);
    id3_text(&body, "TPE1", 0, "Caf\xE9 Band", 9, 3);          /* Latin-1 */
    id3_text(&body, "TALB", 3, "Album \xC3\xA9", 8, 3);          /* UTF-8 */
    id3_text(&body, "TPE2", 0, "Various", 7, 3);
    id3_text(&body, "TRCK", 0, "3/12", 4, 3);
    id3_text(&body, "TPOS", 0, "2/3", 3, 3);
    id3_text(&body, "TYER", 0, "1999", 4, 3);
    id3_text(&body, "TCON", 0, "Rock", 4, 3);
    /* APIC: enc, mime\0, type 3, desc\0, data */
    Buf apic = { 0 };
    u8(&apic, 0); put_s(&apic, "image/jpeg"); u8(&apic, 0); u8(&apic, 3); u8(&apic, 0); put(&apic, JPEG, JPEG_LEN);
    put_s(&body, "APIC"); be32w(&body, (uint32_t)apic.n); be16(&body, 0); put(&body, apic.d, apic.n);
    for (int i = 0; i < 20; i++) u8(&body, 0); /* padding */

    Buf f = { 0 };
    put_s(&f, "ID3"); u8(&f, 3); u8(&f, 0); u8(&f, 0); ss32(&f, (uint32_t)body.n);
    put(&f, body.d, body.n);
    for (int i = 0; i < 64; i++) u8(&f, 0xFF - (i & 1)); /* fake audio */
    const char *p = save("v23.mp3", &f);

    TagInfo t;
    CHECK(tags_read(p, &t));
    CHECK_STR(t.title, "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
    CHECK_STR(t.artist, "Caf\xC3\xA9 Band");
    CHECK_STR(t.album, "Album \xC3\xA9");
    CHECK_STR(t.album_artist, "Various");
    CHECK_STR(t.genre, "Rock");
    CHECK(t.track == 3 && t.track_total == 12);
    CHECK(t.disc == 2 && t.disc_total == 3);
    CHECK(t.year == 1999);
    CHECK(t.has_art);
    CHECK_STR(t.art_mime, "image/jpeg");
    size_t n = 0;
    char mime[32];
    unsigned char *art = tags_read_art(p, &n, mime, sizeof mime);
    CHECK(art && n == JPEG_LEN && !memcmp(art, JPEG, JPEG_LEN));
    CHECK_STR(mime, "image/jpeg");
    free(art);
    free(body.d); free(apic.d); free(f.d);
}

static void test_id3v24_and_v1(void)
{
    Buf body = { 0 };
    id3_text(&body, "TIT2", 3, "Song \xE2\x80\x94 Live", 13, 4);
    id3_text(&body, "TRCK", 3, "07", 2, 4);
    id3_text(&body, "TDRC", 3, "2021-05-01", 10, 4);
    Buf f = { 0 };
    put_s(&f, "ID3"); u8(&f, 4); u8(&f, 0); u8(&f, 0); ss32(&f, (uint32_t)body.n);
    put(&f, body.d, body.n);
    for (int i = 0; i < 32; i++) u8(&f, 0xFF);
    /* ID3v1 at the end fills in what v2 lacks */
    unsigned char v1[128] = { 0 };
    memcpy(v1, "TAG", 3);
    memcpy(v1 + 3, "Ignored Title", 13);
    memcpy(v1 + 33, "Old Artist", 10);
    put(&f, v1, 128);
    const char *p = save("v24.mp3", &f);
    TagInfo t;
    CHECK(tags_read(p, &t));
    CHECK_STR(t.title, "Song \xE2\x80\x94 Live");   /* v2 wins over v1 */
    CHECK_STR(t.artist, "Old Artist");               /* v1 fills the gap */
    CHECK(t.track == 7);
    CHECK(t.year == 2021);
    CHECK(!t.has_art);
    free(body.d); free(f.d);

    /* ID3v1 only, with a track number */
    Buf g = { 0 };
    for (int i = 0; i < 40; i++) u8(&g, 0xFF);
    unsigned char v1b[128] = { 0 };
    memcpy(v1b, "TAG", 3); memcpy(v1b + 3, "Only V1", 7); memcpy(v1b + 93, "1987", 4); v1b[125] = 0; v1b[126] = 9;
    put(&g, v1b, 128);
    p = save("v1.mp3", &g);
    CHECK(tags_read(p, &t));
    CHECK_STR(t.title, "Only V1");
    CHECK(t.year == 1987 && t.track == 9);
    free(g.d);
}

/* ------------------------------------------------------------------ FLAC */

static void vorbis_comment(Buf *b, const char *const *kv, int n, const char *extra_key, const char *extra_val)
{
    le32w(b, 5); put_s(b, "THC v");
    le32w(b, (uint32_t)(n + (extra_key ? 1 : 0)));
    for (int i = 0; i < n; i++) { le32w(b, (uint32_t)strlen(kv[i])); put_s(b, kv[i]); }
    if (extra_key) {
        le32w(b, (uint32_t)(strlen(extra_key) + 1 + strlen(extra_val)));
        put_s(b, extra_key); put_s(b, "="); put_s(b, extra_val);
    }
}

static void picture_block(Buf *b, int type, const char *mime, const void *data, size_t n)
{
    be32w(b, (uint32_t)type);
    be32w(b, (uint32_t)strlen(mime)); put_s(b, mime);
    be32w(b, 0);                       /* description */
    be32w(b, 16); be32w(b, 16); be32w(b, 24); be32w(b, 0);
    be32w(b, (uint32_t)n); put(b, data, n);
}

static void test_flac(void)
{
    const char *kv[] = { "title=Mixed Case Keys", "ARTIST=\xC3\x85sa", "Album=Sk\xC3\xA5ne", "TRACKNUMBER=4/10",
                         "DISCNUMBER=1", "DATE=2003-02-01", "ALBUMARTIST=Group" };
    Buf vc = { 0 }, pic1 = { 0 }, pic2 = { 0 };
    vorbis_comment(&vc, kv, 7, NULL, NULL);
    picture_block(&pic1, 4, "image/png", "\x89PNGback", 8);        /* back cover */
    picture_block(&pic2, 3, "image/jpeg", JPEG, JPEG_LEN);        /* front cover wins */
    Buf f = { 0 };
    put_s(&f, "fLaC");
    u8(&f, 0); be24(&f, 34); for (int i = 0; i < 34; i++) u8(&f, 0);   /* STREAMINFO */
    u8(&f, 4); be24(&f, (unsigned)vc.n); put(&f, vc.d, vc.n);
    u8(&f, 6); be24(&f, (unsigned)pic1.n); put(&f, pic1.d, pic1.n);
    u8(&f, 0x80 | 6); be24(&f, (unsigned)pic2.n); put(&f, pic2.d, pic2.n);
    const char *p = save("a.flac", &f);
    TagInfo t;
    CHECK(tags_read(p, &t));
    CHECK_STR(t.title, "Mixed Case Keys");
    CHECK_STR(t.artist, "\xC3\x85sa");
    CHECK_STR(t.album, "Sk\xC3\xA5ne");
    CHECK_STR(t.album_artist, "Group");
    CHECK(t.track == 4 && t.track_total == 10 && t.disc == 1 && t.year == 2003);
    CHECK(t.has_art);
    CHECK_STR(t.format, "flac");
    size_t n = 0;
    unsigned char *art = tags_read_art(p, &n, NULL, 0);
    CHECK(art && n == JPEG_LEN && !memcmp(art, JPEG, JPEG_LEN));   /* the front cover */
    free(art);
    free(vc.d); free(pic1.d); free(pic2.d); free(f.d);
}

/* ------------------------------------------------------------------- OGG */

static void ogg_page(Buf *f, int flags, int seq, const unsigned char *data, size_t n, bool packet_ends)
{
    /* data: whole segments of this page; packet_ends: last lacing value < 255 */
    put_s(f, "OggS"); u8(f, 0); u8(f, flags);
    for (int i = 0; i < 8; i++) u8(f, 0);
    le32w(f, 0x1234); le32w(f, (uint32_t)seq); le32w(f, 0);
    size_t full = n / 255, rem = n % 255;
    size_t nseg = full + (packet_ends ? 1 : (rem ? 1 : 0));
    if (!packet_ends && rem == 0) nseg = full;
    u8(f, (unsigned)nseg);
    for (size_t i = 0; i < full; i++) u8(f, 255);
    if (packet_ends) u8(f, (unsigned)rem);
    else if (rem) u8(f, (unsigned)rem);
    put(f, data, n);
}

static void test_ogg(void)
{
    /* a comment packet bigger than one page: it carries a base64 cover */
    Buf blockpic = { 0 };
    unsigned char big[40000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (unsigned char)(i * 7 + 1);
    memcpy(big, "\xFF\xD8\xFF", 3);
    picture_block(&blockpic, 3, "image/jpeg", big, sizeof big);
    /* base64 */
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    Buf b64 = { 0 };
    for (size_t i = 0; i < blockpic.n; i += 3) {
        uint32_t v = (uint32_t)blockpic.d[i] << 16 | (i + 1 < blockpic.n ? blockpic.d[i + 1] << 8 : 0) | (i + 2 < blockpic.n ? blockpic.d[i + 2] : 0);
        u8(&b64, tbl[(v >> 18) & 63]); u8(&b64, tbl[(v >> 12) & 63]);
        u8(&b64, i + 1 < blockpic.n ? tbl[(v >> 6) & 63] : '='); u8(&b64, i + 2 < blockpic.n ? tbl[v & 63] : '=');
    }
    u8(&b64, 0);
    const char *kv[] = { "TITLE=Ogg Song", "ARTIST=Ogg Artist", "TRACKNUMBER=2" };
    Buf packet = { 0 };
    put(&packet, "\x03vorbis", 7);
    vorbis_comment(&packet, kv, 3, "METADATA_BLOCK_PICTURE", (const char *)b64.d);
    u8(&packet, 1); /* framing bit */

    Buf f = { 0 };
    unsigned char ident[30] = { 1, 'v', 'o', 'r', 'b', 'i', 's' };
    ogg_page(&f, 2, 0, ident, sizeof ident, true);
    /* split the comment packet over several pages (continued-packet flag) */
    size_t pos = 0, chunk = 255 * 100;
    int seq = 1;
    while (pos < packet.n) {
        size_t n = packet.n - pos > chunk ? chunk : packet.n - pos;
        bool last = pos + n >= packet.n;
        ogg_page(&f, pos ? 1 : 0, seq++, packet.d + pos, n, last);
        pos += n;
    }
    const char *p = save("a.ogg", &f);
    TagInfo t;
    CHECK(tags_read(p, &t));
    CHECK_STR(t.title, "Ogg Song");
    CHECK_STR(t.artist, "Ogg Artist");
    CHECK(t.track == 2);
    CHECK(t.has_art);
    size_t n = 0;
    unsigned char *art = tags_read_art(p, &n, NULL, 0);
    CHECK(art && n == sizeof big && !memcmp(art, big, sizeof big));
    free(art);
    free(blockpic.d); free(b64.d); free(packet.d); free(f.d);
}

/* ------------------------------------------------------------------- MP4 */

static void mp4_box(Buf *out, const char *type4, const Buf *body)
{
    be32w(out, (uint32_t)(8 + body->n));
    put(out, type4, 4);
    put(out, body->d, body->n);
}

static void mp4_item(Buf *ilst, const char *key4, int dtype, const void *v, size_t n)
{
    Buf data = { 0 };
    be32w(&data, (uint32_t)dtype); be32w(&data, 0); put(&data, v, n);
    Buf item = { 0 };
    mp4_box(&item, "data", &data);
    mp4_box(ilst, key4, &item);
    free(data.d); free(item.d);
}

static void test_m4b(void)
{
    Buf ilst = { 0 };
    mp4_item(&ilst, "\xA9nam", 1, "The Book", 8);
    mp4_item(&ilst, "\xA9" "ART", 1, "An Author", 9);
    mp4_item(&ilst, "\xA9nrt", 1, "A Narrator", 10);
    mp4_item(&ilst, "\xA9" "alb", 1, "The Series", 10);
    mp4_item(&ilst, "\xA9" "day", 1, "2015", 4);
    const unsigned char trkn[] = { 0, 0, 0, 5, 0, 9, 0, 0 };
    mp4_item(&ilst, "trkn", 0, trkn, sizeof trkn);
    mp4_item(&ilst, "covr", 13, JPEG, JPEG_LEN);
    Buf meta = { 0 };
    be32w(&meta, 0);                       /* meta is a full box */
    mp4_box(&meta, "ilst", &ilst);
    Buf chpl = { 0 };
    be32w(&chpl, 0x01000000); be32w(&chpl, 0); u8(&chpl, 3);
    for (int i = 0; i < 3; i++) { be32w(&chpl, 0); be32w(&chpl, (uint32_t)(i * 100000000)); u8(&chpl, 2); put_s(&chpl, "Ch"); }
    Buf udta = { 0 };
    Buf metabox = { 0 };
    mp4_box(&metabox, "meta", &meta);
    put(&udta, metabox.d, metabox.n);
    mp4_box(&udta, "chpl", &chpl);
    Buf moov = { 0 };
    mp4_box(&moov, "udta", &udta);

    Buf f = { 0 };
    Buf ftyp = { 0 };
    put_s(&ftyp, "M4B "); be32w(&ftyp, 0);
    mp4_box(&f, "ftyp", &ftyp);
    Buf mdat = { 0 };
    for (int i = 0; i < 5000; i++) u8(&mdat, i);
    mp4_box(&f, "mdat", &mdat);            /* the tags come after the audio, as in real files */
    mp4_box(&f, "moov", &moov);
    const char *p = save("a.m4b", &f);
    TagInfo t;
    CHECK(tags_read(p, &t));
    CHECK_STR(t.title, "The Book");
    CHECK_STR(t.artist, "An Author");
    CHECK_STR(t.narrator, "A Narrator");
    CHECK_STR(t.album, "The Series");
    CHECK(t.year == 2015 && t.track == 5 && t.track_total == 9);
    CHECK(t.chapters == 3);
    CHECK(t.has_art);
    CHECK_STR(t.art_mime, "image/jpeg");
    size_t n = 0;
    unsigned char *art = tags_read_art(p, &n, NULL, 0);
    CHECK(art && n == JPEG_LEN && !memcmp(art, JPEG, JPEG_LEN));
    free(art);
    free(ilst.d); free(meta.d); free(chpl.d); free(udta.d); free(metabox.d); free(moov.d); free(f.d); free(ftyp.d); free(mdat.d);
}

/* ------------------------------------------------------------------- WAV */

static void test_wav(void)
{
    Buf info = { 0 };
    put_s(&info, "INFO");
    put_s(&info, "INAM"); le32w(&info, 6); put(&info, "Wav A\0", 6);
    put_s(&info, "IART"); le32w(&info, 4); put(&info, "Bob\0", 4);
    put_s(&info, "ITRK"); le32w(&info, 2); put(&info, "8\0", 2);
    Buf f = { 0 };
    put_s(&f, "RIFF"); le32w(&f, 0); put_s(&f, "WAVE");
    put_s(&f, "fmt "); le32w(&f, 16); for (int i = 0; i < 16; i++) u8(&f, 0);
    put_s(&f, "LIST"); le32w(&f, (uint32_t)info.n); put(&f, info.d, info.n);
    put_s(&f, "data"); le32w(&f, 4); put(&f, "\0\0\0\0", 4);
    const char *p = save("a.wav", &f);
    TagInfo t;
    CHECK(tags_read(p, &t));
    CHECK_STR(t.title, "Wav A");
    CHECK_STR(t.artist, "Bob");
    CHECK(t.track == 8);
    free(info.d); free(f.d);
}

/* ------------------------------------------------------------ robustness */

static void test_bad_input(void)
{
    TagInfo t;
    CHECK(!tags_read("tags_test_does_not_exist.mp3", &t));
    CHECK(t.title[0] == 0 && t.track == 0 && !t.has_art);

    Buf junk = { 0 };
    for (int i = 0; i < 5000; i++) u8(&junk, (i * 31 + 7) & 0xFF);
    CHECK(!tags_read(save("junk.bin", &junk), &t));

    /* a truncated ID3 tag and a truncated FLAC must not crash */
    Buf trunc = { 0 };
    put_s(&trunc, "ID3"); u8(&trunc, 3); u8(&trunc, 0); u8(&trunc, 0); ss32(&trunc, 100000);
    put_s(&trunc, "TIT2"); be32w(&trunc, 2000); be16(&trunc, 0); u8(&trunc, 0); put_s(&trunc, "abc");
    tags_read(save("trunc.mp3", &trunc), &t);
    Buf tf = { 0 };
    put_s(&tf, "fLaC"); u8(&tf, 4); be24(&tf, 5000000); put_s(&tf, "xxxx");
    tags_read(save("trunc.flac", &tf), &t);
    /* a box that claims to be bigger than the file */
    Buf tm = { 0 };
    be32w(&tm, 0x7FFFFFFF); put_s(&tm, "ftyp"); put_s(&tm, "M4B ");
    tags_read(save("trunc.m4b", &tm), &t);
    CHECK(tags_read_art("tags_test_trunc.flac", NULL, NULL, 0) == NULL);

    /* Latin-1 text that is not valid UTF-8 becomes UTF-8 */
    Buf body = { 0 };
    id3_text(&body, "TIT2", 0, "\xE9t\xE9", 3, 3);
    Buf f = { 0 };
    put_s(&f, "ID3"); u8(&f, 3); u8(&f, 0); u8(&f, 0); ss32(&f, (uint32_t)body.n); put(&f, body.d, body.n);
    CHECK(tags_read(save("latin.mp3", &f), &t));
    CHECK_STR(t.title, "\xC3\xA9t\xC3\xA9");
    free(junk.d); free(trunc.d); free(tf.d); free(tm.d); free(body.d); free(f.d);
}

static void test_read_only_and_order(void)
{
    const char *files[] = { "tags_test_v23.mp3", "tags_test_a.flac", "tags_test_a.ogg", "tags_test_a.m4b", "tags_test_a.wav" };
    for (int i = 0; i < 5; i++) {
        long s1, s2;
        uint32_t h1 = checksum(files[i], &s1);
        TagInfo t;
        tags_read(files[i], &t);
        size_t n;
        free(tags_read_art(files[i], &n, NULL, 0));
        uint32_t h2 = checksum(files[i], &s2);
        CHECK(h1 == h2 && s1 == s2 && s1 > 0);   /* files are byte-identical after reading */
    }
    TagInfo a = { .disc = 1, .track = 2 }, b = { .disc = 1, .track = 10 }, c = { .disc = 2, .track = 1 };
    CHECK(tags_order(&a, &b) < 0);
    CHECK(tags_order(&b, &c) < 0);   /* disc beats track */
    CHECK(tags_order(&c, &a) > 0);
    CHECK(tags_order(&a, &a) == 0);
}

int main(void)
{
    test_id3v23();
    test_id3v24_and_v1();
    test_flac();
    test_ogg();
    test_m4b();
    test_wav();
    test_bad_input();
    test_read_only_and_order();
    TEST_DONE();
}
