#include "library.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Name parsing                                                               */
/* ------------------------------------------------------------------------ */

static void lower_ascii(const char *in, char *out, size_t cap)
{
    size_t i = 0;
    for (; in[i] && i + 1 < cap; i++) out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = 0;
}

static bool is_year(const char *tok, int *year)
{
    if (strlen(tok) != 4) return false;
    for (int i = 0; i < 4; i++)
        if (!isdigit((unsigned char)tok[i])) return false;
    int y = atoi(tok);
    if (y < 1900 || y > 2099) return false;
    if (year) *year = y;
    return true;
}

static bool is_release_tag(const char *tok)
{
    static const char *const tags[] = {
        "480p", "576p", "720p", "1080p", "1080i", "2160p", "4k", "uhd", "x264", "x265", "h264", "h265",
        "h.264", "h.265", "hevc", "avc", "xvid", "divx", "bluray", "blu-ray", "brrip", "bdrip", "webrip",
        "web-dl", "webdl", "web", "hdtv", "dvdrip", "dvd", "remux", "proper", "repack", "10bit", "8bit",
        "aac", "aac2.0", "dts", "ac3", "eac3", "ddp5.1", "dd5.1", "hdr", "hdr10", "dv", "atmos", "extended",
        "unrated", "remastered", "imax", "nf", "amzn", "dsnp", "hmax", "multi", "dual", "subbed", "dubbed",
        NULL
    };
    char low[64];
    lower_ascii(tok, low, sizeof low);
    for (int i = 0; tags[i]; i++)
        if (!strcmp(low, tags[i])) return true;
    return false;
}

void clean_title(const char *name, bool strip_ext, char *title, size_t cap, int *year)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", name);
    if (year) *year = 0;
    if (strip_ext) {
        char *dot = strrchr(buf, '.');
        if (dot && dot != buf && strlen(dot) <= 5 && media_kind(buf) != MK_OTHER) *dot = 0;
    }

    /* Remove [tags] and {tags}; parentheses go too if they hold a year or a
     * release tag, otherwise they stay ("The Office (US)"). */
    char out[512];
    size_t o = 0;
    bool paren_year = false;
    for (size_t i = 0; buf[i] && o + 1 < sizeof out;) {
        char c = buf[i];
        if (c == '[' || c == '{' || c == '(') {
            char close = c == '[' ? ']' : c == '{' ? '}' : ')';
            const char *end = strchr(buf + i + 1, close);
            if (end) {
                char inner[256];
                size_t n = (size_t)(end - (buf + i + 1));
                if (n >= sizeof inner) n = sizeof inner - 1;
                memcpy(inner, buf + i + 1, n);
                inner[n] = 0;
                int y;
                bool drop = c != '(' || is_year(inner, &y) || is_release_tag(inner);
                if (c == '(' && is_year(inner, &y)) {
                    if (year) *year = y;
                    paren_year = true;
                }
                if (drop) {
                    i = (size_t)(end - buf) + 1;
                    continue;
                }
            }
        }
        out[o++] = c;
        i++;
    }
    out[o] = 0;

    /* Dots and underscores as word separators ("Blade.Runner.2049"). */
    bool has_space = strchr(out, ' ') != NULL;
    for (char *p = out; *p; p++) {
        if (*p == '_') *p = ' ';
        else if (*p == '.' && !has_space) *p = ' ';
    }

    /* Split into words, cut at the first release tag, then take a trailing
     * year off the end. */
    char *words[64];
    int nw = 0;
    for (char *p = strtok(out, " "); p && nw < 64; p = strtok(NULL, " ")) words[nw++] = p;
    int cut = nw;
    for (int i = 0; i < nw; i++)
        if (is_release_tag(words[i])) { cut = i; break; }
    /* one trailing year, unless the year was already given in parentheses
     * ("Blade Runner 2049 (2017)") */
    int y;
    if (!paren_year && cut > 1 && is_year(words[cut - 1], &y)) {
        if (year) *year = y;
        cut--;
    }
    size_t t = 0;
    title[0] = 0;
    for (int i = 0; i < cut; i++) {
        size_t wl = strlen(words[i]);
        if (t + wl + 2 >= cap) break;
        if (t) title[t++] = ' ';
        memcpy(title + t, words[i], wl);
        t += wl;
        title[t] = 0;
    }
    /* trim separators left at either end */
    while (t > 0 && (title[t - 1] == '-' || title[t - 1] == ' ' || title[t - 1] == '.')) title[--t] = 0;
    size_t lead = 0;
    while (title[lead] == '-' || title[lead] == ' ' || title[lead] == '.') lead++;
    if (lead) memmove(title, title + lead, strlen(title + lead) + 1);
}

static int read_num(const char *s, int maxdigits, int *len)
{
    int n = 0, d = 0;
    while (isdigit((unsigned char)s[d]) && d < maxdigits) n = n * 10 + (s[d++] - '0');
    if (isdigit((unsigned char)s[d])) d = 0; /* too many digits: not a match */
    *len = d;
    return n;
}

void parse_episode(const char *filename, EpisodeInfo *out)
{
    memset(out, 0, sizeof *out);
    out->season = out->episode = out->episode_end = -1;
    char name[512], low[512];
    snprintf(name, sizeof name, "%s", filename);
    char *dot = strrchr(name, '.');
    if (dot && dot != name && media_kind(name) != MK_OTHER) *dot = 0;
    lower_ascii(name, low, sizeof low);
    size_t len = strlen(low);
    const char *rest = NULL;

    /* 1. S01E02, s1e2, S01.E02, S01E01-E02 */
    for (size_t i = 0; i < len && !rest; i++) {
        if (low[i] != 's' || (i > 0 && isalpha((unsigned char)low[i - 1]))) continue;
        int sl, el;
        int s = read_num(low + i + 1, 2, &sl);
        if (!sl) continue;
        size_t j = i + 1 + (size_t)sl;
        while (low[j] == '.' || low[j] == ' ' || low[j] == '_' || low[j] == '-') j++;
        if (low[j] != 'e') continue;
        int e = read_num(low + j + 1, 4, &el);
        if (!el) continue;
        out->season = s;
        out->episode = out->episode_end = e;
        j += 1 + (size_t)el;
        if ((low[j] == '-' && low[j + 1] == 'e') || low[j] == 'e') {
            size_t k = j + (low[j] == '-' ? 2 : 1);
            int e2 = read_num(low + k, 4, &el);
            if (el && e2 > e) { out->episode_end = e2; j = k + (size_t)el; }
        }
        rest = name + j;
    }
    /* 2. 1x02 (not a resolution like 1920x1080) */
    for (size_t i = 0; i < len && !rest; i++) {
        if (!isdigit((unsigned char)low[i]) || (i > 0 && isalnum((unsigned char)low[i - 1]))) continue;
        int sl, el;
        int s = read_num(low + i, 2, &sl);
        if (!sl || low[i + (size_t)sl] != 'x') continue;
        int e = read_num(low + i + (size_t)sl + 1, 3, &el);
        if (!el) continue;
        out->season = s;
        out->episode = out->episode_end = e;
        rest = name + i + (size_t)sl + 1 + (size_t)el;
    }
    /* 3. "Episode 2", "Ep 2", "Ep.02", "E02" */
    for (size_t i = 0; i < len && !rest; i++) {
        if (i > 0 && isalpha((unsigned char)low[i - 1])) continue;
        size_t k = 0;
        if (!strncmp(low + i, "episode", 7)) k = 7;
        else if (!strncmp(low + i, "ep", 2)) k = 2;
        else if (low[i] == 'e') k = 1;
        else continue;
        size_t j = i + k;
        if (k >= 2) while (low[j] == ' ' || low[j] == '.' || low[j] == '_' || low[j] == '#') j++;
        int el;
        int e = read_num(low + j, 4, &el);
        if (!el) continue;
        out->episode = out->episode_end = e;
        rest = name + j + (size_t)el;
    }
    /* 4. anime style "Show - 03 [1080p]" or "Show - 03v2" */
    for (size_t i = 0; i + 3 < len && !rest; i++) {
        if (strncmp(low + i, " - ", 3)) continue;
        int el;
        int e = read_num(low + i + 3, 4, &el);
        if (!el) continue;
        char after = low[i + 3 + (size_t)el];
        if (after && after != ' ' && after != '[' && after != '(' && after != 'v' && after != '.') continue;
        int y;
        char tok[8];
        snprintf(tok, sizeof tok, "%.*s", el, low + i + 3);
        if (is_year(tok, &y)) continue;
        out->episode = out->episode_end = e;
        rest = name + i + 3 + (size_t)el;
        if (*rest == 'v' && isdigit((unsigned char)rest[1])) rest += 2;
    }
    /* 5. a leading number: "02 - Title", "02. Title", "2 Title" */
    if (!rest && isdigit((unsigned char)low[0])) {
        int el;
        int e = read_num(low, 3, &el);
        if (el && (!low[el] || low[el] == ' ' || low[el] == '.' || low[el] == '-' || low[el] == '_')) {
            out->episode = out->episode_end = e;
            rest = name + el;
        }
    }
    if (rest) clean_title(rest, false, out->title, sizeof out->title, NULL);
}

int parse_season_dir(const char *dirname)
{
    char low[256];
    lower_ascii(dirname, low, sizeof low);
    static const char *const specials[] = { "specials", "special", "extras", "extra", "ova", "ovas",
                                            "bonus", "featurettes", NULL };
    for (int i = 0; specials[i]; i++)
        if (!strcmp(low, specials[i])) return 0;
    static const char *const words[] = { "season", "series", "staffel", "saison", "temporada", "stagione",
                                         "book", "part", "volume", "vol", "cour", NULL };
    for (int w = 0; words[w]; w++) {
        size_t wl = strlen(words[w]);
        const char *p = strstr(low, words[w]);
        if (!p || (p > low && isalpha((unsigned char)p[-1]))) continue;
        p += wl;
        while (*p == ' ' || *p == '.' || *p == '_' || *p == '-') p++;
        int nl;
        int n = read_num(p, 3, &nl);
        if (nl) return n;
    }
    /* "S02" / "S2" alone */
    if (low[0] == 's') {
        int nl;
        int n = read_num(low + 1, 2, &nl);
        if (nl && (!low[1 + nl] || low[1 + nl] == ' ')) return n;
    }
    /* a bare number: "1", "01" */
    int nl;
    int n = read_num(low, 2, &nl);
    if (nl && !low[nl]) return n;
    return -1;
}

int section_for_dirname(const char *dirname)
{
    char low[64];
    lower_ascii(dirname, low, sizeof low);
    static const struct { const char *name; int section; } map[] = {
        { "anime", SECTION_ANIME }, { "animes", SECTION_ANIME },
        { "tv", SECTION_TV }, { "television", SECTION_TV }, { "tv shows", SECTION_TV },
        { "tv series", SECTION_TV }, { "shows", SECTION_TV }, { "series", SECTION_TV },
        { "television shows", SECTION_TV },
        { "cartoons", SECTION_CARTOONS }, { "cartoon", SECTION_CARTOONS }, { "animation", SECTION_CARTOONS },
        { "animated", SECTION_CARTOONS },
        { "movies", SECTION_MOVIES }, { "movie", SECTION_MOVIES }, { "films", SECTION_MOVIES },
        { "film", SECTION_MOVIES },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcmp(low, map[i].name)) return map[i].section;
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Building the library                                                       */
/* ------------------------------------------------------------------------ */

typedef struct Builder {
    VideoLibrary *lib;
    int cap_shows, cap_movies;
    const Settings *set;
} Builder;

static void collect_videos(const FsNode *n, const FsNode ***list, int *count, int *cap)
{
    for (int i = 0; i < n->nkids; i++) {
        const FsNode *k = n->kids[i];
        if (k->dir) collect_videos(k, list, count, cap);
        else if (k->kind == MK_VIDEO) {
            if (*count == *cap) {
                *cap = *cap ? *cap * 2 : 16;
                *list = realloc(*list, sizeof **list * (size_t)*cap);
            }
            (*list)[(*count)++] = k;
        }
    }
}

static void make_key(char *key, size_t cap, int root, const FsNode *n)
{
    char rel[THC_PATH_MAX];
    fs_relpath(n, rel, sizeof rel);
    snprintf(key, cap, "%d:%s", root, rel);
}

static int ep_cmp(const void *pa, const void *pb)
{
    const Episode *a = pa, *b = pb;
    int na = a->number < 0 ? 1 << 30 : a->number, nb = b->number < 0 ? 1 << 30 : b->number;
    if (na != nb) return na < nb ? -1 : 1;
    return natcmp(a->file->name, b->file->name);
}

static int season_rank(const Season *s)
{
    if (s->number > 0) return s->number;
    if (s->number == 0) return 1 << 20;      /* specials last */
    return 1 << 19;                          /* unnumbered after numbered */
}

static int season_cmp(const void *pa, const void *pb)
{
    const Season *a = pa, *b = pb;
    int ra = season_rank(a), rb = season_rank(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    return natcmp(a->name, b->name);
}

static Season *season_for(Show *sh, int number, const FsNode *dir, const char *name)
{
    for (int i = 0; i < sh->nseasons; i++) {
        Season *s = &sh->seasons[i];
        if (dir && s->dir == dir) return s;
        if (!dir && number >= 0 && s->number == number) return s;
        if (!dir && number < 0 && s->number < 0 && !s->dir) return s;
    }
    sh->seasons = realloc(sh->seasons, sizeof *sh->seasons * (size_t)(sh->nseasons + 1));
    Season *s = &sh->seasons[sh->nseasons++];
    memset(s, 0, sizeof *s);
    s->number = number;
    s->dir = dir;
    if (name) snprintf(s->name, sizeof s->name, "%s", name);
    else if (number == 0) snprintf(s->name, sizeof s->name, "Specials");
    else if (number > 0) snprintf(s->name, sizeof s->name, "Season %d", number);
    else snprintf(s->name, sizeof s->name, "Episodes");
    return s;
}

static void add_episode(Season *s, const FsNode *f)
{
    s->eps = realloc(s->eps, sizeof *s->eps * (size_t)(s->neps + 1));
    Episode *e = &s->eps[s->neps++];
    memset(e, 0, sizeof *e);
    EpisodeInfo info;
    parse_episode(f->name, &info);
    e->file = f;
    e->number = info.episode;
    e->number_end = info.episode_end;
    snprintf(e->title, sizeof e->title, "%s", info.title);
}

static void finish_show(Show *sh)
{
    qsort(sh->seasons, (size_t)sh->nseasons, sizeof *sh->seasons, season_cmp);
    sh->nepisodes = 0;
    for (int i = 0; i < sh->nseasons; i++) {
        Season *s = &sh->seasons[i];
        qsort(s->eps, (size_t)s->neps, sizeof *s->eps, ep_cmp);
        int next = 1;
        for (int j = 0; j < s->neps; j++) {
            Episode *e = &s->eps[j];
            if (e->number < 0) {
                /* unnumbered files follow the numbered ones in name order */
                e->number = e->number_end = next;
            }
            next = e->number_end + 1;
            if (e->number_end > e->number)
                snprintf(e->label, sizeof e->label, "Episodes %d\xE2\x80\x93%d", e->number, e->number_end);
            else
                snprintf(e->label, sizeof e->label, "Episode %d", e->number);
        }
        sh->nepisodes += s->neps;
    }
}

static void build_show(Builder *b, const FsNode *dir, int root, int section)
{
    VideoLibrary *lib = b->lib;
    if (lib->nshows == b->cap_shows) {
        b->cap_shows = b->cap_shows ? b->cap_shows * 2 : 32;
        lib->shows = realloc(lib->shows, sizeof *lib->shows * (size_t)b->cap_shows);
    }
    Show *sh = &lib->shows[lib->nshows++];
    memset(sh, 0, sizeof *sh);
    clean_title(dir->name, false, sh->title, sizeof sh->title, &sh->year);
    if (!sh->title[0]) snprintf(sh->title, sizeof sh->title, "%s", dir->name);
    sh->root = root;
    sh->section = section;
    sh->dir = dir;
    sh->art = fs_find_art(dir);
    sh->added = fs_newest(dir);
    make_key(sh->key, sizeof sh->key, root, dir);

    for (int i = 0; i < dir->nkids; i++) {
        const FsNode *k = dir->kids[i];
        if (k->dir) {
            if (fs_count(k, MK_VIDEO) == 0) continue;
            int num = parse_season_dir(k->name);
            Season *s = season_for(sh, num, k, NULL);
            if (num >= 0) {
                /* keep a descriptive name like "Book 1 Water", else "Season N" */
                char low[128];
                lower_ascii(k->name, low, sizeof low);
                if (num > 0 && (strstr(low, "season") == low || low[0] == 's' || isdigit((unsigned char)low[0])))
                    snprintf(s->name, sizeof s->name, "Season %d", num);
                else
                    snprintf(s->name, sizeof s->name, "%s", k->name);
            } else {
                snprintf(s->name, sizeof s->name, "%s", k->name);
            }
            const FsNode **files = NULL;
            int n = 0, cap = 0;
            collect_videos(k, &files, &n, &cap);
            for (int j = 0; j < n; j++) add_episode(s, files[j]);
            free(files);
            if (!sh->art) sh->art = fs_find_art(k);
        } else if (k->kind == MK_VIDEO) {
            EpisodeInfo info;
            parse_episode(k->name, &info);
            add_episode(season_for(sh, info.season, NULL, NULL), k);
        }
    }
    /* A show with one unnamed group of episodes reads better as "Season 1". */
    if (sh->nseasons == 1 && sh->seasons[0].number < 0 && !sh->seasons[0].dir) {
        sh->seasons[0].number = 1;
        snprintf(sh->seasons[0].name, sizeof sh->seasons[0].name, "Season 1");
    }
    finish_show(sh);
}

static void build_movie(Builder *b, const FsNode *n, int root, int section)
{
    const FsNode *file = n;
    if (n->dir) {
        /* the main feature is the largest video in the folder */
        const FsNode **files = NULL;
        int count = 0, cap = 0;
        collect_videos(n, &files, &count, &cap);
        file = NULL;
        for (int i = 0; i < count; i++)
            if (!file || files[i]->size > file->size) file = files[i];
        free(files);
        if (!file) return;
    }
    VideoLibrary *lib = b->lib;
    if (lib->nmovies == b->cap_movies) {
        b->cap_movies = b->cap_movies ? b->cap_movies * 2 : 32;
        lib->movies = realloc(lib->movies, sizeof *lib->movies * (size_t)b->cap_movies);
    }
    Movie *m = &lib->movies[lib->nmovies++];
    memset(m, 0, sizeof *m);
    clean_title(n->name, !n->dir, m->title, sizeof m->title, &m->year);
    if (!m->title[0]) snprintf(m->title, sizeof m->title, "%s", n->name);
    m->root = root;
    m->section = section;
    m->file = file;
    m->dir = n->dir ? n : NULL;
    m->art = n->dir ? fs_find_art(n) : NULL;
    m->added = n->dir ? fs_newest(n) : n->mtime;
    make_key(m->key, sizeof m->key, root, n);
}

static bool looks_like_show(const FsNode *dir)
{
    int seasons = 0, episodic = 0;
    for (int i = 0; i < dir->nkids; i++) {
        const FsNode *k = dir->kids[i];
        if (k->dir && parse_season_dir(k->name) >= 0 && fs_count(k, MK_VIDEO) > 0) seasons++;
        if (!k->dir && k->kind == MK_VIDEO) {
            EpisodeInfo info;
            parse_episode(k->name, &info);
            if (info.episode >= 0) episodic++;
        }
    }
    return seasons > 0 || episodic >= 2 || fs_count(dir, MK_VIDEO) >= 3;
}

static void classify(Builder *b, const FsNode *n, int root, int hint)
{
    if (n->dir && fs_count(n, MK_VIDEO) == 0) return;
    if (!n->dir && n->kind != MK_VIDEO) return;
    char key[THC_PATH_MAX];
    make_key(key, sizeof key, root, n);
    int ov = b->set ? settings_override(b->set, key) : -2;
    if (ov == -1) return; /* hidden by the user */

    bool show;
    int section;
    if (!n->dir) {
        show = false;
        section = hint >= 0 ? hint : SECTION_MOVIES;
    } else if (hint == SECTION_MOVIES) {
        /* a movie folder with extras is still a movie unless it has seasons */
        bool has_seasons = false;
        for (int i = 0; i < n->nkids; i++)
            if (n->kids[i]->dir && parse_season_dir(n->kids[i]->name) > 0) has_seasons = true;
        show = has_seasons;
        section = SECTION_MOVIES;
    } else if (hint >= 0) {
        show = true;
        section = hint;
    } else {
        show = looks_like_show(n);
        section = show ? SECTION_TV : SECTION_MOVIES;
    }
    if (ov >= 0 && ov < SECTION_COUNT) {
        section = ov;
        if (n->dir) show = ov != SECTION_MOVIES || looks_like_show(n);
    }
    if (show) build_show(b, n, root, section);
    else build_movie(b, n, root, section);
}

void library_build(VideoLibrary *lib, const LibraryRoot *roots, int nroots, const Settings *s)
{
    memset(lib, 0, sizeof *lib);
    Builder b = { lib, 0, 0, s };
    for (int r = 0; r < nroots; r++) {
        const FsNode *root = roots[r].node;
        if (!root) continue;
        for (int i = 0; i < root->nkids; i++) {
            const FsNode *k = root->kids[i];
            int sec = roots[r].section;
            if (sec < 0 && k->dir) {
                int named = section_for_dirname(k->name);
                if (named >= 0) {
                    /* "Anime/", "Movies/" ... inside the main folder */
                    char key[THC_PATH_MAX];
                    make_key(key, sizeof key, roots[r].id, k);
                    if (b.set && settings_override(b.set, key) == -1) continue;
                    for (int j = 0; j < k->nkids; j++) classify(&b, k->kids[j], roots[r].id, named);
                    continue;
                }
            }
            classify(&b, k, roots[r].id, sec);
        }
    }
}

void library_clear(VideoLibrary *lib)
{
    for (int i = 0; i < lib->nshows; i++) {
        for (int j = 0; j < lib->shows[i].nseasons; j++) free(lib->shows[i].seasons[j].eps);
        free(lib->shows[i].seasons);
    }
    free(lib->shows);
    free(lib->movies);
    memset(lib, 0, sizeof *lib);
}

const Show *library_show_of(const VideoLibrary *lib, const FsNode *file, int *season, int *episode)
{
    for (int i = 0; i < lib->nshows; i++) {
        const Show *sh = &lib->shows[i];
        for (int j = 0; j < sh->nseasons; j++)
            for (int k = 0; k < sh->seasons[j].neps; k++)
                if (sh->seasons[j].eps[k].file == file) {
                    if (season) *season = j;
                    if (episode) *episode = k;
                    return sh;
                }
    }
    return NULL;
}

const Movie *library_movie_of(const VideoLibrary *lib, const FsNode *file)
{
    for (int i = 0; i < lib->nmovies; i++)
        if (lib->movies[i].file == file) return &lib->movies[i];
    return NULL;
}
