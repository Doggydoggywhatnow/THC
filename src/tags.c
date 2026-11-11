#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
/* tags.c: read-only metadata readers. See tags.h. */
#include "tags.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define FSEEK _fseeki64
#define FTELL _ftelli64
#else
#define FSEEK fseeko
#define FTELL ftello
#endif

#define MAX_TAG_BYTES (64u * 1024 * 1024)   /* never read more than this for one tag */
#define MAX_TEXT_BLOCK (8u * 1024 * 1024)

typedef struct Ctx {
    FILE *f;
    int64_t size;
    TagInfo *t;
    bool want_art;
    unsigned char *art;
    size_t art_len;
    char art_mime[32];
    int art_pri;
} Ctx;

/* ---------------------------------------------------------------- helpers */

static bool rd(FILE *f, int64_t off, void *buf, size_t n)
{
    if (off < 0 || FSEEK(f, off, SEEK_SET) != 0) return false;
    return fread(buf, 1, n, f) == n;
}

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
static uint32_t syncsafe(const unsigned char *p) { return (uint32_t)(p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F); }

static size_t put_cp(char *d, size_t cap, size_t o, uint32_t cp)
{
    char tmp[4];
    int n;
    if (cp < 0x80) { tmp[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { tmp[0] = (char)(0xC0 | cp >> 6); tmp[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { tmp[0] = (char)(0xE0 | cp >> 12); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else { tmp[0] = (char)(0xF0 | cp >> 18); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    if (o + (size_t)n >= cap) return o;
    memcpy(d + o, tmp, (size_t)n);
    return o + (size_t)n;
}

/* Valid UTF-8 check; bad input is treated as Latin-1. */
static bool valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t k;
        if (c < 0x80) k = 0;
        else if ((c >> 5) == 6) k = 1;
        else if ((c >> 4) == 14) k = 2;
        else if ((c >> 3) == 30) k = 3;
        else return false;
        for (size_t j = 1; j <= k; j++)
            if (i + j >= n || (s[i + j] & 0xC0) != 0x80) return false;
        i += k + 1;
    }
    return true;
}

/* enc: 0 Latin-1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8, 4 "UTF-8 or Latin-1" */
static void to_utf8(char *dst, size_t cap, const unsigned char *s, size_t n, int enc)
{
    size_t o = 0;
    if (!cap) return;
    if (enc == 1 || enc == 2) {
        bool be = enc == 2;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (s[0] == 0xFF && s[1] == 0xFE) { be = false; i = 2; }
            else if (s[0] == 0xFE && s[1] == 0xFF) { be = true; i = 2; }
        }
        for (; i + 1 < n; i += 2) {
            uint32_t u = be ? (uint32_t)(s[i] << 8 | s[i + 1]) : (uint32_t)(s[i + 1] << 8 | s[i]);
            if (u == 0) break;
            if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
                uint32_t u2 = be ? (uint32_t)(s[i + 2] << 8 | s[i + 3]) : (uint32_t)(s[i + 3] << 8 | s[i + 2]);
                if (u2 >= 0xDC00 && u2 < 0xE000) {
                    u = 0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00);
                    i += 2;
                }
            }
            o = put_cp(dst, cap, o, u);
        }
    } else {
        /* stop at the first NUL (ID3v2.4 multi-value fields: keep the first) */
        size_t len = 0;
        while (len < n && s[len]) len++;
        if (enc == 3 || (enc == 4 && valid_utf8(s, len))) {
            o = len < cap - 1 ? len : cap - 1;
            memcpy(dst, s, o);
            /* do not cut a multi-byte character in half */
            while (o > 0 && o < len && (((unsigned char)dst[o]) & 0xC0) == 0x80) o--;
        } else {
            for (size_t i = 0; i < len; i++) o = put_cp(dst, cap, o, s[i]);
        }
    }
    dst[o] = 0;
    /* trim */
    while (o > 0 && (dst[o - 1] == ' ' || dst[o - 1] == '\r' || dst[o - 1] == '\n')) dst[--o] = 0;
    size_t lead = 0;
    while (dst[lead] == ' ') lead++;
    if (lead) memmove(dst, dst + lead, strlen(dst + lead) + 1);
}

static void set_str(char *dst, size_t cap, const unsigned char *s, size_t n, int enc)
{
    if (dst[0]) return; /* first value found wins (ID3v2 before ID3v1, etc.) */
    to_utf8(dst, cap, s, n, enc);
}

static void parse_pair(const char *s, int *a, int *b)
{
    int x = 0, y = 0;
    int n = sscanf(s, "%d/%d", &x, &y);
    if (n >= 1 && x > 0 && !*a) *a = x;
    if (n >= 2 && y > 0 && !*b) *b = y;
}

static int parse_year(const char *s)
{
    int y = 0;
    for (int i = 0; i < 4; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        y = y * 10 + (s[i] - '0');
    }
    return y;
}

static const char *sniff_mime(const unsigned char *d, size_t n)
{
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return "image/jpeg";
    if (n >= 8 && !memcmp(d, "\x89PNG", 4)) return "image/png";
    if (n >= 6 && !memcmp(d, "GIF8", 4)) return "image/gif";
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return "image/bmp";
    return "";
}

/* Offer a picture. Front covers (type 3) beat other types; the first of a kind wins.
 * `data` may be NULL when only its existence matters (n then unused). */
static void art_offer(Ctx *c, const unsigned char *data, size_t n, const char *mime, int type)
{
    int pri = type == 3 ? 2 : 1;
    if (!c->t->has_art) {
        c->t->has_art = true;
    }
    if (c->art_pri >= pri) return;
    c->art_pri = pri;
    const char *m = (mime && *mime && strchr(mime, '/')) ? mime : (data ? sniff_mime(data, n) : "");
    snprintf(c->art_mime, sizeof c->art_mime, "%s", m);
    snprintf(c->t->art_mime, sizeof c->t->art_mime, "%s", m);
    if (c->want_art && data && n) {
        free(c->art);
        c->art = (unsigned char *)malloc(n);
        if (c->art) {
            memcpy(c->art, data, n);
            c->art_len = n;
        } else {
            c->art_len = 0;
        }
    }
}

/* ------------------------------------------------------------------ ID3 */

static size_t unsync(unsigned char *p, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        p[o++] = p[i];
        if (p[i] == 0xFF && i + 1 < n && p[i + 1] == 0) i++;
    }
    return o;
}

/* Skip a terminated string in an ID3 text field; returns bytes consumed. */
static size_t id3_skip_term(const unsigned char *p, size_t n, int enc)
{
    if (enc == 1 || enc == 2) {
        for (size_t i = 0; i + 1 < n; i += 2)
            if (!p[i] && !p[i + 1]) return i + 2;
        return n;
    }
    for (size_t i = 0; i < n; i++)
        if (!p[i]) return i + 1;
    return n;
}

static void id3_frame(Ctx *c, const char *id, bool v22, const unsigned char *d, size_t n)
{
    TagInfo *t = c->t;
    if (n < 1) return;
    int enc = d[0];
    if (enc > 3) enc = 0;
    const unsigned char *txt = d + 1;
    size_t tn = n - 1;
#define IS(a, b) (!strcmp(id, v22 ? (a) : (b)))
    if (IS("TT2", "TIT2")) set_str(t->title, sizeof t->title, txt, tn, enc);
    else if (IS("TP1", "TPE1")) set_str(t->artist, sizeof t->artist, txt, tn, enc);
    else if (IS("TAL", "TALB")) set_str(t->album, sizeof t->album, txt, tn, enc);
    else if (IS("TP2", "TPE2")) set_str(t->album_artist, sizeof t->album_artist, txt, tn, enc);
    else if (IS("TCM", "TCOM")) set_str(t->narrator, sizeof t->narrator, txt, tn, enc);
    else if (IS("TCO", "TCON")) set_str(t->genre, sizeof t->genre, txt, tn, enc);
    else if (IS("TRK", "TRCK") || IS("TPA", "TPOS")) {
        char buf[32];
        to_utf8(buf, sizeof buf, txt, tn, enc);
        if (IS("TRK", "TRCK")) parse_pair(buf, &t->track, &t->track_total);
        else parse_pair(buf, &t->disc, &t->disc_total);
    } else if (IS("TYE", "TYER") || !strcmp(id, "TDRC")) {
        char buf[32];
        to_utf8(buf, sizeof buf, txt, tn, enc);
        if (!t->year) t->year = parse_year(buf);
    } else if (IS("PIC", "APIC")) {
        char mime[32] = "";
        size_t i = 0;
        if (v22) {
            if (tn < 3) return;
            snprintf(mime, sizeof mime, "image/%c%c%c", txt[0] | 0x20, txt[1] | 0x20, txt[2] | 0x20);
            if (!strncmp(mime, "image/jpg", 9)) snprintf(mime, sizeof mime, "image/jpeg");
            i = 3;
        } else {
            size_t k = 0;
            while (k < tn && txt[k]) k++;
            snprintf(mime, sizeof mime, "%.*s", (int)(k < sizeof mime - 1 ? k : sizeof mime - 1), (const char *)txt);
            i = k + 1;
        }
        if (i >= tn) return;
        int type = txt[i++];
        i += id3_skip_term(txt + i, tn - i, enc);
        if (i >= tn) return;
        art_offer(c, txt + i, tn - i, mime, type);
    }
#undef IS
}

/* Parse an ID3v2 tag at `off`. Returns the tag's total length (header + body), or 0. */
static int64_t parse_id3v2(Ctx *c, int64_t off)
{
    unsigned char h[10];
    if (!rd(c->f, off, h, 10) || memcmp(h, "ID3", 3) != 0) return 0;
    int ver = h[3];
    if (ver < 2 || ver > 4) return 0;
    for (int i = 6; i < 10; i++)
        if (h[i] & 0x80) return 0;
    uint32_t size = syncsafe(h + 6);
    int64_t total = 10 + (int64_t)size + ((h[5] & 0x10) ? 10 : 0);
    if (size == 0) return total;
    if (off + 10 + (int64_t)size > c->size) size = (uint32_t)(c->size - off - 10 > 0 ? c->size - off - 10 : 0);
    if (size > MAX_TAG_BYTES) size = MAX_TAG_BYTES;
    unsigned char *buf = (unsigned char *)malloc(size ? size : 1);
    if (!buf) return total;
    if (!rd(c->f, off + 10, buf, size)) { free(buf); return total; }
    size_t n = size;
    if (h[5] & 0x80) n = unsync(buf, n);
    size_t p = 0;
    if (h[5] & 0x40) { /* extended header */
        if (ver == 3 && n >= 4) p = 4 + be32(buf);
        else if (ver == 4 && n >= 4) p = syncsafe(buf);
        if (p > n) p = n;
    }
    bool v22 = ver == 2;
    size_t hl = v22 ? 6 : 10;
    while (p + hl <= n) {
        if (buf[p] == 0) break; /* padding */
        char id[5] = { 0 };
        memcpy(id, buf + p, v22 ? 3 : 4);
        size_t fs = v22 ? ((size_t)buf[p + 3] << 16 | buf[p + 4] << 8 | buf[p + 5])
                        : (ver == 4 ? syncsafe(buf + p + 4) : be32(buf + p + 4));
        unsigned fl2 = v22 ? 0 : buf[p + 9];
        p += hl;
        if (fs > n - p) break;
        unsigned char *d = buf + p;
        size_t dn = fs;
        bool skip = false;
        if (ver == 4) {
            if (fl2 & 0x08) skip = true;               /* compressed */
            if (fl2 & 0x04) skip = true;               /* encrypted */
            if (!skip && (fl2 & 0x01) && dn >= 4) { d += 4; dn -= 4; } /* data length indicator */
            if (!skip && (fl2 & 0x02)) dn = unsync(d, dn);
        } else if (ver == 3 && (fl2 & 0xC0)) {
            skip = true;                                  /* compressed or encrypted */
        }
        if (!skip) id3_frame(c, id, v22, d, dn);
        p += fs;
    }
    free(buf);
    if (!c->t->format[0]) snprintf(c->t->format, sizeof c->t->format, "id3");
    return total;
}

static void parse_id3v1(Ctx *c)
{
    unsigned char b[128];
    if (c->size < 128 || !rd(c->f, c->size - 128, b, 128) || memcmp(b, "TAG", 3) != 0) return;
    set_str(c->t->title, sizeof c->t->title, b + 3, 30, 0);
    set_str(c->t->artist, sizeof c->t->artist, b + 33, 30, 0);
    set_str(c->t->album, sizeof c->t->album, b + 63, 30, 0);
    char y[5] = { 0 };
    memcpy(y, b + 93, 4);
    if (!c->t->year) c->t->year = parse_year(y);
    if (b[125] == 0 && b[126] != 0 && !c->t->track) c->t->track = b[126];
    if (!c->t->format[0]) snprintf(c->t->format, sizeof c->t->format, "id3");
}

/* ---------------------------------------------------------- Vorbis comments */

static int b64val(int ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+' || ch == '-') return 62;
    if (ch == '/' || ch == '_') return 63;
    return -1;
}

static unsigned char *b64_decode(const char *s, size_t n, size_t *out)
{
    unsigned char *d = (unsigned char *)malloc(n * 3 / 4 + 4);
    if (!d) return NULL;
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = b64val((unsigned char)s[i]);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            d[o++] = (unsigned char)(acc >> bits);
        }
    }
    *out = o;
    return d;
}

/* A FLAC PICTURE block body: type, mime, description, size fields, data. */
static void parse_picture_block(Ctx *c, const unsigned char *b, size_t n)
{
    if (n < 32) return;
    uint32_t type = be32(b);
    size_t p = 4;
    uint32_t ml = be32(b + p);
    p += 4;
    if (ml > n - p) return;
    char mime[32];
    snprintf(mime, sizeof mime, "%.*s", (int)(ml < sizeof mime - 1 ? ml : sizeof mime - 1), (const char *)b + p);
    p += ml;
    if (p + 4 > n) return;
    uint32_t dl = be32(b + p);
    p += 4;
    if (dl > n - p || n - p - dl < 20) return;
    p += dl + 16; /* width, height, depth, colors */
    uint32_t len = be32(b + p);
    p += 4;
    if (len > n - p) return;
    art_offer(c, b + p, len, mime, (int)type);
}

static void parse_vorbis_comments(Ctx *c, const unsigned char *b, size_t n)
{
    TagInfo *t = c->t;
    if (n < 8) return;
    size_t p = 0;
    uint32_t vl = le32(b);
    p = 4;
    if (vl > n - p) return;
    p += vl;
    if (p + 4 > n) return;
    uint32_t count = le32(b + p);
    p += 4;
    for (uint32_t i = 0; i < count && p + 4 <= n; i++) {
        uint32_t l = le32(b + p);
        p += 4;
        if (l > n - p) break;
        const char *kv = (const char *)b + p;
        const char *eq = memchr(kv, '=', l);
        p += l;
        if (!eq) continue;
        size_t kl = (size_t)(eq - kv);
        const unsigned char *val = (const unsigned char *)eq + 1;
        size_t vn = l - kl - 1;
        char key[40];
        size_t kk = kl < sizeof key - 1 ? kl : sizeof key - 1;
        for (size_t j = 0; j < kk; j++) key[j] = (char)((kv[j] >= 'a' && kv[j] <= 'z') ? kv[j] - 32 : kv[j]);
        key[kk] = 0;
        if (!strcmp(key, "TITLE")) set_str(t->title, sizeof t->title, val, vn, 4);
        else if (!strcmp(key, "ARTIST")) set_str(t->artist, sizeof t->artist, val, vn, 4);
        else if (!strcmp(key, "ALBUM")) set_str(t->album, sizeof t->album, val, vn, 4);
        else if (!strcmp(key, "ALBUMARTIST") || !strcmp(key, "ALBUM ARTIST"))
            set_str(t->album_artist, sizeof t->album_artist, val, vn, 4);
        else if (!strcmp(key, "GENRE")) set_str(t->genre, sizeof t->genre, val, vn, 4);
        else if (!strcmp(key, "NARRATOR")) set_str(t->narrator, sizeof t->narrator, val, vn, 4);
        else if (!strcmp(key, "TRACKNUMBER") || !strcmp(key, "DISCNUMBER") || !strcmp(key, "TRACKTOTAL") ||
                 !strcmp(key, "TOTALTRACKS") || !strcmp(key, "DISCTOTAL") || !strcmp(key, "TOTALDISCS") ||
                 !strcmp(key, "DATE") || !strcmp(key, "YEAR")) {
            char buf[32];
            to_utf8(buf, sizeof buf, val, vn, 4);
            if (!strcmp(key, "TRACKNUMBER")) parse_pair(buf, &t->track, &t->track_total);
            else if (!strcmp(key, "DISCNUMBER")) parse_pair(buf, &t->disc, &t->disc_total);
            else if (!strcmp(key, "TRACKTOTAL") || !strcmp(key, "TOTALTRACKS")) { if (!t->track_total) t->track_total = atoi(buf); }
            else if (!strcmp(key, "DISCTOTAL") || !strcmp(key, "TOTALDISCS")) { if (!t->disc_total) t->disc_total = atoi(buf); }
            else if (!t->year) t->year = parse_year(buf);
        } else if (!strcmp(key, "METADATA_BLOCK_PICTURE")) {
            size_t dn = 0;
            unsigned char *raw = b64_decode((const char *)val, vn, &dn);
            if (raw) {
                parse_picture_block(c, raw, dn);
                free(raw);
            }
        }
    }
}

/* ------------------------------------------------------------------ FLAC */

static void parse_flac(Ctx *c, int64_t off)
{
    int64_t p = off + 4;
    for (int guard = 0; guard < 4096; guard++) {
        unsigned char h[4];
        if (!rd(c->f, p, h, 4)) return;
        bool last = h[0] & 0x80;
        int type = h[0] & 0x7F;
        uint32_t len = (uint32_t)h[1] << 16 | h[2] << 8 | h[3];
        int64_t body = p + 4;
        if (body + len > c->size) return;
        if (type == 4 || type == 6) {
            if (len <= MAX_TEXT_BLOCK + MAX_TAG_BYTES) {
                if (type == 6 && !c->want_art) {
                    /* only learn that a picture exists, and its type and mime */
                    unsigned char head[8 + 64];
                    size_t hn = len < sizeof head ? len : sizeof head;
                    if (rd(c->f, body, head, hn) && hn >= 8) {
                        uint32_t pt = be32(head), ml = be32(head + 4);
                        char mime[32] = "";
                        if (ml < sizeof mime && 8 + ml <= hn) snprintf(mime, sizeof mime, "%.*s", (int)ml, (const char *)head + 8);
                        art_offer(c, NULL, 0, mime, (int)pt);
                    }
                } else {
                    unsigned char *buf = (unsigned char *)malloc(len ? len : 1);
                    if (buf && rd(c->f, body, buf, len)) {
                        if (type == 4) parse_vorbis_comments(c, buf, len);
                        else parse_picture_block(c, buf, len);
                    }
                    free(buf);
                }
            }
        }
        if (last) break;
        p = body + len;
    }
    snprintf(c->t->format, sizeof c->t->format, "flac");
}

/* ------------------------------------------------------------------- OGG */

static void parse_ogg(Ctx *c)
{
    int64_t off = 0;
    unsigned char *pkt = NULL;
    size_t pn = 0, pcap = 0;
    int pktno = 0;
    uint32_t serial = 0;
    bool have_serial = false;
    int guard = 0;
    while (guard++ < 20000) {
        unsigned char h[27];
        if (!rd(c->f, off, h, 27) || memcmp(h, "OggS", 4) != 0) break;
        uint32_t ser = le32(h + 14);
        int nseg = h[26];
        unsigned char seg[255];
        if (!rd(c->f, off + 27, seg, (size_t)nseg)) break;
        size_t dlen = 0;
        for (int i = 0; i < nseg; i++) dlen += seg[i];
        int64_t dpos = off + 27 + nseg;
        off = dpos + (int64_t)dlen;
        if (!have_serial) { serial = ser; have_serial = true; }
        if (ser != serial) continue;
        size_t consumed = 0;
        bool done = false;
        for (int i = 0; i < nseg && !done; i++) {
            if (pn + seg[i] > MAX_TAG_BYTES) { done = true; break; }
            if (pn + seg[i] > pcap) {
                size_t nc = (pn + seg[i]) * 2 + 4096;
                unsigned char *np = (unsigned char *)realloc(pkt, nc);
                if (!np) { done = true; break; }
                pkt = np;
                pcap = nc;
            }
            if (!rd(c->f, dpos + (int64_t)consumed, pkt + pn, seg[i])) { done = true; break; }
            pn += seg[i];
            consumed += seg[i];
            if (seg[i] < 255) {
                if (pktno == 1) {
                    if (pn > 7 && !memcmp(pkt, "\x03vorbis", 7)) parse_vorbis_comments(c, pkt + 7, pn - 7);
                    else if (pn > 8 && !memcmp(pkt, "OpusTags", 8)) parse_vorbis_comments(c, pkt + 8, pn - 8);
                    done = true;
                    break;
                }
                pktno++;
                pn = 0;
            }
        }
        if (done) break;
    }
    free(pkt);
    snprintf(c->t->format, sizeof c->t->format, "ogg");
}

/* ------------------------------------------------------------------- MP4 */

static int mp4_int(const unsigned char *p, size_t n)
{
    int v = 0;
    for (size_t i = 0; i < n && i < 4; i++) v = v << 8 | p[i];
    return v;
}

static void mp4_item(Ctx *c, const unsigned char key[4], int64_t start, int64_t end)
{
    /* an ilst item holds one or more 'data' boxes */
    int64_t p = start;
    while (p + 16 <= end) {
        unsigned char h[8];
        if (!rd(c->f, p, h, 8)) return;
        uint32_t bs = be32(h);
        if (bs < 16 || p + bs > end) return;
        if (!memcmp(h + 4, "data", 4)) {
            unsigned char meta[8];
            if (!rd(c->f, p + 8, meta, 8)) return;
            int dtype = (int)(be32(meta) & 0xFFFFFF);
            size_t vn = bs - 16;
            int64_t vpos = p + 16;
            bool is_cover = !memcmp(key, "covr", 4);
            if (is_cover) {
                if (!c->want_art) {
                    unsigned char head[16];
                    size_t hn = vn < sizeof head ? vn : sizeof head;
                    if (rd(c->f, vpos, head, hn)) art_offer(c, NULL, 0, dtype == 14 ? "image/png" : dtype == 13 ? "image/jpeg" : sniff_mime(head, hn), 3);
                } else if (vn <= MAX_TAG_BYTES) {
                    unsigned char *buf = (unsigned char *)malloc(vn ? vn : 1);
                    if (buf && rd(c->f, vpos, buf, vn))
                        art_offer(c, buf, vn, dtype == 14 ? "image/png" : dtype == 13 ? "image/jpeg" : NULL, 3);
                    free(buf);
                }
            } else if (vn > 0 && vn <= 65536) {
                unsigned char *buf = (unsigned char *)malloc(vn);
                if (buf && rd(c->f, vpos, buf, vn)) {
                    TagInfo *t = c->t;
                    if (!memcmp(key, "\xA9nam", 4)) set_str(t->title, sizeof t->title, buf, vn, 4);
                    else if (!memcmp(key, "\xA9" "ART", 4)) set_str(t->artist, sizeof t->artist, buf, vn, 4);
                    else if (!memcmp(key, "\xA9" "alb", 4)) set_str(t->album, sizeof t->album, buf, vn, 4);
                    else if (!memcmp(key, "aART", 4)) set_str(t->album_artist, sizeof t->album_artist, buf, vn, 4);
                    else if (!memcmp(key, "\xA9nrt", 4)) set_str(t->narrator, sizeof t->narrator, buf, vn, 4);
                    else if (!memcmp(key, "\xA9gen", 4)) set_str(t->genre, sizeof t->genre, buf, vn, 4);
                    else if (!memcmp(key, "\xA9" "day", 4)) { char y[16]; to_utf8(y, sizeof y, buf, vn, 4); if (!t->year) t->year = parse_year(y); }
                    else if (!memcmp(key, "trkn", 4) && vn >= 6) {
                        if (!t->track) t->track = mp4_int(buf + 2, 2);
                        if (!t->track_total) t->track_total = mp4_int(buf + 4, 2);
                    } else if (!memcmp(key, "disk", 4) && vn >= 6) {
                        if (!t->disc) t->disc = mp4_int(buf + 2, 2);
                        if (!t->disc_total) t->disc_total = mp4_int(buf + 4, 2);
                    }
                }
                free(buf);
            }
            return; /* first data box is enough */
        }
        p += bs;
    }
}

static void mp4_boxes(Ctx *c, int64_t start, int64_t end, int depth, const char *parent)
{
    if (depth > 6) return;
    int64_t p = start;
    int guard = 0;
    while (p + 8 <= end && guard++ < 100000) {
        unsigned char h[16];
        if (!rd(c->f, p, h, 8)) return;
        uint64_t sz = be32(h);
        int64_t hl = 8;
        if (sz == 1) {
            if (!rd(c->f, p + 8, h + 8, 8)) return;
            sz = (uint64_t)be32(h + 8) << 32 | be32(h + 12);
            hl = 16;
        } else if (sz == 0) {
            sz = (uint64_t)(end - p);
        }
        if (sz < (uint64_t)hl || p + (int64_t)sz > end) return;
        char type[5];
        memcpy(type, h + 4, 4);
        type[4] = 0;
        int64_t body = p + hl, bend = p + (int64_t)sz;
        if (!strcmp(parent, "ilst")) {
            mp4_item(c, h + 4, body, bend);
        } else if (!strcmp(type, "moov") || !strcmp(type, "udta") || !strcmp(type, "ilst")) {
            mp4_boxes(c, body, bend, depth + 1, type);
        } else if (!strcmp(type, "meta")) {
            mp4_boxes(c, body + 4, bend, depth + 1, type); /* meta is a full box */
        } else if (!strcmp(type, "chpl") && !strcmp(parent, "udta")) {
            unsigned char ch[9];
            if (bend - body >= 9 && rd(c->f, body, ch, 9) && ch[8] > 0) c->t->chapters = ch[8];
        }
        p = bend;
    }
}

/* ------------------------------------------------------------------- WAV */

static void parse_wav(Ctx *c)
{
    int64_t p = 12;
    for (int guard = 0; guard < 4096 && p + 8 <= c->size; guard++) {
        unsigned char h[8];
        if (!rd(c->f, p, h, 8)) return;
        uint32_t len = le32(h + 4);
        int64_t body = p + 8;
        if (!memcmp(h, "LIST", 4) && len >= 4 && len <= 1u << 20) {
            unsigned char *buf = (unsigned char *)malloc(len);
            if (buf && rd(c->f, body, buf, len) && !memcmp(buf, "INFO", 4)) {
                size_t q = 4;
                while (q + 8 <= len) {
                    uint32_t sl = le32(buf + q + 4);
                    const unsigned char *v = buf + q + 8;
                    if (sl > len - q - 8) break;
                    TagInfo *t = c->t;
                    if (!memcmp(buf + q, "INAM", 4)) set_str(t->title, sizeof t->title, v, sl, 4);
                    else if (!memcmp(buf + q, "IART", 4)) set_str(t->artist, sizeof t->artist, v, sl, 4);
                    else if (!memcmp(buf + q, "IPRD", 4)) set_str(t->album, sizeof t->album, v, sl, 4);
                    else if (!memcmp(buf + q, "IGNR", 4)) set_str(t->genre, sizeof t->genre, v, sl, 4);
                    else if (!memcmp(buf + q, "ITRK", 4) || !memcmp(buf + q, "IPRT", 4)) {
                        char b[32];
                        to_utf8(b, sizeof b, v, sl, 4);
                        parse_pair(b, &t->track, &t->track_total);
                    } else if (!memcmp(buf + q, "ICRD", 4)) {
                        char b[32];
                        to_utf8(b, sizeof b, v, sl, 4);
                        if (!t->year) t->year = parse_year(b);
                    }
                    q += 8 + sl + (sl & 1);
                }
                if (!c->t->format[0]) snprintf(c->t->format, sizeof c->t->format, "wav");
            }
            free(buf);
        } else if ((!memcmp(h, "id3 ", 4) || !memcmp(h, "ID3 ", 4)) && len >= 10) {
            parse_id3v2(c, body);
            snprintf(c->t->format, sizeof c->t->format, "wav");
        }
        p = body + len + (len & 1);
    }
}

/* ------------------------------------------------------------------ main */

static bool any_field(const TagInfo *t)
{
    return t->title[0] || t->artist[0] || t->album[0] || t->album_artist[0] || t->genre[0] || t->narrator[0] ||
           t->track || t->disc || t->year || t->chapters || t->has_art;
}

static bool run(const char *path, TagInfo *t, bool want_art, unsigned char **art, size_t *art_len, char *mime,
                size_t mime_cap)
{
    memset(t, 0, sizeof *t);
    if (art) *art = NULL;
    if (art_len) *art_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Ctx c;
    memset(&c, 0, sizeof c);
    c.f = f;
    c.t = t;
    c.want_art = want_art;
    FSEEK(f, 0, SEEK_END);
    c.size = (int64_t)FTELL(f);
    unsigned char h[12];
    if (c.size >= 12 && rd(f, 0, h, 12)) {
        int64_t start = 0;
        if (!memcmp(h, "ID3", 3)) {
            int64_t total = parse_id3v2(&c, 0);
            start = total;
            unsigned char m[4];
            if (total > 0 && rd(f, total, m, 4)) {
                if (!memcmp(m, "fLaC", 4)) parse_flac(&c, total);
            }
            parse_id3v1(&c);
        } else if (!memcmp(h, "fLaC", 4)) {
            parse_flac(&c, 0);
        } else if (!memcmp(h, "OggS", 4)) {
            parse_ogg(&c);
        } else if (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) {
            parse_wav(&c);
        } else if (!memcmp(h + 4, "ftyp", 4)) {
            mp4_boxes(&c, 0, c.size, 0, "");
            snprintf(t->format, sizeof t->format, "mp4");
        } else {
            parse_id3v1(&c);
        }
        (void)start;
    }
    fclose(f);
    if (mime && mime_cap) snprintf(mime, mime_cap, "%s", c.art_mime);
    if (art) *art = c.art; else free(c.art);
    if (art_len) *art_len = c.art_len;
    return any_field(t);
}

bool tags_read(const char *path, TagInfo *t)
{
    return run(path, t, false, NULL, NULL, NULL, 0);
}

unsigned char *tags_read_art(const char *path, size_t *len, char *mime, size_t mime_cap)
{
    TagInfo t;
    unsigned char *art = NULL;
    size_t n = 0;
    run(path, &t, true, &art, &n, mime, mime_cap);
    if (len) *len = n;
    if (!n) {
        free(art);
        return NULL;
    }
    return art;
}

int tags_order(const TagInfo *a, const TagInfo *b)
{
    if (a->disc != b->disc) return a->disc < b->disc ? -1 : 1;
    if (a->track != b->track) return a->track < b->track ? -1 : 1;
    return 0;
}
