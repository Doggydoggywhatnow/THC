/* thc-inspect: check what THC really does. A separate, small program that only reads.
 *
 * It never connects to the network, never writes to THC's data and never touches your media files
 * (--snapshot writes one file, the one you name). It links only SQLite and the C library.
 *
 * Exit code: 0 = nothing found, 1 = findings, 2 = error. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <sqlite3.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#ifndef INSPECT_VERSION
#define INSPECT_VERSION "0.3.0"
#endif

/* ------------------------------------------------------------------------ */
/* Report: sections of entries, notes and findings, as text or JSON            */
/* ------------------------------------------------------------------------ */

enum { L_ENTRY, L_NOTE, L_FINDING };
typedef struct Line { int type; char *key; char *text; } Line;
typedef struct Section { char *name; Line *lines; int n, cap; } Section;

static struct {
    int json, verbose, titles;
    Section *secs;
    int nsec, capsec;
    int findings;
} R;

static char *dupstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void section(const char *name)
{
    if (R.nsec == R.capsec) {
        R.capsec = R.capsec ? R.capsec * 2 : 8;
        R.secs = realloc(R.secs, (size_t)R.capsec * sizeof *R.secs);
    }
    Section *s = &R.secs[R.nsec++];
    memset(s, 0, sizeof *s);
    s->name = dupstr(name);
}

static void add_line(int type, const char *key, const char *text)
{
    Section *s = &R.secs[R.nsec - 1];
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->lines = realloc(s->lines, (size_t)s->cap * sizeof *s->lines);
    }
    s->lines[s->n].type = type;
    s->lines[s->n].key = key ? dupstr(key) : NULL;
    s->lines[s->n].text = dupstr(text);
    s->n++;
    if (type == L_FINDING) R.findings++;
}

static void kv(const char *key, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    add_line(L_ENTRY, key, buf);
}

static void note(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    add_line(L_NOTE, NULL, buf);
}

static void finding(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    add_line(L_FINDING, NULL, buf);
}

static void jstr(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void print_report(FILE *f)
{
    if (R.json) {
        fprintf(f, "{\"tool\":\"thc-inspect\",\"version\":\"%s\",\"sections\":[", INSPECT_VERSION);
        for (int i = 0; i < R.nsec; i++) {
            Section *s = &R.secs[i];
            fprintf(f, "%s{\"name\":", i ? "," : "");
            jstr(f, s->name);
            const char *names[3] = { "entries", "notes", "findings" };
            for (int t = 0; t < 3; t++) {
                fprintf(f, ",\"%s\":[", names[t]);
                int first = 1;
                for (int j = 0; j < s->n; j++) {
                    if (s->lines[j].type != t) continue;
                    if (!first) fputc(',', f);
                    first = 0;
                    if (t == L_ENTRY) {
                        fputs("{\"key\":", f);
                        jstr(f, s->lines[j].key ? s->lines[j].key : "");
                        fputs(",\"value\":", f);
                        jstr(f, s->lines[j].text);
                        fputc('}', f);
                    } else {
                        jstr(f, s->lines[j].text);
                    }
                }
                fputc(']', f);
            }
            fputc('}', f);
        }
        fprintf(f, "],\"findings\":%d,\"result\":\"%s\"}\n", R.findings, R.findings ? "findings" : "pass");
        return;
    }
    for (int i = 0; i < R.nsec; i++) {
        Section *s = &R.secs[i];
        fprintf(f, "== %s ==\n", s->name);
        for (int j = 0; j < s->n; j++) {
            Line *l = &s->lines[j];
            if (l->type == L_ENTRY) fprintf(f, "  %s%s%s\n", l->key ? l->key : "", l->key ? ": " : "", l->text);
            else if (l->type == L_NOTE) fprintf(f, "  - %s\n", l->text);
            else fprintf(f, "  ! %s\n", l->text);
        }
        fputc('\n', f);
    }
    if (R.findings) fprintf(f, "Result: %d finding%s. See the lines marked with !\n", R.findings, R.findings == 1 ? "" : "s");
    else fprintf(f, "Result: pass. Nothing to report.\n");
}

/* ------------------------------------------------------------------------ */
/* Files                                                                       */
/* ------------------------------------------------------------------------ */

typedef void (*WalkFn)(const char *full, const char *rel, const struct stat *st, void *ud);

static int is_dir(const struct stat *st) { return (st->st_mode & S_IFMT) == S_IFDIR; }
static int is_reg(const struct stat *st) { return (st->st_mode & S_IFMT) == S_IFREG; }

static void walk(const char *dir, const char *rel, WalkFn fn, void *ud)
{
#ifdef _WIN32
    char pat[1200];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = fd.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        char full[1200], r[1200];
        snprintf(full, sizeof full, "%s\\%s", dir, name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (is_dir(&st)) walk(full, r, fn, ud);
        else fn(full, r, &st, ud);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        char full[1200], r[1200];
        snprintf(full, sizeof full, "%s/%s", dir, name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", name);
        struct stat st;
        if (lstat(full, &st) != 0) continue;     /* symbolic links are not followed */
        if (is_dir(&st)) walk(full, r, fn, ud);
        else if (is_reg(&st)) fn(full, r, &st, ud);
    }
    closedir(d);
#endif
}

static void fmt_time(time_t t, char *out, size_t cap)
{
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(out, cap, "%Y-%m-%d %H:%M:%S UTC", &tmv);
}

static void fmt_size(long long n, char *out, size_t cap)
{
    if (n < 1024) snprintf(out, cap, "%lld B", n);
    else if (n < 1024 * 1024) snprintf(out, cap, "%.1f KB", (double)n / 1024.0);
    else snprintf(out, cap, "%.1f MB", (double)n / (1024.0 * 1024.0));
}

typedef struct FilesCtx { long long total; int count; int top_files; } FilesCtx;

static void files_cb(const char *full, const char *rel, const struct stat *st, void *ud)
{
    (void)full;
    FilesCtx *c = ud;
    c->total += (long long)st->st_size;
    c->count++;
    /* thumbnails can be thousands of files: summarize them unless -v */
    if (!strncmp(rel, "screenshots/nsfw/", 17)) return;       /* counted, never listed: the names could say what they show */
    if (!R.verbose && (!strncmp(rel, "thumbs/", 7) || !strncmp(rel, "cache/", 6))) return;
    char sz[32], tm[48], line[200];
    fmt_size((long long)st->st_size, sz, sizeof sz);
    fmt_time(st->st_mtime, tm, sizeof tm);
    snprintf(line, sizeof line, "%s, modified %s", sz, tm);
    kv(rel, "%s", line);
}

static void thumbs_cb(const char *full, const char *rel, const struct stat *st, void *ud)
{
    (void)full; (void)rel;
    long long *v = ud;
    v[0]++;
    v[1] += (long long)st->st_size;
}

static void report_files(const char *data)
{
    section("Files THC has written");
    kv("data folder", "%s", data);
    struct stat st;
    if (stat(data, &st) != 0 || !is_dir(&st)) {
        note("The folder does not exist (THC has not run yet, or --data points elsewhere).");
        return;
    }
    FilesCtx c = { 0, 0, 0 };
    walk(data, "", files_cb, &c);
    if (!R.verbose) {
        long long v[2] = { 0, 0 };
        char p[1200];
        snprintf(p, sizeof p, "%s/thumbs", data);
        walk(p, "", thumbs_cb, v);
        if (v[0]) {
            char sz[32];
            fmt_size(v[1], sz, sizeof sz);
            kv("thumbs/", "%lld thumbnail files, %s (use -v to list them)", v[0], sz);
        }
    }
    char sz[32];
    fmt_size(c.total, sz, sizeof sz);
    kv("total", "%d files, %s", c.count, sz);
}

/* ------------------------------------------------------------------------ */
/* History database                                                            */
/* ------------------------------------------------------------------------ */

/* ---- NSFW (#13): the tool counts the marks and never prints the name of a marked item ---- */

static int g_nsfw_rules_on;
static char g_nsfw_rules[300];
static int g_nsfw_roots;

static int glob_match(const char *p, const char *s)
{
    while (*p) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;
            for (; *s; s++) if (glob_match(p, s)) return 1;
            return 0;
        }
        if (!*s) return 0;
        if (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s)) return 0;
        p++; s++;
    }
    return !*s;
}

static int rules_hit(const char *rel)
{
    if (!g_nsfw_rules_on || !g_nsfw_rules[0]) return 0;
    char part[256];
    const char *p = rel;
    while (*p) {
        const char *sl = strchr(p, '/');
        size_t n = sl ? (size_t)(sl - p) : strlen(p);
        if (n && n < sizeof part) {
            memcpy(part, p, n);
            part[n] = 0;
            const char *r = g_nsfw_rules;
            while (*r) {
                const char *bar = strchr(r, '|');
                size_t rl = bar ? (size_t)(bar - r) : strlen(r);
                char pat[128];
                while (rl && isspace((unsigned char)*r)) { r++; rl--; }
                while (rl && isspace((unsigned char)r[rl - 1])) rl--;
                if (rl && rl < sizeof pat) {
                    memcpy(pat, r, rl);
                    pat[rl] = 0;
                    if (glob_match(pat, part)) return 1;
                }
                if (!bar) break;
                r = bar + 1;
            }
        }
        if (!sl) break;
        p = sl + 1;
    }
    return 0;
}

static int tag_state(sqlite3 *db, const char *sql, const char *rel, long long size, int use_size)
{
    sqlite3_stmt *q;
    int state = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &q, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(q, 1, rel, -1, SQLITE_TRANSIENT);
    if (use_size) sqlite3_bind_int64(q, 2, size);
    if (sqlite3_step(q) == SQLITE_ROW) state = sqlite3_column_int(q, 0);
    sqlite3_finalize(q);
    return state;
}

/* Would THC treat this history row as NSFW? Conservative: any root, any rule. */
static int row_is_nsfw(sqlite3 *db, const char *rel, long long size)
{
    int st = tag_state(db, "SELECT state FROM nsfw_tags WHERE scope=0 AND rel_path=?1 AND size=?2 LIMIT 1", rel, size, 1);
    if (st >= 0) return st == 1;
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", rel);
    for (char *sl = strrchr(buf, '/'); sl; sl = strrchr(buf, '/')) {
        *sl = 0;
        st = tag_state(db, "SELECT state FROM nsfw_tags WHERE scope=1 AND rel_path=?1 LIMIT 1", buf, 0, 0);
        if (st >= 0) return st == 1;
    }
    return rules_hit(rel);
}

static int setting_value(const char *data, const char *key, char *out, size_t cap);

static int setting_value(const char *data, const char *key, char *out, size_t cap);

static void report_history(const char *data)
{
    section("History database");
    char path[1200];
    snprintf(path, sizeof path, "%s/history.db", data);
    struct stat st;
    if (stat(path, &st) != 0) {
        note("No history.db here.");
        return;
    }
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        finding("history.db could not be opened: %s", db ? sqlite3_errmsg(db) : "out of memory");
        if (db) sqlite3_close(db);
        return;
    }
    sqlite3_stmt *q;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &q, NULL) == SQLITE_OK) {
        if (sqlite3_step(q) == SQLITE_ROW) kv("schema version", "%d", sqlite3_column_int(q, 0));
        sqlite3_finalize(q);
    }
    char tables[64][64];
    int nt = 0;
    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name",
                           -1, &q, NULL) == SQLITE_OK) {
        while (sqlite3_step(q) == SQLITE_ROW && nt < 64) {
            snprintf(tables[nt], sizeof tables[nt], "%s", (const char *)sqlite3_column_text(q, 0));
            nt++;
        }
        sqlite3_finalize(q);
    }
    for (int i = 0; i < nt; i++) {
        char sql[160];
        snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM \"%s\"", tables[i]);
        if (sqlite3_prepare_v2(db, sql, -1, &q, NULL) == SQLITE_OK) {
            if (sqlite3_step(q) == SQLITE_ROW) kv(tables[i], "%lld rows", (long long)sqlite3_column_int64(q, 0));
            sqlite3_finalize(q);
        }
    }
    {   /* NSFW: how many marks there are, never what they are */
        int marks = 0, nots = 0;
        if (sqlite3_prepare_v2(db, "SELECT COALESCE(SUM(state=1),0), COALESCE(SUM(state=0),0) FROM nsfw_tags", -1, &q, NULL) == SQLITE_OK) {
            if (sqlite3_step(q) == SQLITE_ROW) { marks = sqlite3_column_int(q, 0); nots = sqlite3_column_int(q, 1); }
            sqlite3_finalize(q);
            kv("NSFW marks", "%d marked, %d explicitly not NSFW", marks, nots);
        }
        char nd[1200];
        snprintf(nd, sizeof nd, "%s/thumbs/nsfw", data);
        struct stat nst;
        if (stat(nd, &nst) == 0 && is_dir(&nst)) {
            long long v[2] = { 0, 0 };
            walk(nd, "", thumbs_cb, v);
            kv("thumbs/nsfw/", v[0] ? "%lld files (THC removes them when it exits)" : "empty", v[0]);
        } else {
            kv("thumbs/nsfw/", "empty");
        }
    }
    if (R.titles) {
        char v[16];
        if (setting_value(data, "nsfw_rules_enabled", v, sizeof v)) g_nsfw_rules_on = strcmp(v, "0") && strcmp(v, "false");
        setting_value(data, "nsfw_rules", g_nsfw_rules, sizeof g_nsfw_rules);
        if (setting_value(data, "nsfw_roots", v, sizeof v)) g_nsfw_roots = atoi(v);
        if (g_nsfw_roots) {
            note("Titles are not listed: whole library folders are marked NSFW, and history does not say which folder a row came from.");
        } else if (sqlite3_prepare_v2(db, "SELECT path, size, position, watched FROM history ORDER BY updated DESC LIMIT 50", -1, &q, NULL) ==
                   SQLITE_OK) {
            int hidden = 0;
            while (sqlite3_step(q) == SQLITE_ROW) {
                const char *p = (const char *)sqlite3_column_text(q, 0);
                if (row_is_nsfw(db, p, sqlite3_column_int64(q, 1))) { hidden++; continue; }
                kv("history", "%s (at %.0f s%s)", p, sqlite3_column_double(q, 2), sqlite3_column_int(q, 3) ? ", watched" : "");
            }
            sqlite3_finalize(q);
            if (hidden) note("%d NSFW entr%s left out. THC-inspect never prints the names of NSFW items.", hidden, hidden == 1 ? "y" : "ies");
        }
    } else {
        note("Titles are hidden. Add --show-titles to list the 50 most recent entries.");
    }
    sqlite3_close(db);
}

/* ------------------------------------------------------------------------ */
/* Program contents: does the THC executable import networking functions?      */
/* ------------------------------------------------------------------------ */

static const char *const k_net_symbols[] = {
    "socket", "connect", "bind", "listen", "accept", "accept4", "getaddrinfo", "gethostbyname", "gethostbyname2",
    "gethostbyaddr", "getnameinfo", "send", "sendto", "sendmsg", "recv", "recvfrom", "recvmsg", "getpeername",
    "WSAStartup", "WSASocketA", "WSASocketW", "WSAConnect", "InternetOpenA", "InternetOpenW", "InternetOpenUrlA",
    "WinHttpOpen", "URLDownloadToFileA", "URLDownloadToFileW", "curl_easy_init", "curl_easy_perform", "SSL_connect",
    "SSL_new", "gnutls_init", "CFNetworkCopySystemProxySettings", NULL
};
static const char *const k_net_libs[] = { "libcurl", "libssl", "libcrypto", "libgnutls", "libnghttp2", "libsoup", "libgio",
                                          "libwininet", "winhttp", "ws2_32", "wsock32", "libresolv", NULL };

static unsigned char *read_whole(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 512L * 1024 * 1024) { fclose(f); return NULL; }
    unsigned char *b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)n;
    return b;
}

static int in_list(const char *const *list, const char *name)
{
    for (int i = 0; list[i]; i++) if (!strcmp(list[i], name)) return 1;
    return 0;
}

static uint64_t rd(const unsigned char *b, size_t off, int bytes)
{
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; i--) v = (v << 8) | b[off + (size_t)i];
    return v;
}

/* ELF64, little endian: undefined dynamic symbols and needed libraries. Returns -1 if not ELF64. */
static int elf_imports(const unsigned char *b, size_t n, char found[][64], int maxf, char libs[][64], int maxl, int *nlibs)
{
    *nlibs = 0;
    if (n < 64 || memcmp(b, "\x7f""ELF", 4) != 0 || b[4] != 2 || b[5] != 1) return -1;
    uint64_t shoff = rd(b, 40, 8);
    unsigned shentsize = (unsigned)rd(b, 58, 2), shnum = (unsigned)rd(b, 60, 2);
    if (!shoff || shentsize < 64 || shoff + (uint64_t)shnum * shentsize > n) return -1;
    int nf = 0;
    for (unsigned i = 0; i < shnum; i++) {
        size_t sh = (size_t)shoff + (size_t)i * shentsize;
        unsigned type = (unsigned)rd(b, sh + 4, 4);
        if (type != 11 && type != 6) continue;           /* SHT_DYNSYM, SHT_DYNAMIC */
        uint64_t off = rd(b, sh + 24, 8), size = rd(b, sh + 32, 8), ent = rd(b, sh + 56, 8);
        unsigned link = (unsigned)rd(b, sh + 40, 4);
        if (link >= shnum || off + size > n || !ent) continue;
        size_t ssh = (size_t)shoff + (size_t)link * shentsize;
        uint64_t stroff = rd(b, ssh + 24, 8), strsize = rd(b, ssh + 32, 8);
        if (stroff + strsize > n) continue;
        for (uint64_t k = 0; k + ent <= size; k += ent) {
            size_t e = (size_t)(off + k);
            if (type == 11) {
                uint32_t nameoff = (uint32_t)rd(b, e, 4);
                unsigned shndx = (unsigned)rd(b, e + 6, 2);
                if (shndx != 0 || nameoff >= strsize) continue;     /* defined here, not imported */
                const char *nm = (const char *)b + stroff + nameoff;
                if (in_list(k_net_symbols, nm) && nf < maxf) snprintf(found[nf++], 64, "%s", nm);
            } else {
                if ((int64_t)rd(b, e, 8) == 1 && *nlibs < maxl) {   /* DT_NEEDED */
                    uint64_t no = rd(b, e + 8, 8);
                    if (no < strsize) snprintf(libs[(*nlibs)++], 64, "%s", (const char *)b + stroff + no);
                }
            }
        }
    }
    return nf;
}

/* PE and Mach-O: look for the imported names as strings (approximate, and said so). */
static int string_scan(const unsigned char *b, size_t n, char found[][64], int maxf)
{
    int nf = 0;
    for (int i = 0; k_net_symbols[i] && nf < maxf; i++) {
        size_t L = strlen(k_net_symbols[i]);
        for (size_t p = 1; p + L < n; p++) {
            if (b[p] != (unsigned char)k_net_symbols[i][0] || memcmp(b + p, k_net_symbols[i], L) != 0 || b[p + L] != 0) continue;
            unsigned char pv = b[p - 1];
            int ok = !(isalnum(pv) || pv == '_');
            if (!ok && pv == '_' && p >= 2 && !(isalnum(b[p - 2]) || b[p - 2] == '_')) ok = 1;   /* Mach-O "_name" */
            if (ok) { snprintf(found[nf++], 64, "%s", k_net_symbols[i]); break; }
        }
    }
    return nf;
}

static void report_program(const char *exe, const char *thc_pid_exe)
{
    section("Program contents");
    const char *path = exe ? exe : thc_pid_exe;
    if (!path || !*path) {
        note("THC's program was not found. Pass --exe <path to thc>, or start THC and use --pid.");
        return;
    }
    kv("program", "%s", path);
    size_t n = 0;
    unsigned char *b = read_whole(path, &n);
    if (!b) {
        note("Could not read the program file.");
        return;
    }
    char found[40][64], libs[64][64];
    int nlibs = 0;
    int nf = elf_imports(b, n, found, 40, libs, 64, &nlibs);
    if (nf >= 0) {
        kv("method", "ELF import table (exact)");
        for (int i = 0; i < nlibs; i++) {
            for (int k = 0; k_net_libs[k]; k++)
                if (strstr(libs[i], k_net_libs[k])) finding("THC links a networking library: %s", libs[i]);
            if (R.verbose) kv("links", "%s", libs[i]);
        }
    } else {
        nf = string_scan(b, n, found, 40);
        kv("method", "searched for imported function names (approximate: PE and Mach-O tables are not parsed)");
    }
    for (int i = 0; i < nf; i++) finding("THC imports a networking function: %s", found[i]);
    if (!nf) kv("network functions", "none imported");
    note("libmpv is loaded when THC starts playing; it is a separate library with its own abilities, which THC limits with "
         "\"Block network in MPV\" (see the MPV section).");
    free(b);
}

/* ------------------------------------------------------------------------ */
/* Network connections of a running THC (Linux: /proc)                         */
/* ------------------------------------------------------------------------ */

#ifdef __linux__
static int find_thc_pid(void)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    int self = (int)getpid();
    while (!found && (e = readdir(d)) != NULL) {
        int pid = atoi(e->d_name);
        if (pid <= 0 || pid == self) continue;
        char p[64], comm[64] = "";
        snprintf(p, sizeof p, "/proc/%d/comm", pid);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        if (fgets(comm, sizeof comm, f)) comm[strcspn(comm, "\n")] = 0;
        fclose(f);
        if (!strcmp(comm, "thc")) found = pid;
    }
    closedir(d);
    return found;
}

static int socket_inodes(int pid, unsigned long *out, int max)
{
    char dir[64];
    snprintf(dir, sizeof dir, "/proc/%d/fd", pid);
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) != NULL && n < max) {
        char p[320], tgt[160];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        ssize_t l = readlink(p, tgt, sizeof tgt - 1);
        if (l <= 0) continue;
        tgt[l] = 0;
        unsigned long ino;
        if (sscanf(tgt, "socket:[%lu]", &ino) == 1) out[n++] = ino;
    }
    closedir(d);
    return n;
}

static int has_inode(const unsigned long *v, int n, unsigned long ino)
{
    for (int i = 0; i < n; i++) if (v[i] == ino) return 1;
    return 0;
}

static void addr_text(const char *hex, char *out, size_t cap)
{
    char ip[64] = "";
    unsigned port = 0;
    const char *colon = strchr(hex, ':');
    if (!colon) { snprintf(out, cap, "%s", hex); return; }
    size_t hl = (size_t)(colon - hex);
    sscanf(colon + 1, "%x", &port);
    if (hl == 8) {
        unsigned v = 0;
        sscanf(hex, "%8x", &v);
        snprintf(ip, sizeof ip, "%u.%u.%u.%u", v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24);
    } else {
        snprintf(ip, sizeof ip, "ipv6 %.*s", (int)hl, hex);
    }
    snprintf(out, cap, "%s:%u", ip, port);
}

static void report_network(int pid)
{
    section("Network connections");
    if (pid <= 0) pid = find_thc_pid();
    if (pid <= 0) {
        note("THC is not running, so there is nothing to check. Start THC, play something, and run this again "
             "(or pass --pid).");
        return;
    }
    kv("process", "thc, pid %d", pid);
    unsigned long inodes[1024];
    int n = socket_inodes(pid, inodes, 1024);
    if (n < 0) {
        note("Could not read the process's open files (not allowed, or it exited).");
        return;
    }
    static const char *const files[] = { "tcp", "tcp6", "udp", "udp6", "raw", "raw6" };
    int inet = 0;
    for (int f = 0; f < 6; f++) {
        char p[96];
        snprintf(p, sizeof p, "/proc/%d/net/%s", pid, files[f]);
        FILE *fp = fopen(p, "r");
        if (!fp) continue;
        char line[512];
        if (!fgets(line, sizeof line, fp)) { fclose(fp); continue; }
        while (fgets(line, sizeof line, fp)) {
            char loc[80], rem[80];
            unsigned st;
            unsigned long ino = 0;
            int uid;
            if (sscanf(line, " %*d: %79s %79s %x %*x:%*x %*x:%*x %*x %d %*d %lu", loc, rem, &st, &uid, &ino) < 5) continue;
            if (!has_inode(inodes, n, ino)) continue;
            char l[96], r[96];
            addr_text(loc, l, sizeof l);
            addr_text(rem, r, sizeof r);
            inet++;
            finding("THC has a %s socket: %s -> %s", files[f], l, r);
        }
        fclose(fp);
    }
    int unixn = n - inet;
    kv("internet sockets", "%d", inet);
    kv("local sockets", "%d (the display server, audio and the desktop's message bus; these never leave this computer)", unixn);
}

static char *g_pid_exe;
static const char *exe_of_pid(int pid)
{
    if (pid <= 0) pid = find_thc_pid();
    if (pid <= 0) return NULL;
    char p[64], tgt[1100];
    snprintf(p, sizeof p, "/proc/%d/exe", pid);
    ssize_t l = readlink(p, tgt, sizeof tgt - 1);
    if (l <= 0) return NULL;
    tgt[l] = 0;
    free(g_pid_exe);
    g_pid_exe = dupstr(tgt);
    return g_pid_exe;
}
#else
static void report_network(int pid)
{
    (void)pid;
    section("Network connections");
    note("Listing a running program's connections is only built for Linux so far. On this system use the operating "
         "system's own tools (Windows: Resource Monitor or `netstat -ano`; macOS: `lsof -i -a -p <pid>`).");
}
static const char *exe_of_pid(int pid) { (void)pid; return NULL; }
#endif

/* ------------------------------------------------------------------------ */
/* MPV config                                                                  */
/* ------------------------------------------------------------------------ */

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int setting_value(const char *data, const char *key, char *out, size_t cap)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/settings.ini", data);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    size_t kl = strlen(key);
    while (!found && fgets(line, sizeof line, f)) {
        char *t = trim(line);
        if (!strncmp(t, key, kl) && trim(t + kl)[0] == '=') {
            snprintf(out, cap, "%s", trim(strchr(t, '=') + 1));
            found = 1;
        }
    }
    fclose(f);
    return found;
}

static void report_mpv(const char *data)
{
    section("MPV");
    char v[64] = "";
    int block = 1;
    if (setting_value(data, "mpv_block_network", v, sizeof v)) block = strcmp(v, "false") && strcmp(v, "0") && strcmp(v, "no");
    kv("Block network in MPV", block ? "on" : "off");
    if (!block) finding("\"Block network in MPV\" is off: mpv.conf may turn on youtube-dl and network protocols.");
    char p[1200];
    snprintf(p, sizeof p, "%s/mpv/mpv.conf", data);
    FILE *f = fopen(p, "r");
    if (!f) {
        note("THC's mpv.conf does not exist (MPV runs with THC's defaults).");
    } else {
        char line[512];
        int ln = 0;
        while (fgets(line, sizeof line, f)) {
            ln++;
            char *t = trim(line);
            if (!*t || *t == '#') continue;
            char key[96] = "", val[300] = "";
            char *eq = strchr(t, '=');
            snprintf(key, sizeof key, "%.*s", eq ? (int)(eq - t) : (int)strlen(t), t);
            if (eq) snprintf(val, sizeof val, "%s", trim(eq + 1));
            char *k = trim(key);
            int online = (!strcmp(k, "ytdl") && strcmp(val, "no")) || !strcmp(k, "ytdl-format") || !strcmp(k, "ytdl-raw-options") ||
                         (!strcmp(k, "load-unsafe-playlists") && strcmp(val, "no")) ||
                         ((!strcmp(k, "stream-lavf-o") || !strcmp(k, "demuxer-lavf-o")) && strstr(val, "protocol_whitelist") &&
                          (strstr(val, "http") || strstr(val, "tcp") || strstr(val, "udp")));
            if (online) {
                if (block) note("mpv.conf line %d (%s) enables an online feature; THC's block overrides it.", ln, k);
                else finding("mpv.conf line %d (%s) enables an online feature, and nothing blocks it.", ln, k);
            }
        }
        fclose(f);
        kv("mpv.conf", "present");
    }
    /* user scripts can do anything, including networking, so they are listed */
    snprintf(p, sizeof p, "%s/mpv/scripts", data);
    struct stat st;
    if (stat(p, &st) == 0 && is_dir(&st)) {
        struct { int n; } c = { 0 };
        (void)c;
        kv("scripts folder", "%s (scripts are your own code; THC cannot limit what they do)", p);
    }
}

/* ------------------------------------------------------------------------ */
/* Media untouched: fingerprint a folder, then compare                         */
/* ------------------------------------------------------------------------ */

typedef struct Fp { char *rel; long long size; long long mtime; uint64_t hash; } Fp;
typedef struct FpSet { Fp *v; int n, cap; } FpSet;

static uint64_t fnv(uint64_t h, const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static uint64_t hash_file(const char *path, long long size)
{
    FILE *f = fopen(path, "rb");
    uint64_t h = 1469598103934665603ULL;
    if (!f) return h;
    unsigned char buf[65536];
    size_t n = fread(buf, 1, sizeof buf, f);
    h = fnv(h, buf, n);
    if (size > (long long)(2 * sizeof buf)) {          /* the middle is skipped: head, tail and size are enough to notice edits */
        fseek(f, -(long)sizeof buf, SEEK_END);
        n = fread(buf, 1, sizeof buf, f);
        h = fnv(h, buf, n);
    }
    fclose(f);
    return h;
}

static void fp_cb(const char *full, const char *rel, const struct stat *st, void *ud)
{
    FpSet *s = ud;
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 256;
        s->v = realloc(s->v, (size_t)s->cap * sizeof *s->v);
    }
    Fp *f = &s->v[s->n++];
    f->rel = dupstr(rel);
    f->size = (long long)st->st_size;
    f->mtime = (long long)st->st_mtime;
    f->hash = hash_file(full, f->size);
}

static int fp_cmp(const void *a, const void *b) { return strcmp(((const Fp *)a)->rel, ((const Fp *)b)->rel); }

static void fp_scan(FpSet *s, const char *dir)
{
    memset(s, 0, sizeof *s);
    walk(dir, "", fp_cb, s);
    if (s->n) qsort(s->v, (size_t)s->n, sizeof *s->v, fp_cmp);
}

static int fp_write(const FpSet *s, const char *file)
{
    FILE *f = fopen(file, "wb");
    if (!f) return 0;
    fprintf(f, "THCFP1\n");
    for (int i = 0; i < s->n; i++)
        fprintf(f, "%lld\t%lld\t%016llx\t%s\n", s->v[i].size, s->v[i].mtime, (unsigned long long)s->v[i].hash, s->v[i].rel);
    fclose(f);
    return 1;
}

static int fp_read(FpSet *s, const char *file)
{
    memset(s, 0, sizeof *s);
    FILE *f = fopen(file, "rb");
    if (!f) return 0;
    char line[2048];
    if (!fgets(line, sizeof line, f) || strncmp(line, "THCFP1", 6) != 0) { fclose(f); return 0; }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        long long size, mt;
        unsigned long long h;
        int off = 0;
        if (sscanf(line, "%lld\t%lld\t%llx\t%n", &size, &mt, &h, &off) < 3 || !off) continue;
        if (s->n == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 256;
            s->v = realloc(s->v, (size_t)s->cap * sizeof *s->v);
        }
        s->v[s->n++] = (Fp){ dupstr(line + off), size, mt, h };
    }
    fclose(f);
    if (s->n) qsort(s->v, (size_t)s->n, sizeof *s->v, fp_cmp);
    return 1;
}

static void report_media(const char *dir, const char *snapshot, const char *compare)
{
    section("Media files");
    struct stat st;
    if (stat(dir, &st) != 0 || !is_dir(&st)) {
        finding("The media folder %s does not exist.", dir);
        return;
    }
    FpSet now;
    fp_scan(&now, dir);
    kv("folder", "%s", dir);
    kv("files", "%d", now.n);
    if (snapshot) {
        if (!fp_write(&now, snapshot)) finding("Could not write the snapshot file %s", snapshot);
        else note("Snapshot saved to %s. Run THC, then run this again with --compare %s.", snapshot, snapshot);
    }
    if (compare) {
        FpSet old;
        if (!fp_read(&old, compare)) {
            finding("Could not read the snapshot file %s", compare);
            return;
        }
        int changed = 0, added = 0, removed = 0;
        for (int i = 0; i < old.n; i++) {
            Fp *m = bsearch(&old.v[i], now.v, (size_t)now.n, sizeof *now.v, fp_cmp);
            if (!m) { removed++; finding("Missing now: %s", old.v[i].rel); }
            else if (m->size != old.v[i].size || m->hash != old.v[i].hash || m->mtime != old.v[i].mtime) {
                changed++;
                finding("Changed: %s", old.v[i].rel);
            }
        }
        for (int i = 0; i < now.n; i++)
            if (!bsearch(&now.v[i], old.v, (size_t)old.n, sizeof *old.v, fp_cmp)) { added++; finding("New: %s", now.v[i].rel); }
        kv("compared with", "%s (%d files)", compare, old.n);
        kv("changed", "%d", changed);
        kv("added", "%d", added);
        kv("removed", "%d", removed);
        if (!changed && !added && !removed) note("Every file is exactly as it was.");
    }
}

/* ------------------------------------------------------------------------ */
/* Main                                                                        */
/* ------------------------------------------------------------------------ */

static void default_data_dir(char *out, size_t cap)
{
    const char *e;
#ifdef _WIN32
    e = getenv("APPDATA");
    snprintf(out, cap, "%s\\THC\\THC", e ? e : ".");
#elif defined(__APPLE__)
    e = getenv("HOME");
    snprintf(out, cap, "%s/Library/Application Support/THC/THC", e ? e : ".");
#else
    e = getenv("XDG_DATA_HOME");
    if (e && *e) snprintf(out, cap, "%s/THC/THC", e);
    else snprintf(out, cap, "%s/.local/share/THC/THC", getenv("HOME") ? getenv("HOME") : ".");
#endif
}

static void usage(FILE *f)
{
    fprintf(f,
            "thc-inspect %s: check what THC really does. It only reads; it never connects to the network.\n\n"
            "Usage: thc-inspect [options]\n"
            "  --data <dir>      THC's data folder (default: the standard one)\n"
            "  --exe <path>      THC's program file (default: the running THC)\n"
            "  --pid <n>         which running THC to check for connections\n"
            "  --media <dir>     a media folder, with --snapshot <file> or --compare <file>:\n"
            "                    run once with --snapshot, use THC, then --compare to see nothing changed\n"
            "  --show-titles     list recent history entries (hidden by default)\n"
            "  --json            machine-readable output\n"
            "  -v, -vv           more detail\n"
            "  --version, --help\n\n"
            "Exit code: 0 nothing found, 1 findings, 2 error.\n",
            INSPECT_VERSION);
}

int main(int argc, char **argv)
{
    char data[1100];
    default_data_dir(data, sizeof data);
    const char *exe = NULL, *media = NULL, *snapshot = NULL, *compare = NULL;
    int pid = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); return 0; }
        if (!strcmp(a, "--version")) { printf("thc-inspect %s\n", INSPECT_VERSION); return 0; }
        if (!strcmp(a, "--json")) R.json = 1;
        else if (!strcmp(a, "-v")) R.verbose = 1;
        else if (!strcmp(a, "-vv")) R.verbose = 2;
        else if (!strcmp(a, "--show-titles")) R.titles = 1;
        else if (i + 1 < argc && !strcmp(a, "--data")) snprintf(data, sizeof data, "%s", argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--exe")) exe = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--pid")) pid = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--media")) media = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--snapshot")) snapshot = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--compare")) compare = argv[++i];
        else {
            fprintf(stderr, "thc-inspect: unknown or incomplete option %s\n\n", a);
            usage(stderr);
            return 2;
        }
    }
    if ((snapshot || compare) && !media) {
        fprintf(stderr, "thc-inspect: --snapshot and --compare need --media <folder>\n");
        return 2;
    }
    if (snapshot && compare) {
        fprintf(stderr, "thc-inspect: use either --snapshot or --compare, not both\n");
        return 2;
    }
    if (media && !snapshot && !compare) {
        fprintf(stderr, "thc-inspect: --media needs --snapshot <file> or --compare <file>\n");
        return 2;
    }
    if (media) {
        /* media mode checks one thing */
        report_media(media, snapshot, compare);
    } else {
        report_files(data);
        report_history(data);
        report_network(pid);
        report_program(exe, exe_of_pid(pid));
        report_mpv(data);
    }
    print_report(stdout);
    return R.findings ? 1 : 0;
}
