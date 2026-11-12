#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- normalization ---- */

/* Latin-1 Supplement and Latin Extended-A letters folded to ASCII. Index: code point - 0xC0 for 0xC0..0xFF. */
static const char *const k_latin1[64] = {
    "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","o","x","o","u","u","u","u","y","th","ss",
    "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","o","","o","u","u","u","u","y","th","y",
};
/* U+0100..U+017F, pairs of upper/lower share a base letter */
static const char *const k_latin_ext[128] = {
    "a","a","a","a","a","a","c","c","c","c","c","c","c","c","d","d",
    "d","d","e","e","e","e","e","e","e","e","e","e","g","g","g","g",
    "g","g","g","g","h","h","h","h","i","i","i","i","i","i","i","i",
    "i","i","ij","ij","j","j","k","k","k","l","l","l","l","l","l","l",
    "l","l","l","n","n","n","n","n","n","n","n","n","o","o","o","o",
    "o","o","oe","oe","r","r","r","r","r","r","s","s","s","s","s","s",
    "s","s","t","t","t","t","t","t","u","u","u","u","u","u","u","u",
    "u","u","u","u","w","w","y","y","y","z","z","z","z","z","z","s",
};

static unsigned next_cp(const unsigned char **p)
{
    const unsigned char *s = *p;
    unsigned c = s[0];
    if (c < 0x80) { *p += 1; return c; }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { *p += 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *p += 3;
        return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *p += 4;
        return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *p += 1;
    return 0xFFFD;
}

static size_t put_cp(char *out, size_t o, size_t cap, unsigned cp)
{
    if (cp < 0x80) { if (o + 1 < cap) out[o++] = (char)cp; }
    else if (cp < 0x800) { if (o + 2 < cap) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); } }
    else if (cp < 0x10000) {
        if (o + 3 < cap) { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
    } else if (o + 4 < cap) {
        out[o++] = (char)(0xF0 | (cp >> 18)); out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F));
    }
    return o;
}

size_t search_normalize(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    bool space = true;       /* swallow leading spaces */
    const unsigned char *p = (const unsigned char *)in;
    while (*p && o + 5 < cap) {
        unsigned cp = next_cp(&p);
        const char *fold = NULL;
        if (cp >= 'A' && cp <= 'Z') cp += 32;
        if (cp >= 0xC0 && cp <= 0xFF) fold = k_latin1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F) fold = k_latin_ext[cp - 0x100];
        else if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;          /* Cyrillic capitals */
        else if (cp >= 0x391 && cp <= 0x3A9) cp += 0x20;          /* Greek capitals */
        if (fold) {
            for (const char *f = fold; *f; f++) { out[o++] = *f; space = false; }
            continue;
        }
        bool alnum = (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || cp >= 0x80;
        if (cp == 0xFFFD || (cp >= 0x2000 && cp <= 0x206F) || cp == 0xA0 || cp == 0xD7 || cp == 0xF7) alnum = false;   /* spaces, dashes, quotes */
        if (alnum) {
            o = put_cp(out, o, cap, cp);
            space = false;
        } else if (!space) {
            out[o++] = ' ';
            space = true;
        }
    }
    if (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
    return o;
}

/* ---- index ---- */

void search_index_init(SearchIndex *x) { memset(x, 0, sizeof *x); }

void search_index_clear(SearchIndex *x)
{
    for (int i = 0; i < x->n; i++) {
        free(x->e[i].name);
        free(x->e[i].norm);
        free(x->e[i].name_norm);
    }
    free(x->e);
    memset(x, 0, sizeof *x);
}

bool search_index_add(SearchIndex *x, int kind, const char *name, const char *extra, void *ref)
{
    if (x->n == x->cap) {
        int cap = x->cap ? x->cap * 2 : 256;
        SearchEntry *e = realloc(x->e, sizeof *e * (size_t)cap);
        if (!e) return false;
        x->e = e;
        x->cap = cap;
    }
    size_t nl = strlen(name), el = extra ? strlen(extra) : 0;
    char *nn = malloc(nl * 2 + 8), *all = malloc((nl + el) * 2 + 16);
    char *copy = malloc(nl + 1);
    if (!nn || !all || !copy) { free(nn); free(all); free(copy); return false; }
    memcpy(copy, name, nl + 1);
    search_normalize(name, nn, nl * 2 + 8);
    size_t o = (size_t)snprintf(all, (nl + el) * 2 + 16, "%s", nn);
    if (el) {
        all[o++] = ' ';
        search_normalize(extra, all + o, (nl + el) * 2 + 16 - o);
    }
    SearchEntry *e = &x->e[x->n++];
    e->kind = kind;
    e->name = copy;
    e->name_norm = nn;
    e->norm = all;
    e->ref = ref;
    return true;
}

/* ---- matching ---- */

bool search_within_one_edit(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la > lb + 1 || lb > la + 1) return false;
    size_t i = 0;
    while (i < la && i < lb && a[i] == b[i]) i++;
    if (i == la && i == lb) return true;
    if (la == lb) {
        if (!strcmp(a + i + 1, b + i + 1)) return true;                                   /* one letter changed */
        if (i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i] && !strcmp(a + i + 2, b + i + 2)) return true;   /* swapped */
        return false;
    }
    if (la + 1 == lb) return !strcmp(a + i, b + i + 1);                                  /* one letter missing */
    return !strcmp(a + i + 1, b + i);                                                    /* one letter extra */
}

#define MAX_TOKENS 8

static int split_tokens(char *q, const char *tok[MAX_TOKENS])
{
    int n = 0;
    for (char *p = q; *p && n < MAX_TOKENS;) {
        while (*p == ' ') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

/* Does a word of `s` start with `tok`? */
static bool word_starts(const char *s, const char *tok, size_t tl)
{
    for (const char *w = s; *w;) {
        if (!strncmp(w, tok, tl)) return true;
        while (*w && *w != ' ') w++;
        while (*w == ' ') w++;
    }
    return false;
}

/* Is some word of `s` within one edit of `tok` (or a prefix of that word one edit away)? */
static bool word_typo(const char *s, const char *tok)
{
    char w[96];
    for (const char *p = s; *p;) {
        size_t l = 0;
        while (p[l] && p[l] != ' ' && l < sizeof w - 1) { w[l] = p[l]; l++; }
        w[l] = 0;
        if (search_within_one_edit(tok, w)) return true;
        p += l;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return false;
}

static int score_entry(const SearchEntry *e, const char *nq, const char *tok[], int nt, const size_t tlen[])
{
    if (!strcmp(e->name_norm, nq)) return 1000;
    if (!strncmp(e->name_norm, nq, strlen(nq))) return 800;
    bool all_words = true;
    for (int i = 0; i < nt && all_words; i++) all_words = word_starts(e->norm, tok[i], tlen[i]);
    if (all_words) {
        /* a word start in the name itself beats one in the artist or album */
        bool in_name = true;
        for (int i = 0; i < nt && in_name; i++) in_name = word_starts(e->name_norm, tok[i], tlen[i]);
        return in_name ? 600 : 560;
    }
    if (strstr(e->norm, nq)) return strstr(e->name_norm, nq) ? 400 : 360;
    /* typos: every token matches a word exactly, or (4 or more letters) within one edit */
    bool typo = true, any_typo = false;
    for (int i = 0; i < nt && typo; i++) {
        if (word_starts(e->norm, tok[i], tlen[i])) continue;
        if (tlen[i] >= 4 && word_typo(e->norm, tok[i])) { any_typo = true; continue; }
        typo = false;
    }
    return typo && any_typo ? 200 : 0;
}

static int hit_cmp(const void *pa, const void *pb)
{
    const SearchHit *a = pa, *b = pb;
    if (a->score != b->score) return b->score - a->score;
    return a->entry - b->entry;
}

int search_query(const SearchIndex *x, const char *query, int kind, int scope, SearchHit *out, int max)
{
    if (max <= 0 || !query) return 0;
    if (scope != SCOPE_FILTER_ALL && scope - 1 != kind) return 0;
    char nq[256];
    search_normalize(query, nq, sizeof nq);
    if (!nq[0]) return 0;
    char tq[256];
    memcpy(tq, nq, sizeof tq);
    const char *tok[MAX_TOKENS];
    int nt = split_tokens(tq, tok);
    size_t tlen[MAX_TOKENS];
    for (int i = 0; i < nt; i++) tlen[i] = strlen(tok[i]);

    /* keep the best `max` as we go (a small insertion list), so a huge library costs one pass */
    int n = 0;
    for (int i = 0; i < x->n; i++) {
        const SearchEntry *e = &x->e[i];
        if (e->kind != kind) continue;
        int sc = score_entry(e, nq, tok, nt, tlen);
        if (sc <= 0) continue;
        /* shorter names first among equals */
        sc = sc * 4 - (int)(strlen(e->name_norm) > 3 ? 3 : 0);
        if (n == max && (sc < out[n - 1].score || (sc == out[n - 1].score && i > out[n - 1].entry))) continue;
        int j = n < max ? n++ : n - 1;
        out[j].entry = i;
        out[j].score = sc;
        while (j > 0 && hit_cmp(&out[j - 1], &out[j]) > 0) {
            SearchHit t = out[j - 1];
            out[j - 1] = out[j];
            out[j] = t;
            j--;
        }
    }
    return n;
}
