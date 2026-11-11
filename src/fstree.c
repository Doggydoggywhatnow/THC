#include "fstree.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

const char *path_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return "";
    return dot + 1;
}

static bool ext_in(const char *ext, const char *const *list)
{
    for (int i = 0; list[i]; i++) {
        const char *a = ext, *b = list[i];
        while (*a && *b && tolower((unsigned char)*a) == *b) { a++; b++; }
        if (!*a && !*b) return true;
    }
    return false;
}

MediaKind media_kind(const char *name)
{
    static const char *const video[] = { "mkv", "mp4", "m4v", "avi", "mov", "webm", "wmv", "flv",
                                         "mpg", "mpeg", "ts", "m2ts", "mts", "ogv", "3gp", "vob",
                                         "divx", "rmvb", "asf", NULL };
    static const char *const audio[] = { "flac", "wav", "ogg", "oga", "mp3", NULL };
    static const char *const audio_mpv[] = { "m4a", "aac", "opus", "wma", "alac", "aiff", "aif",
                                             "ape", "wv", "mka", NULL };
    static const char *const book[] = { "m4b", NULL };
    static const char *const image[] = { "jpg", "jpeg", "png", NULL };
    const char *e = path_ext(name);
    if (!*e) return MK_OTHER;
    if (ext_in(e, video)) return MK_VIDEO;
    if (ext_in(e, audio)) return MK_AUDIO;
    if (ext_in(e, audio_mpv)) return MK_AUDIO_MPV;
    if (ext_in(e, book)) return MK_BOOK;
    if (ext_in(e, image)) return MK_IMAGE;
    return MK_OTHER;
}

FsNode *fs_new(const char *name, bool dir)
{
    FsNode *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->name = dupstr(name ? name : "");
    n->dir = dir;
    n->kind = dir ? MK_OTHER : media_kind(n->name);
    return n;
}

FsNode *fs_add(FsNode *parent, const char *name, bool dir, int64_t size, int64_t mtime)
{
    FsNode *n = fs_new(name, dir);
    if (!n) return NULL;
    n->size = size;
    n->mtime = mtime;
    n->parent = parent;
    if (parent->nkids == parent->cap) {
        int cap = parent->cap ? parent->cap * 2 : 8;
        FsNode **k = realloc(parent->kids, sizeof *k * (size_t)cap);
        if (!k) { fs_free(n); return NULL; }
        parent->kids = k;
        parent->cap = cap;
    }
    parent->kids[parent->nkids++] = n;
    return n;
}

FsNode *fs_child(const FsNode *n, const char *name)
{
    for (int i = 0; i < n->nkids; i++) {
        const char *a = n->kids[i]->name, *b = name;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return n->kids[i];
    }
    return NULL;
}

FsNode *fs_add_path(FsNode *root, const char *rel, int64_t size, int64_t mtime)
{
    FsNode *cur = root;
    char part[512];
    const char *p = rel;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len >= sizeof part) len = sizeof part - 1;
        memcpy(part, p, len);
        part[len] = 0;
        bool last = !slash;
        if (len) {
            FsNode *next = NULL;
            for (int i = 0; i < cur->nkids; i++)
                if (!strcmp(cur->kids[i]->name, part)) { next = cur->kids[i]; break; }
            if (!next) next = fs_add(cur, part, !last, last ? size : 0, mtime);
            if (!next) return NULL;
            cur = next;
        }
        if (last) break;
        p = slash + 1;
    }
    return cur;
}

void fs_free(FsNode *n)
{
    if (!n) return;
    for (int i = 0; i < n->nkids; i++) fs_free(n->kids[i]);
    free(n->kids);
    free(n->name);
    free(n);
}

int natcmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0' && isdigit((unsigned char)a[1])) a++;
            while (*b == '0' && isdigit((unsigned char)b[1])) b++;
            const char *ea = a, *eb = b;
            while (isdigit((unsigned char)*ea)) ea++;
            while (isdigit((unsigned char)*eb)) eb++;
            if (ea - a != eb - b) return (int)((ea - a) - (eb - b));
            for (; a < ea; a++, b++)
                if (*a != *b) return *a - *b;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int node_cmp(const void *pa, const void *pb)
{
    const FsNode *a = *(const FsNode *const *)pa, *b = *(const FsNode *const *)pb;
    if (a->dir != b->dir) return a->dir ? -1 : 1;
    int c = natcmp(a->name, b->name);
    return c ? c : strcmp(a->name, b->name);
}

void fs_sort(FsNode *n, bool recursive)
{
    if (n->nkids > 1) qsort(n->kids, (size_t)n->nkids, sizeof *n->kids, node_cmp);
    if (recursive)
        for (int i = 0; i < n->nkids; i++)
            if (n->kids[i]->dir) fs_sort(n->kids[i], true);
}

char *fs_relpath(const FsNode *n, char *buf, size_t cap)
{
    const FsNode *chain[64];
    int depth = 0;
    for (const FsNode *p = n; p && p->parent && depth < 64; p = p->parent) chain[depth++] = p;
    size_t len = 0;
    buf[0] = 0;
    for (int i = depth - 1; i >= 0; i--) {
        size_t nl = strlen(chain[i]->name);
        if (len + nl + 2 > cap) break;
        if (len) buf[len++] = '/';
        memcpy(buf + len, chain[i]->name, nl);
        len += nl;
        buf[len] = 0;
    }
    return buf;
}

int fs_count(const FsNode *n, MediaKind kind)
{
    if (!n->dir) return n->kind == kind;
    int c = 0;
    for (int i = 0; i < n->nkids; i++) c += fs_count(n->kids[i], kind);
    return c;
}

int64_t fs_newest(const FsNode *n)
{
    int64_t t = n->mtime;
    for (int i = 0; i < n->nkids; i++) {
        int64_t k = fs_newest(n->kids[i]);
        if (k > t) t = k;
    }
    return t;
}

const FsNode *fs_find_art(const FsNode *dir)
{
    static const char *const names[] = { "poster", "cover", "folder", "front", "album", "show", NULL };
    if (!dir || !dir->dir) return NULL;
    for (int k = 0; names[k]; k++)
        for (int i = 0; i < dir->nkids; i++) {
            const FsNode *c = dir->kids[i];
            if (c->dir || c->kind != MK_IMAGE) continue;
            size_t nl = strlen(names[k]);
            if (strlen(c->name) > nl && c->name[nl] == '.') {
                bool eq = true;
                for (size_t j = 0; j < nl; j++)
                    if (tolower((unsigned char)c->name[j]) != names[k][j]) { eq = false; break; }
                if (eq) return c;
            }
        }
    return NULL;
}

FsNode *fs_find(FsNode *root, const char *rel)
{
    FsNode *cur = root;
    char part[512];
    const char *p = rel;
    while (cur && *p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len >= sizeof part) return NULL;
        memcpy(part, p, len);
        part[len] = 0;
        FsNode *next = NULL;
        for (int i = 0; i < cur->nkids; i++)
            if (!strcmp(cur->kids[i]->name, part)) { next = cur->kids[i]; break; }
        cur = next;
        if (!slash) break;
        p = slash + 1;
    }
    return cur;
}
