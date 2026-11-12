#include "nsfw.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Entry {
    int root, scope, state;
    int64_t size;
    char *path;
} Entry;

struct NsfwIndex {
    Entry *e;
    int n, cap;
    int *slots;               /* open addressing: entry index + 1, 0 = empty, -1 = removed */
    size_t nslots;
    NsfwConfig cfg;
    bool (*exists)(void *ud, int root, const char *rel);
    void *exists_ud;
};

static uint32_t hash_key(int root, int scope, const char *path, int64_t size)
{
    uint32_t h = 2166136261u ^ (uint32_t)(root * 31 + scope);
    for (const char *p = path; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    h ^= (uint32_t)size; h *= 16777619u;
    h ^= (uint32_t)((uint64_t)size >> 32); h *= 16777619u;
    return h;
}

static void free_entries(NsfwIndex *x)
{
    for (int i = 0; i < x->n; i++) free(x->e[i].path);
    x->n = 0;
}

static void rebuild_slots(NsfwIndex *x)
{
    size_t want = 64;
    while (want < (size_t)(x->n + 1) * 2) want *= 2;
    free(x->slots);
    x->slots = (int *)calloc(want, sizeof *x->slots);
    x->nslots = x->slots ? want : 0;
    for (int i = 0; i < x->n && x->nslots; i++) {
        size_t s = hash_key(x->e[i].root, x->e[i].scope, x->e[i].path, x->e[i].size) & (x->nslots - 1);
        while (x->slots[s]) s = (s + 1) & (x->nslots - 1);
        x->slots[s] = i + 1;
    }
}

NsfwIndex *nsfw_index_new(void)
{
    NsfwIndex *x = (NsfwIndex *)calloc(1, sizeof *x);
    if (x) rebuild_slots(x);
    return x;
}

void nsfw_index_free(NsfwIndex *x)
{
    if (!x) return;
    free_entries(x);
    free(x->e);
    free(x->slots);
    free(x);
}

void nsfw_index_config(NsfwIndex *x, const NsfwConfig *cfg)
{
    if (x && cfg) x->cfg = *cfg;
}

void nsfw_index_set_exists_check(NsfwIndex *x, bool (*exists)(void *ud, int root, const char *rel), void *ud)
{
    if (!x) return;
    x->exists = exists;
    x->exists_ud = ud;
}

static int find_entry(const NsfwIndex *x, int root, int scope, const char *path, int64_t size)
{
    if (!x->nslots) return -1;
    size_t s = hash_key(root, scope, path, size) & (x->nslots - 1);
    while (x->slots[s]) {
        int i = x->slots[s] - 1;
        if (i >= 0) {
            const Entry *e = &x->e[i];
            if (e->root == root && e->scope == scope && e->size == size && !strcmp(e->path, path)) return i;
        }
        s = (s + 1) & (x->nslots - 1);
    }
    return -1;
}

static bool add_entry(NsfwIndex *x, int root, int scope, const char *path, int64_t size, int state)
{
    if (scope == NSFW_SCOPE_FOLDER) size = 0;
    int i = find_entry(x, root, scope, path, size);
    if (i >= 0) { x->e[i].state = state; return true; }
    if (x->n == x->cap) {
        int nc = x->cap ? x->cap * 2 : 64;
        Entry *ne = (Entry *)realloc(x->e, (size_t)nc * sizeof *ne);
        if (!ne) return false;
        x->e = ne;
        x->cap = nc;
    }
    size_t pl = strlen(path) + 1;
    char *copy = (char *)malloc(pl);
    if (!copy) return false;
    memcpy(copy, path, pl);
    x->e[x->n] = (Entry){ root, scope, state, size, copy };
    x->n++;
    rebuild_slots(x);
    return true;
}

static void remove_entry(NsfwIndex *x, int i)
{
    free(x->e[i].path);
    x->e[i] = x->e[x->n - 1];
    x->n--;
    rebuild_slots(x);
}

bool nsfw_index_load(NsfwIndex *x, History *h)
{
    if (!x) return false;
    free_entries(x);
    int n = 0;
    NsfwTagRow *rows = history_nsfw_load(h, &n);
    for (int i = 0; i < n; i++) add_entry(x, rows[i].root, rows[i].scope, rows[i].path, rows[i].size, rows[i].state);
    free(rows);
    rebuild_slots(x);
    return true;
}

/* A file tag that belongs to a file which moved: same root and size, a different path, an old path that is
 * gone from the library, and exactly one such tag. */
static int moved_candidate(const NsfwIndex *x, int root, const char *rel, int64_t size)
{
    if (!x->exists) return -1;
    int found = -1, count = 0;
    for (int i = 0; i < x->n; i++) {
        const Entry *e = &x->e[i];
        if (e->scope != NSFW_SCOPE_FILE || e->root != root || e->size != size || !strcmp(e->path, rel)) continue;
        if (x->exists(x->exists_ud, root, e->path)) continue;
        found = i;
        count++;
    }
    return count == 1 ? found : -1;
}

bool nsfw_tag(NsfwIndex *x, History *h, int root, int scope, const char *rel, int64_t size, int state)
{
    if (!x || !rel || !*rel) return false;
    if (scope == NSFW_SCOPE_FOLDER) size = 0;
    else {
        int mv = find_entry(x, root, scope, rel, size) < 0 ? moved_candidate(x, root, rel, size) : -1;
        if (mv >= 0) {                                        /* the tag follows the moved file */
            if (h) history_nsfw_delete(h, root, scope, x->e[mv].path, size);
            remove_entry(x, mv);
        }
    }
    if (!add_entry(x, root, scope, rel, size, state ? 1 : 0)) return false;
    if (h) history_nsfw_put(h, root, scope, rel, size, state ? 1 : 0);
    return true;
}

bool nsfw_untag(NsfwIndex *x, History *h, int root, int scope, const char *rel, int64_t size)
{
    if (!x || !rel) return false;
    if (scope == NSFW_SCOPE_FOLDER) size = 0;
    int i = find_entry(x, root, scope, rel, size);
    if (i < 0) return false;
    remove_entry(x, i);
    if (h) history_nsfw_delete(h, root, scope, rel, size);
    return true;
}

int nsfw_tag_count(const NsfwIndex *x) { return x ? x->n : 0; }

int nsfw_tag_count_state(const NsfwIndex *x, int state)
{
    int c = 0;
    for (int i = 0; x && i < x->n; i++) c += x->e[i].state == state;
    return c;
}

/* ---- the NSFW folder: grouping and paths ------------------------------- */

int nsfw_item_count(const FsNode *n)
{
    if (!n) return 0;
    if (!n->dir) return n->kind == MK_VIDEO || n->kind == MK_AUDIO || n->kind == MK_AUDIO_MPV || n->kind == MK_BOOK;
    int c = 0;
    for (int i = 0; i < n->nkids; i++) c += nsfw_item_count(n->kids[i]);
    return c;
}

int nsfw_groups(const FsNode *dir, NsfwGroup *out, int max)
{
    if (!dir || !dir->dir || !out) return 0;
    int n = 0, loose = 0;
    for (int i = 0; i < dir->nkids; i++) {
        const FsNode *k = dir->kids[i];
        if (k->dir) {
            int c = nsfw_item_count(k);
            if (c > 0 && n < max) out[n++] = (NsfwGroup){ k, k->name, c };
        } else if (nsfw_item_count(k)) {
            loose++;
        }
    }
    if (loose > 0 && n < max) out[n++] = (NsfwGroup){ NULL, "Unsorted", loose };
    return n;
}

static size_t trim_len(const char *p)
{
    size_t n = strlen(p);
    while (n > 1 && (p[n - 1] == '/' || p[n - 1] == '\\')) n--;
    return n;
}

static bool same_char(char a, char b)
{
    if ((a == '/' || a == '\\') && (b == '/' || b == '\\')) return true;
#ifdef _WIN32
    return tolower((unsigned char)a) == tolower((unsigned char)b);
#else
    return a == b;
#endif
}

bool nsfw_path_inside(const char *dir, const char *path)
{
    if (!dir || !path || !*dir || !*path) return false;
    size_t dl = trim_len(dir), pl = trim_len(path);
    if (pl <= dl) return false;
    for (size_t i = 0; i < dl; i++)
        if (!same_char(dir[i], path[i])) return false;
    return path[dl] == '/' || path[dl] == '\\' || dl == 1;
}

bool nsfw_path_same(const char *a, const char *b)
{
    if (!a || !b || !*a || !*b) return false;
    size_t al = trim_len(a), bl = trim_len(b);
    if (al != bl) return false;
    for (size_t i = 0; i < al; i++)
        if (!same_char(a[i], b[i])) return false;
    return true;
}

const char *nsfw_why_text(int why)
{
    switch (why) {
    case NSFW_WHY_ITEM: return "Marked";
    case NSFW_WHY_FOLDER: return "In a folder you marked";
    case NSFW_WHY_LIBRARY: return "Library folder marked NSFW";
    case NSFW_WHY_DIR: return "In your NSFW folder";
    default: return "";
    }
}

/* ---- the decision ------------------------------------------------------ */

static bool decide(int state, int kind_why, int *why, bool *out)
{
    if (why) *why = state ? kind_why : NSFW_WHY_EXPLICIT_NOT;
    *out = state != 0;
    return true;
}

bool nsfw_hidden(const NsfwIndex *x, int root, const char *rel, bool is_dir, int64_t size, int *why)
{
    if (why) *why = NSFW_WHY_NONE;
    if (!x || !x->cfg.enabled || !rel) return false;
    bool result = false;
    int i;
    /* everything in the chosen NSFW folder is NSFW: no tag changes that */
    if (x->cfg.dir_set && root == x->cfg.dir_root) {
        if (why) *why = NSFW_WHY_DIR;
        return true;
    }
    if (!is_dir) {
        i = find_entry(x, root, NSFW_SCOPE_FILE, rel, size);
        if (i < 0) i = moved_candidate(x, root, rel, size);
        if (i >= 0 && decide(x->e[i].state, NSFW_WHY_ITEM, why, &result)) return result;
    } else {
        i = find_entry(x, root, NSFW_SCOPE_FOLDER, rel, 0);
        if (i >= 0 && decide(x->e[i].state, NSFW_WHY_ITEM, why, &result)) return result;
    }
    /* parent folders, nearest first */
    char buf[1024];
    size_t len = strlen(rel);
    if (len >= sizeof buf) return false;
    memcpy(buf, rel, len + 1);
    for (char *slash = strrchr(buf, '/'); slash; slash = strrchr(buf, '/')) {
        *slash = 0;
        i = find_entry(x, root, NSFW_SCOPE_FOLDER, buf, 0);
        if (i >= 0 && decide(x->e[i].state, NSFW_WHY_FOLDER, why, &result)) return result;
    }
    /* nothing was tagged: a library folder marked NSFW */
    if (root >= 0 && root < 32 && (x->cfg.root_mask >> root) & 1u) {
        if (why) *why = NSFW_WHY_LIBRARY;
        return true;
    }
    return false;
}

/* ---- import and export ------------------------------------------------- */

int nsfw_export(const NsfwIndex *x, FILE *f)
{
    if (!x || !f) return 0;
    fprintf(f, "THC NSFW tags 1\n");
    for (int i = 0; i < x->n; i++) {
        const Entry *e = &x->e[i];
        fprintf(f, "%d\t%c\t%d\t%lld\t%s\n", e->root, e->scope == NSFW_SCOPE_FOLDER ? 'd' : 'f', e->state, (long long)e->size, e->path);
    }
    return x->n;
}

int nsfw_import(NsfwIndex *x, History *h, FILE *f)
{
    if (!x || !f) return 0;
    char line[1400];
    if (!fgets(line, sizeof line, f) || strncmp(line, "THC NSFW tags", 13) != 0) return 0;
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        int root, state;
        char kind;
        long long size;
        char *tok[5];
        int nt = 0;
        char *p = line;
        while (nt < 5) {
            tok[nt++] = p;
            char *tab = strchr(p, '\t');
            if (!tab || nt == 5) break;
            *tab = 0;
            p = tab + 1;
        }
        if (nt < 5) continue;
        root = atoi(tok[0]);
        kind = tok[1][0];
        state = atoi(tok[2]);
        size = atoll(tok[3]);
        const char *rel = tok[4];
        if (root < 0 || root > 31 || (kind != 'f' && kind != 'd') || !*rel) continue;
        /* a path that climbs out of the library is not a library path */
        if (strstr(rel, "..") || rel[0] == '/' || strchr(rel, '\\')) continue;
        if (nsfw_tag(x, h, root, kind == 'd' ? NSFW_SCOPE_FOLDER : NSFW_SCOPE_FILE, rel, size, state)) n++;
    }
    return n;
}

/* ---- filtered trees ---------------------------------------------------- */

static FsNode *filter_rec(const FsNode *src, FsNode *parent, bool (*hidden)(void *, const FsNode *), void *ud, bool want, bool is_root)
{
    if (!src->dir) {
        if (hidden(ud, src) != want) return NULL;
        return fs_add(parent, src->name, false, src->size, src->mtime);
    }
    FsNode *copy = is_root ? fs_new(src->name, true) : fs_add(parent, src->name, true, 0, src->mtime);
    if (!copy) return NULL;
    copy->mtime = src->mtime;
    copy->kind = src->kind;
    for (int i = 0; i < src->nkids; i++) {
        const FsNode *k = src->kids[i];
        FsNode *kid = filter_rec(k, copy, hidden, ud, want, false);
        if (kid) kid->kind = k->kind;
    }
    if (!is_root && copy->nkids == 0) {
        /* a folder with nothing kept goes away, unless it was empty to begin with and is a normal-portal folder */
        bool was_empty = src->nkids == 0;
        if (want || !was_empty || hidden(ud, src)) {
            /* remove the empty copy from its parent */
            for (int j = parent->nkids - 1; j >= 0; j--)
                if (parent->kids[j] == copy) {
                    memmove(&parent->kids[j], &parent->kids[j + 1], (size_t)(parent->nkids - j - 1) * sizeof *parent->kids);
                    parent->nkids--;
                    break;
                }
            fs_free(copy);
            return NULL;
        }
    }
    return copy;
}

FsNode *fs_filter(const FsNode *src, bool (*hidden)(void *ud, const FsNode *n), void *ud, bool want_hidden)
{
    if (!src) return NULL;
    return filter_rec(src, NULL, hidden, ud, want_hidden, true);
}

bool nsfw_record_allowed(const NsfwIndex *x, bool history_on_for_kind, bool record_nsfw, int root, const char *rel, int64_t size)
{
    if (!history_on_for_kind) return false;
    if (record_nsfw || !x) return true;
    return !nsfw_hidden(x, root, rel, false, size, NULL);
}

void nsfw_thumb_name(const char *hexkey, bool nsfw, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s", nsfw ? "nsfw/" : "", hexkey);
}
