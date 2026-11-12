/* portal_books.c: the audiobook portal ("Library" look: warm paper and ink,
 * covers drawn as books with a spine).
 *
 * Layouts understood (all optional):
 *   Author/Book/01 Chapter.mp3     a book made of several files
 *   Author/Book.m4b                one file, chapters inside
 *   Book/...  or  Book.m4b         without an author folder
 * Each book remembers where you stopped, per file, in the history database. */
#include "app.h"

#include <string.h>

typedef struct Book {
    char title[256];
    char author[256];
    const FsNode *node;     /* folder of files, or the single file */
    const FsNode *files[256];
    int nfiles;
    const FsNode *art;
} Book;

#define MAX_BOOKS 512
static Book g_books[MAX_BOOKS];
static int g_nbooks;
static Uint64 g_built_for;

static bool is_book_file(const FsNode *n)
{
    return !n->dir && (n->kind == MK_BOOK || n->kind == MK_AUDIO || n->kind == MK_AUDIO_MPV);
}

static int direct_files(const FsNode *d)
{
    int c = 0;
    for (int i = 0; i < d->nkids; i++) c += is_book_file(d->kids[i]);
    return c;
}

static void add_book(const FsNode *n, const char *author)
{
    if (g_nbooks >= MAX_BOOKS) return;
    Book *b = &g_books[g_nbooks++];
    SDL_zerop(b);
    b->node = n;
    SDL_strlcpy(b->author, author ? author : "", sizeof b->author);
    clean_title(n->name, !n->dir, b->title, sizeof b->title, NULL);
    if (!b->title[0]) SDL_strlcpy(b->title, n->name, sizeof b->title);
    if (n->dir) {
        for (int i = 0; i < n->nkids && b->nfiles < 256; i++)
            if (is_book_file(n->kids[i])) b->files[b->nfiles++] = n->kids[i];
        /* parts in sub-folders ("CD 1", "Disc 2") */
        for (int i = 0; i < n->nkids; i++) {
            const FsNode *k = n->kids[i];
            if (!k->dir) continue;
            for (int j = 0; j < k->nkids && b->nfiles < 256; j++)
                if (is_book_file(k->kids[j])) b->files[b->nfiles++] = k->kids[j];
        }
        b->art = fs_find_art(n);
    } else {
        b->files[b->nfiles++] = n;
        b->art = n->parent ? fs_find_art(n->parent) : NULL;
        if (b->art && n->parent && n->parent->parent == NULL) b->art = NULL; /* root folder art */
    }
}

static void build_books(App *a)
{
    if (g_built_for == a->lib_version) return;
    g_built_for = a->lib_version;
    g_nbooks = 0;
    const FsNode *root = a->lib.root[ROOT_BOOKS];
    if (!root) return;
    for (int i = 0; i < root->nkids; i++) {
        const FsNode *k = root->kids[i];
        if (!k->dir) {
            if (is_book_file(k)) add_book(k, NULL);
            continue;
        }
        /* Book/ with chapter files; but a folder holding only .m4b files is an
         * author's folder of single-file books */
        int m4b = 0;
        for (int j = 0; j < k->nkids; j++) m4b += !k->kids[j]->dir && k->kids[j]->kind == MK_BOOK;
        if (direct_files(k) > 0 && m4b < direct_files(k)) {
            add_book(k, NULL);
            continue;
        }
        for (int j = 0; j < k->nkids; j++) {   /* Author/ */
            const FsNode *b = k->kids[j];
            if (b->dir && (direct_files(b) > 0 || fs_count(b, MK_BOOK) + fs_count(b, MK_AUDIO) > 0))
                add_book(b, k->name);
            else if (is_book_file(b)) add_book(b, k->name);
        }
    }
}

typedef struct BookStat {
    int done;
    float frac;
    int resume;        /* file index to continue with */
    double resume_pos;
    bool started;
} BookStat;

static void book_stat(App *a, const Book *b, BookStat *st)
{
    SDL_zerop(st);
    Sint64 best = -1;
    float sum = 0;
    int first_unfinished = -1;
    for (int i = 0; i < b->nfiles; i++) {
        HistoryEntry e;
        if (!app_history(a, b->files[i], &e)) {
            if (first_unfinished < 0) first_unfinished = i;
            continue;
        }
        st->started = true;
        if (e.watched) {
            st->done++;
            sum += 1;
        } else {
            sum += (float)history_progress(&e);
            if (first_unfinished < 0) first_unfinished = i;
            if (e.position > 5 && e.updated > best) {
                best = e.updated;
                st->resume = i;
                st->resume_pos = history_resume_point(&e, a->set.watched_threshold);
            }
        }
    }
    if (best < 0) {
        st->resume = first_unfinished < 0 ? 0 : first_unfinished;
        st->resume_pos = 0;
    }
    st->frac = b->nfiles ? sum / b->nfiles : 0;
}

/* What to show for a book: tags in the files (title, author, narrator, cover) win over the
 * names of folders and files, once the tag cache has read them. */
typedef struct BookInfo {
    char title[256], author[256], narrator[256];
    char art[THC_PATH_MAX];
    int chapters;
} BookInfo;

static void book_info(const App *a, const Book *b, BookInfo *bi)
{
    SDL_zerop(bi);
    SDL_strlcpy(bi->title, b->title, sizeof bi->title);
    SDL_strlcpy(bi->author, b->author, sizeof bi->author);
    if (b->art) app_full_path(a, ROOT_BOOKS, b->art, bi->art, sizeof bi->art);
    /* the first few files are enough, and keep this cheap for books with hundreds of parts */
    for (int i = 0; i < b->nfiles && i < 8; i++) {
        const TagInfo *t = tag_get(a, ROOT_BOOKS, b->files[i]);
        if (!t) continue;
        if (i == 0) {
            /* a single-file book is named by its title tag; a folder of parts by its album tag */
            const char *name = b->nfiles == 1 && !b->node->dir ? (t->title[0] ? t->title : t->album)
                                                               : (t->album[0] ? t->album : "");
            if (name[0]) SDL_strlcpy(bi->title, name, sizeof bi->title);
            const char *who = t->artist[0] ? t->artist : t->album_artist;
            if (who[0]) SDL_strlcpy(bi->author, who, sizeof bi->author);
        }
        if (t->narrator[0] && !bi->narrator[0]) SDL_strlcpy(bi->narrator, t->narrator, sizeof bi->narrator);
        bi->chapters += t->chapters;
        char key[THC_PATH_MAX + 8];
        if (i == 0 && tag_art_key(a, ROOT_BOOKS, b->files[i], key, sizeof key)) SDL_strlcpy(bi->art, key, sizeof bi->art);
    }
}

/* A cover drawn as a book: art (or a gradient) with a spine on the left. */
static void book_cover(App *a, SDL_FRect r, const Book *b, float lift)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    BookInfo bi;
    book_info(a, b, &bi);
    float rad = dp(ui, 6);
    ui_shadow(ui, r, rad, dp(ui, 16 + lift * 16), t->shadow);
    draw_cover(a, r, bi.title, bi.art[0] ? bi.art : NULL, rad, ICON_NONE);
    if (!t->high_contrast) {
        float sw = r.w * 0.07f;
        ui_fill_grad_h(ui, (SDL_FRect){ r.x, r.y, sw, r.h }, rgba(0x000000, 90), rgba(0x000000, 0), rad);
        ui_fill(ui, (SDL_FRect){ r.x + sw, r.y + dp(ui, 2), dp(ui, 1), r.h - dp(ui, 4) }, rgba(0xFFFFFF, 50), 0);
    }
}

static bool book_card(App *a, SDL_FRect r, const Book *b)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    Uint32 id = ui_idp("book", b->node);
    SDL_FRect c = { r.x, r.y, r.w, r.w * 1.5f };
    UiItemState st = ui_item(ui, id, c);
    if (st.context) {
        float cx, cy;
        ctx_pos(ui, c, &cx, &cy);
        if (b->node->dir) queue_menu_dir(a, cx, cy, ROOT_BOOKS, b->node);
        else queue_menu_item(a, cx, cy, ROOT_BOOKS, b->node);
    }
    if (!ui_visible(ui, r)) return st.activated;
    float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
    SDL_FRect cc = { c.x - lift * dp(ui, 3), c.y - lift * dp(ui, 6), c.w + lift * dp(ui, 6), c.h + lift * dp(ui, 6) };
    book_cover(a, cc, b, lift);
    if (st.focused) ui_focus_ring(ui, cc, dp(ui, 6));
    BookStat bs;
    book_stat(a, b, &bs);
    float y = c.y + c.h + dp(ui, 12);
    BookInfo bi;
    book_info(a, b, &bi);
    ui_text_fit(ui, FONT_BOLD, TS_BODY, r.x, y, r.w, t->text, bi.title);
    y += ui_line_h(ui, TS_BODY);
    char meta[300];
    if (bi.author[0]) SDL_strlcpy(meta, bi.author, sizeof meta);
    else SDL_snprintf(meta, sizeof meta, "%d file%s", b->nfiles, b->nfiles == 1 ? "" : "s");
    app_nsfw_why(a, ROOT_BOOKS, b->node, st.hover || st.focused, c, meta, sizeof meta);
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x, y, r.w, t->text_dim, meta);
    y += ui_line_h(ui, TS_SMALL) + dp(ui, 6);
    ui_watch_status(ui, (SDL_FRect){ r.x, y, r.w, dp(ui, 16) }, bs.frac, b->nfiles && bs.done == b->nfiles, t->accent);
    return st.activated;
}

static void play_book(App *a, const Book *b, int file, bool from_start)
{
    if (from_start) {
        for (int i = 0; i < b->nfiles; i++) {
            char rel[THC_PATH_MAX];
            if (!app_history_ok_node(a, KIND_BOOK, b->files[i])) continue;
            history_set_watched(a->hist, fs_relpath(b->files[i], rel, sizeof rel), b->files[i]->size, false);
        }
        app_history_changed();
    }
    const FsNode *where = b->node->dir ? b->files[file]->parent : b->node;
    if (b->node->dir) {
        /* queue the folder the file lives in, starting at that file */
        int idx = 0;
        for (int i = 0; i < where->nkids; i++) {
            if (where->kids[i] == b->files[file]) break;
            if (is_book_file(where->kids[i])) idx++;
        }
        music_play_folder(a, ROOT_BOOKS, where, idx);
    } else {
        music_play_folder(a, ROOT_BOOKS, b->node, 0);
    }
}

static void book_page(App *a, const Book *b, float x, float *py, float w)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    float bs = dp(ui, 44);
    if (ui_icon_button(ui, ui_id("bback"), (SDL_FRect){ x - dp(ui, 6), *py, bs, bs }, ICON_BACK, "Back"))
        nav_pop(a, &a->bnav);
    *py += bs + dp(ui, 12);
    BookStat st;
    book_stat(a, b, &st);
    float cw = SDL_min(dp(ui, 220), w * 0.3f);
    SDL_FRect cr = { x, *py, cw, cw * 1.5f };
    book_cover(a, cr, b, 0.5f);
    float tx = x + cw + dp(ui, 36), tw = w - cw - dp(ui, 36);
    float ty = *py;
    ui_text(ui, FONT_BOLD, TS_SMALL, tx, ty, t->accent, "AUDIOBOOK");
    ty += ui_line_h(ui, TS_SMALL);
    BookInfo bi;
    book_info(a, b, &bi);
    ty += ui_text_wrap(ui, FONT_BOLD, TS_HERO, tx, ty, tw, t->text, bi.title, 2);
    if (bi.author[0]) {
        ui_text_fit(ui, FONT_REGULAR, TS_TITLE, tx, ty, tw, t->text_dim, bi.author);
        ty += ui_line_h(ui, TS_TITLE);
    }
    if (bi.narrator[0]) {
        char nar[300];
        SDL_snprintf(nar, sizeof nar, "Narrated by %s", bi.narrator);
        ui_text_fit(ui, FONT_REGULAR, TS_BODY, tx, ty, tw, t->text_dim, nar);
        ty += ui_line_h(ui, TS_BODY);
    }
    char meta[128];
    int used = SDL_snprintf(meta, sizeof meta, "%d part%s", b->nfiles, b->nfiles == 1 ? "" : "s");
    if (bi.chapters > 0 && used > 0 && used < (int)sizeof meta)
        used += SDL_snprintf(meta + used, sizeof meta - (size_t)used, " \xC2\xB7 %d chapters", bi.chapters);
    if (used > 0 && used < (int)sizeof meta)
        SDL_snprintf(meta + used, sizeof meta - (size_t)used, " \xC2\xB7 %d finished \xC2\xB7 %d%%", st.done,
                     (int)(st.frac * 100 + 0.5f));
    ty += dp(ui, 8);
    ui_text(ui, FONT_REGULAR, TS_BODY, tx, ty, t->text_dim, meta);
    ty += ui_line_h(ui, TS_BODY) + dp(ui, 10);
    ui_watch_status(ui, (SDL_FRect){ tx, ty, SDL_min(tw, dp(ui, 360)), dp(ui, 18) }, st.frac,
                    b->nfiles && st.done == b->nfiles, t->accent);
    ty += dp(ui, 34);
    char lbl[96];
    if (st.started && st.resume_pos > 0) {
        char p[32];
        fmt_time(st.resume_pos, p, sizeof p);
        if (b->nfiles > 1) SDL_snprintf(lbl, sizeof lbl, "Resume part %d \xC2\xB7 %s", st.resume + 1, p);
        else SDL_snprintf(lbl, sizeof lbl, "Resume at %s", p);
    } else if (st.started) {
        SDL_snprintf(lbl, sizeof lbl, b->nfiles > 1 ? "Continue with part %d" : "Listen", st.resume + 1);
    } else {
        SDL_strlcpy(lbl, "Start listening", sizeof lbl);
    }
    float bw = ui_text_w(ui, FONT_BOLD, TS_LABEL, lbl) + dp(ui, 70);
    if (ui_button(ui, ui_id("book-play"), (SDL_FRect){ tx, ty, bw, dp(ui, 48) }, lbl, ICON_PLAY, true))
        play_book(a, b, st.resume, false);
    float bx = tx + bw + dp(ui, 12);
    if (st.started && ui_button(ui, ui_id("book-restart"), (SDL_FRect){ bx, ty, dp(ui, 150), dp(ui, 48) }, "Start over",
                                ICON_REWIND, false))
        play_book(a, b, 0, true);
    {
        float ax = bx + (st.started ? dp(ui, 162) : 0);
        if (ui_button(ui, ui_id("book-add"), (SDL_FRect){ ax, ty, dp(ui, 190), dp(ui, 48) }, "Add to queue", ICON_QUEUE, false)) {
            if (b->node->dir) queue_add_dir(a, ROOT_BOOKS, b->node, false);
            else queue_add_file(a, ROOT_BOOKS, b->node, false);
        }
    }
    *py += SDL_max(cr.h, ty + dp(ui, 48) - *py) + dp(ui, 32);

    draw_heading(a, x, *py, b->nfiles > 1 ? "Parts" : "File", NULL);
    *py += ui_line_h(ui, TS_TITLE) + dp(ui, 14);
    float rh = dp(ui, 66);
    for (int i = 0; i < b->nfiles; i++) {
        const FsNode *f = b->files[i];
        SDL_FRect r = { x, *py, w, rh };
        Uint32 id = ui_idp("bfile", f);
        UiItemState s = ui_item(ui, id, r);
        if (s.focused || s.hover) { a->focus_file = f; a->focus_root = ROOT_BOOKS; }
        if (s.context) {
            float cx, cy;
            ctx_pos(ui, r, &cx, &cy);
            queue_menu_item(a, cx, cy, ROOT_BOOKS, f);
        }
        bool cur = music_is_current(a, f);
        if (ui_visible(ui, r)) {
            float hv = ui_anim_id(ui, id, (s.hover || s.focused) ? 1.0f : 0.0f, 16);
            ui_fill(ui, r, cur ? with_alpha(t->accent, 40) : color_mix(t->surface, t->surface_hi, hv), dp(ui, 12));
            if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
            if (s.focused) ui_focus_ring(ui, r, dp(ui, 12));
            char num[16];
            SDL_snprintf(num, sizeof num, "%d", i + 1);
            ui_text_center(ui, FONT_BOLD, TS_LABEL, (SDL_FRect){ r.x + dp(ui, 10), r.y, dp(ui, 44), rh },
                           cur ? t->accent : t->text_dim, num, true);
            char name[256];
            clean_title(f->name, true, name, sizeof name, NULL);
            float nx = r.x + dp(ui, 64);
            ui_text_fit(ui, FONT_BOLD, TS_BODY, nx, r.y + dp(ui, 12), r.w - dp(ui, 220), cur ? t->accent : t->text,
                        name[0] ? name : f->name);
            HistoryEntry e;
            bool have = app_history(a, f, &e);
            char m[64] = "";
            if (have && e.duration > 0) {
                char d[32];
                fmt_time(e.duration, d, sizeof d);
                if (e.watched) SDL_snprintf(m, sizeof m, "%s \xC2\xB7 Finished", d);
                else if (e.position > 0) {
                    char l[32];
                    fmt_time(e.duration - e.position, l, sizeof l);
                    SDL_snprintf(m, sizeof m, "%s \xC2\xB7 %s left", d, l);
                } else SDL_strlcpy(m, d, sizeof m);
            }
            ui_text_fit(ui, FONT_REGULAR, TS_SMALL, nx, r.y + dp(ui, 12) + ui_line_h(ui, TS_BODY), r.w - dp(ui, 220),
                        t->text_dim, m);
            ui_watch_status(ui, (SDL_FRect){ r.x + r.w - dp(ui, 200), r.y + (rh - dp(ui, 16)) / 2, dp(ui, 180), dp(ui, 16) },
                            have ? (float)history_progress(&e) : 0, have && e.watched, t->accent);
        }
        if (s.activated) play_book(a, b, i, false);
        *py += rh + dp(ui, 8);
    }
}

void portal_books(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    PortalNav *nav = &a->bnav;
    if (!a->set.books_dir[0]) {
        bool pick = false;
        draw_empty(a, area, ICON_BOOK, "Add your audiobooks",
                   "Pick a folder of audiobooks: Author, then Book, then files. M4B and MP3 work, and THC "
                   "remembers your place in every book.",
                   "Choose audiobook folder", &pick);
        if (pick) app_pick_folder(a, 2, NULL);
        return;
    }
    if (!a->lib.root[ROOT_BOOKS]) {
        bool pick = false;
        draw_empty(a, area, ICON_BOOK, a->scanning ? "Reading your library" : "Folder not found",
                   a->scanning ? "This only takes a moment." : a->set.books_dir,
                   a->scanning ? NULL : "Choose another folder", &pick);
        if (pick) app_pick_folder(a, 2, a->set.books_dir);
        return;
    }
    build_books(a);
    float pad = SDL_max(dp(ui, 32), (area.w - dp(ui, 1400)) / 2);
    float x = area.x + pad, w = area.w - 2 * pad;
    float off = ui_scroll_begin(ui, ui_idi("bpage", nav->depth), area, false);
    float y = area.y + dp(ui, 28) - off;

    if (nav->depth > 0) {
        const Book *b = NULL;
        for (int i = 0; i < g_nbooks; i++) {
            char rel[THC_PATH_MAX];
            if (!strcmp(fs_relpath(g_books[i].node, rel, sizeof rel), nav->rel[nav->depth])) b = &g_books[i];
        }
        if (b) book_page(a, b, x, &y, w);
        else nav_pop(a, nav);
    } else {
        ui_text(ui, FONT_BOLD, TS_H1, x, y, t->text, "Audiobooks");
        y += ui_line_h(ui, TS_H1);
        char sub[64];
        SDL_snprintf(sub, sizeof sub, "%d book%s", g_nbooks, g_nbooks == 1 ? "" : "s");
        ui_text(ui, FONT_REGULAR, TS_BODY, x, y, t->text_dim, sub);
        y += ui_line_h(ui, TS_BODY) + dp(ui, 28);

        /* books in progress first, like a nightstand */
        int order[MAX_BOOKS], n = 0, started = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < g_nbooks; i++) {
                BookStat bs;
                book_stat(a, &g_books[i], &bs);
                bool reading = bs.started && bs.done < g_books[i].nfiles;
                if ((pass == 0) == reading) order[n++] = i;
                if (pass == 0 && reading) started++;
            }
        float gap = dp(ui, 26);
        int cols = SDL_max(2, (int)((w + gap) / (dp(ui, 160) + gap)));
        float cw = (w - gap * (cols - 1)) / cols;
        float ch = cw * 1.5f + dp(ui, 12) + ui_line_h(ui, TS_BODY) + ui_line_h(ui, TS_SMALL) + dp(ui, 26);
        int idx = 0;
        for (int sec = 0; sec < 2; sec++) {
            int count = sec == 0 ? started : n - started;
            if (!count) continue;
            draw_heading(a, x, y, sec == 0 ? "Currently reading" : "Your shelf", NULL);
            y += ui_line_h(ui, TS_TITLE) + dp(ui, 16);
            for (int i = 0; i < count; i++, idx++) {
                const Book *b = &g_books[order[idx]];
                SDL_FRect r = { x + (i % cols) * (cw + gap), y + (i / cols) * (ch + gap), cw, ch };
                if (book_card(a, r, b)) {
                    char rel[THC_PATH_MAX];
                    nav_push(a, nav, ROOT_BOOKS, fs_relpath(b->node, rel, sizeof rel));
                }
            }
            y += ((count + cols - 1) / cols) * (ch + gap) + dp(ui, 12);
        }
        if (!g_nbooks) {
            ui_text(ui, FONT_REGULAR, TS_LABEL, x, y, t->text_dim, "No audiobooks found in this folder yet.");
            y += ui_line_h(ui, TS_LABEL);
        }
    }
    y += dp(ui, 48);
    ui_scroll_end(ui, y + off - area.y);
}
