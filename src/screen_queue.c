/* screen_queue.c: the queue panel, the right-click menu, adding things to the queues, M3U8
 * export and import, and the saved backlog. The queue logic itself is in queue.c. */
#include "app.h"
#include "hotkeys.h"

#include <stdlib.h>
#include <string.h>

#define MAX_GROUP 5000

/* ------------------------------------------------------------------------ */
/* Adding                                                                     */
/* ------------------------------------------------------------------------ */

static int g_tab = QUEUE_MUSIC;       /* which queue the panel shows */

static const char *queue_name(int q) { return q == QUEUE_VIDEO ? "video" : "music"; }

/* Which queue does this item go to? "other" sends it to the other one. */
static int target_queue(const App *a, const QueueItem *it, bool other)
{
    int q = a->set.queue_add_target == TARGET_ACTIVE ? player_active_queue() : queue_home_of(it->kind);
    if (other) q = q == QUEUE_VIDEO ? QUEUE_MUSIC : QUEUE_VIDEO;
    return q;
}

static int add_items(App *a, const QueueItem *items, int n, bool other, bool next)
{
    int added = 0, last_q = -1;
    int next_at[QUEUE_COUNT] = { -1, -1 };
    for (int i = 0; i < n; i++) {
        int qid = target_queue(a, &items[i], other);
        Queue *q = player_queue(qid);
        int at;
        if (next) {
            /* keep a group in its own order, right after the current item */
            if (next_at[qid] < 0) next_at[qid] = q->cur >= 0 ? q->cur + 1 : 0;
            at = queue_insert(q, SDL_min(next_at[qid], q->n), &items[i]);
            next_at[qid]++;
        } else {
            at = queue_add(q, &items[i], false);
        }
        if (at >= 0) { added++; last_q = qid; }
    }
    if (added) {
        app_toast(a, added == 1 ? "Added to the %s queue" : "Added %d items to the queue", added == 1 ? 0 : added);
        if (added == 1 && last_q >= 0) app_toast(a, "Added to the %s queue", queue_name(last_q));
        if (last_q >= 0) g_tab = last_q;      /* the panel follows what was just added */
    }
    return added;
}

static void add_node(App *a, int root, const FsNode *f, bool other, bool next)
{
    QueueItem it;
    qitem_from_node(a, &it, root, f, "", "");
    add_items(a, &it, 1, other, next);
}

/* ---- groups: an album or book folder, a season or a whole show ---- */

static int dir_items(App *a, int root, const char *rel, QueueItem *out, int max)
{
    FsNode *r = a->lib.root[root];
    FsNode *d = r ? fs_find(r, rel) : NULL;
    return d ? player_items_for(a, root, d, out, max) : 0;
}

static int show_items(App *a, const char *key, int season, bool unwatched_only, QueueItem *out, int max)
{
    const Show *sh = NULL;
    for (int i = 0; i < a->vlib.nshows; i++)
        if (!strcmp(a->vlib.shows[i].key, key)) sh = &a->vlib.shows[i];
    if (!sh) return 0;
    int n = 0;
    for (int s = 0; s < sh->nseasons; s++) {
        if (season >= 0 && s != season) continue;
        for (int e = 0; e < sh->seasons[s].neps && n < max; e++) {
            const Episode *ep = &sh->seasons[s].eps[e];
            if (unwatched_only) {
                HistoryEntry h;
                if (app_history(a, ep->file, &h) && h.watched) continue;
            }
            QueueItem *it = &out[n++];
            char sub[160];
            if (sh->seasons[s].number > 0) SDL_snprintf(sub, sizeof sub, "%s \xC2\xB7 S%d E%d", sh->title, sh->seasons[s].number, ep->number);
            else SDL_snprintf(sub, sizeof sub, "%s \xC2\xB7 %s", sh->title, ep->label);
            qitem_from_node(a, it, sh->root, ep->file, sub, "");
            if (ep->title[0]) SDL_strlcpy(it->title, ep->title, sizeof it->title);
            else SDL_strlcpy(it->title, ep->label, sizeof it->title);
        }
    }
    return n;
}

/* ------------------------------------------------------------------------ */
/* The right-click menu                                                       */
/* ------------------------------------------------------------------------ */

enum {
    CA_PLAY_NOW = 1, CA_PLAY_NEXT, CA_ADD_END, CA_ADD_OTHER, CA_PLAY_FROM_HERE, CA_ADD_UNWATCHED, CA_REMOVE,
    CA_MOVE_OTHER, CA_GOTO, CA_MARK_PLAYED, CA_MARK_UNPLAYED, CA_MODE_WINDOW, CA_MODE_LISTEN, CA_MODE_SKIP, CA_FORGET
};
enum { CT_ITEM = 1, CT_DIR, CT_SHOW, CT_QROW };

static struct {
    bool open, fresh;
    float x, y;
    int n;
    struct { char label[56]; int act; } row[14];
    int type;
    QueueItem item;                 /* CT_ITEM, CT_QROW */
    int root;
    char rel[THC_PATH_MAX];         /* CT_DIR: folder; CT_SHOW: the show's key */
    int season;                     /* CT_SHOW: -1 = the whole show */
    int qid, qidx;                  /* CT_QROW */
} M;

static void menu_row(int act, const char *label)
{
    if (M.n >= 14) return;
    M.row[M.n].act = act;
    SDL_strlcpy(M.row[M.n].label, label, sizeof M.row[0].label);
    M.n++;
}

static void menu_begin(App *a, float x, float y, int type)
{
    (void)a;
    SDL_zero(M);
    M.open = true;
    M.fresh = true;
    M.x = x;
    M.y = y;
    M.type = type;
    M.season = -1;
}

static void menu_common_add(const char *other_label)
{
    menu_row(CA_PLAY_NOW, "Play now");
    menu_row(CA_PLAY_NEXT, "Play next");
    menu_row(CA_ADD_END, "Add to the end of the queue");
    menu_row(CA_ADD_OTHER, other_label);
}

void queue_menu_item(App *a, float x, float y, int root, const FsNode *f)
{
    menu_begin(a, x, y, CT_ITEM);
    qitem_from_node(a, &M.item, root, f, "", "");
    M.root = root;
    menu_common_add(M.item.kind == QK_VIDEO ? "Add to the music queue" : "Add to the video queue");
    menu_row(CA_GOTO, M.item.kind == QK_VIDEO ? "Go to the show" : "Go to the album");
    menu_row(CA_MARK_PLAYED, M.item.kind == QK_VIDEO ? "Mark as watched" : "Mark as played");
    menu_row(CA_MARK_UNPLAYED, M.item.kind == QK_VIDEO ? "Mark as not watched" : "Mark as not played");
    menu_row(CA_FORGET, "Remove from history");
}

void queue_menu_dir(App *a, float x, float y, int root, const FsNode *dir)
{
    menu_begin(a, x, y, CT_DIR);
    M.root = root;
    fs_relpath(dir, M.rel, sizeof M.rel);
    menu_common_add("Add to the video queue");
    M.row[3].act = CA_ADD_OTHER;
    SDL_strlcpy(M.row[3].label, root == ROOT_VIDEO ? "Add to the music queue" : "Add to the video queue", sizeof M.row[3].label);
}

void queue_menu_show(App *a, float x, float y, const char *show_key, int season)
{
    menu_begin(a, x, y, CT_SHOW);
    SDL_strlcpy(M.rel, show_key, sizeof M.rel);
    M.season = season;
    menu_row(CA_PLAY_NOW, season >= 0 ? "Play this season" : "Play the show");
    menu_row(CA_PLAY_NEXT, "Play next");
    menu_row(CA_ADD_END, season >= 0 ? "Add the season to the queue" : "Add the show to the queue");
    menu_row(CA_ADD_UNWATCHED, "Add unwatched only");
    menu_row(CA_ADD_OTHER, "Add to the music queue (sound only)");
}

static void queue_menu_qrow(App *a, float x, float y, int qid, int idx)
{
    menu_begin(a, x, y, CT_QROW);
    Queue *q = player_queue(qid);
    if (idx < 0 || idx >= q->n) { M.open = false; return; }
    M.item = q->items[idx];
    M.qid = qid;
    M.qidx = idx;
    menu_row(CA_PLAY_NOW, "Play now");
    menu_row(CA_PLAY_NEXT, "Play next");
    menu_row(CA_PLAY_FROM_HERE, "Play from here");
    menu_row(CA_MOVE_OTHER, qid == QUEUE_VIDEO ? "Move to the music queue" : "Move to the video queue");
    if (M.item.kind == QK_VIDEO && qid == QUEUE_MUSIC) {
        menu_row(CA_MODE_WINDOW, "Play in a video window");
        menu_row(CA_MODE_LISTEN, "Listen only (no window)");
        menu_row(CA_MODE_SKIP, "Skip this video");
    }
    menu_row(CA_GOTO, M.item.kind == QK_VIDEO ? "Go to the show" : "Go to the album");
    menu_row(CA_MARK_PLAYED, M.item.kind == QK_VIDEO ? "Mark as watched" : "Mark as played");
    menu_row(CA_MARK_UNPLAYED, M.item.kind == QK_VIDEO ? "Mark as not watched" : "Mark as not played");
    menu_row(CA_FORGET, "Remove from history");
    menu_row(CA_REMOVE, "Remove from the queue");
}

static void goto_item(App *a, const QueueItem *it)
{
    char rel[THC_PATH_MAX];
    SDL_strlcpy(rel, it->rel, sizeof rel);
    if (it->kind == QK_VIDEO) {
        FsNode *root = a->lib.root[it->root];
        FsNode *f = root ? fs_find(root, rel) : NULL;
        const Show *sh = f ? library_show_of(&a->vlib, f, NULL, NULL) : NULL;
        if (sh) portal_video_open_show(a, sh->key);
        else app_go(a, SCREEN_VIDEO);
        return;
    }
    char *slash = SDL_strrchr(rel, '/');
    if (slash) *slash = 0; else rel[0] = 0;
    bool book = it->kind == QK_BOOK;
    PortalNav *nav = book ? &a->bnav : &a->mnav;
    app_go(a, book ? SCREEN_BOOKS : SCREEN_MUSIC);
    nav_reset(nav);
    if (rel[0]) nav_push(a, nav, book ? ROOT_BOOKS : ROOT_MUSIC, rel);
}

static void mark_item(App *a, const QueueItem *it, bool played)
{
    if (it->kind == QK_VIDEO || it->kind == QK_BOOK) {
        history_set_watched(a->hist, it->rel, it->size, played);
    } else if (played) {
        history_record_play(a->hist, it->rel, it->size, KIND_MUSIC, 0);
    } else {
        history_remove(a->hist, it->rel, it->size);
    }
    app_history_changed();
    app_toast(a, played ? "Marked as played" : "Marked as not played");
}

static void run_action(App *a, int act)
{
    QueueItem *buf = NULL;
    int n = 0;
    bool group = M.type == CT_DIR || M.type == CT_SHOW;
    if (group) {
        buf = SDL_malloc(sizeof *buf * MAX_GROUP);
        if (!buf) return;
        if (M.type == CT_DIR) n = dir_items(a, M.root, M.rel, buf, MAX_GROUP);
        else n = show_items(a, M.rel, M.season, act == CA_ADD_UNWATCHED || a->set.queue_season_unwatched_only, buf, MAX_GROUP);
    }
    switch (act) {
    case CA_PLAY_NOW:
        if (group) {
            if (n) player_play_items(a, queue_home_of(buf[0].kind), buf, n, 0);
        } else if (M.type == CT_QROW) {
            player_play_index(a, M.qid, M.qidx);
        } else {
            player_play_now(a, &M.item);
        }
        break;
    case CA_PLAY_NEXT:
        if (group) add_items(a, buf, n, false, true);
        else if (M.type == CT_QROW) {
            Queue *q = player_queue(M.qid);
            if (M.qidx != q->cur && M.qidx != q->cur + 1) {
                int to = M.qidx < q->cur ? q->cur : q->cur + 1;
                queue_move(q, M.qidx, SDL_clamp(to, 0, q->n - 1));
            }
        } else add_items(a, &M.item, 1, false, true);
        break;
    case CA_ADD_END:
    case CA_ADD_UNWATCHED:
        if (group) add_items(a, buf, n, false, false);
        else add_items(a, &M.item, 1, false, false);
        break;
    case CA_ADD_OTHER:
        if (group) add_items(a, buf, n, true, false);
        else add_items(a, &M.item, 1, true, false);
        break;
    case CA_PLAY_FROM_HERE: {
        Queue *q = player_queue(M.qid);
        if (M.qidx < q->n) player_play_index(a, M.qid, M.qidx);
        break;
    }
    case CA_REMOVE: {
        Queue *q = player_queue(M.qid);
        queue_remove(q, M.qidx);
        break;
    }
    case CA_MOVE_OTHER: {
        Queue *from = player_queue(M.qid), *to = player_queue(M.qid == QUEUE_VIDEO ? QUEUE_MUSIC : QUEUE_VIDEO);
        queue_transfer(from, M.qidx, to, false);
        break;
    }
    case CA_FORGET:
        history_remove(a->hist, M.item.rel, M.item.size);
        app_history_changed();
        app_toast(a, "Removed from history");
        break;
    case CA_GOTO: goto_item(a, &M.item); break;
    case CA_MARK_PLAYED: mark_item(a, &M.item, true); break;
    case CA_MARK_UNPLAYED: mark_item(a, &M.item, false); break;
    case CA_MODE_WINDOW:
    case CA_MODE_LISTEN:
    case CA_MODE_SKIP: {
        Queue *q = player_queue(M.qid);
        if (M.qidx < q->n) q->items[M.qidx].mode = act == CA_MODE_WINDOW ? QMODE_WINDOW : act == CA_MODE_LISTEN ? QMODE_LISTEN : QMODE_SKIP;
        break;
    }
    }
    SDL_free(buf);
}

bool queue_menu_open(void) { return M.open; }

void queue_menu_close(void) { M.open = false; }

/* Draw and run the menu; called last in the frame. */
void queue_menu_draw(App *a)
{
    if (!M.open) return;
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    int saved = ui->layer;
    ui->layer = UI_MODAL_LAYER;
    /* a click anywhere else closes it */
    SDL_FRect full = { 0, 0, (float)ui->w, (float)ui->h };
    if (ui_item(ui, ui_id("ctx-bg"), full).activated) { M.open = false; ui->layer = saved; return; }
    float rh = dp(ui, 42), w = dp(ui, 290), pad = dp(ui, 6);
    float h = M.n * rh + 2 * pad;
    SDL_FRect r = { M.x, M.y, w, h };
    if (r.x + r.w > ui->w - dp(ui, 8)) r.x = ui->w - dp(ui, 8) - r.w;
    if (r.y + r.h > ui->h - dp(ui, 8)) r.y = ui->h - dp(ui, 8) - r.h;
    if (r.x < dp(ui, 8)) r.x = dp(ui, 8);
    if (r.y < dp(ui, 8)) r.y = dp(ui, 8);
    ui_shadow(ui, r, dp(ui, 14), dp(ui, 28), rgba(0x000000, 170));
    ui_fill(ui, r, t->surface, dp(ui, 14));
    ui_stroke(ui, r, t->high_contrast ? t->border : with_alpha(t->border, 255), dp(ui, 14), dp(ui, t->high_contrast ? 2 : 1));
    int chosen = 0;
    for (int i = 0; i < M.n; i++) {
        SDL_FRect row = { r.x + pad, r.y + pad + i * rh, w - 2 * pad, rh };
        Uint32 id = ui_idi("ctx-row", i);
        if (M.fresh && i == 0) ui_focus(ui, id);
        UiItemState st = ui_item(ui, id, row);
        if (st.hover || st.focused) ui_fill(ui, row, t->surface_hi, dp(ui, 10));
        ui_text_center(ui, FONT_BOLD, TS_BODY, (SDL_FRect){ row.x + dp(ui, 12), row.y, row.w - dp(ui, 16), row.h }, t->text,
                       M.row[i].label, false);
        if (st.focused) ui_focus_ring(ui, row, dp(ui, 10));
        if (st.activated) chosen = M.row[i].act;
    }
    M.fresh = false;
    ui->layer = saved;
    ui->needs_redraw = true;
    if (chosen) {
        M.open = false;
        run_action(a, chosen);
    }
}

/* ------------------------------------------------------------------------ */
/* The panel                                                                  */
/* ------------------------------------------------------------------------ */

#define PANEL_DP 420
static int g_drag_from = -1;
static Uint32 g_row_focus_id;         /* id of the focused queue row (for Alt+Up/Down/Delete) */
static int g_row_focus_q = -1, g_row_focus_i = -1;

float queue_panel_width(App *a) { return a->queue_open ? dp(&a->ui, PANEL_DP) : 0; }

static const char *repeat_label(int r) { return r == REPEAT_ONE ? "Repeat one" : r == REPEAT_ALL ? "Repeat all" : "Repeat off"; }

static void SDLCALL export_chosen(void *ud, const char *const *list, int filter);
static void SDLCALL import_chosen(void *ud, const char *const *list, int filter);

void queue_panel(App *a, SDL_FRect r)
{
    if (!a->queue_open) return;
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    int saved = ui->layer;
    ui->layer = 1;
    ui_fill(ui, r, with_alpha(color_mix(t->bg, t->surface, 0.5f), 255), 0);
    ui_fill(ui, (SDL_FRect){ r.x, r.y, 1, r.h }, with_alpha(t->border, 255), 0);
    ui_item(ui, ui_id("queue-bg"), r);       /* swallows clicks on the empty parts */

    float pad = dp(ui, 16);
    float x = r.x + pad, w = r.w - 2 * pad, y = r.y + pad;
    ui_text(ui, FONT_BOLD, TS_TITLE, x, y, t->text, "Queue");
    if (ui_icon_button(ui, ui_id("queue-close"), (SDL_FRect){ r.x + r.w - pad - dp(ui, 40), y - dp(ui, 6), dp(ui, 40), dp(ui, 40) },
                       ICON_CLOSE, "Close the queue"))
        a->queue_open = false;
    y += ui_line_h(ui, TS_TITLE) + dp(ui, 10);

    /* tabs: which queue is shown. A dot marks the one that is playing. */
    for (int qid = 0; qid < QUEUE_COUNT; qid++) {
        Queue *q = player_queue(qid);
        char lbl[48];
        SDL_snprintf(lbl, sizeof lbl, "%s (%d)%s", qid == QUEUE_MUSIC ? "Music" : "Video", q->n, player_active_queue() == qid && player_has_item(a) ? " \xE2\x80\xA2" : "");
        float tw = (w - dp(ui, 8)) / 2;
        if (ui_pill(ui, ui_idi("queue-tab", qid), (SDL_FRect){ x + qid * (tw + dp(ui, 8)), y, tw, dp(ui, 38) }, lbl, g_tab == qid))
            g_tab = qid;
    }
    y += dp(ui, 38) + dp(ui, 12);
    Queue *q = player_queue(g_tab);

    /* controls */
    float bs = dp(ui, 40);
    float bx = x;
    if (ui_icon_button(ui, ui_id("queue-shuffle"), (SDL_FRect){ bx, y, bs, bs }, ICON_SHUFFLE, q->shuffle ? "Shuffle is on (upcoming only)" : "Shuffle the upcoming items")) {
        q->shuffle = !q->shuffle;
        a->set.queue_shuffle = q->shuffle;
        if (q->shuffle) queue_shuffle_upcoming(q, (uint32_t)SDL_GetTicks() | 1u);
        app_save_settings(a);
    }
    if (q->shuffle) ui_circle(ui, bx + bs - dp(ui, 6), y + dp(ui, 6), dp(ui, 4), t->accent);
    bx += bs + dp(ui, 4);
    if (ui_icon_button(ui, ui_id("queue-repeat"), (SDL_FRect){ bx, y, bs, bs }, q->repeat == REPEAT_ONE ? ICON_REPEAT_ONE : ICON_REPEAT,
                       repeat_label(q->repeat))) {
        q->repeat = (q->repeat + 1) % 3;
        a->set.queue_repeat = q->repeat;
        app_save_settings(a);
        app_toast(a, "%s", repeat_label(q->repeat));
    }
    if (q->repeat != REPEAT_OFF) ui_circle(ui, bx + bs - dp(ui, 6), y + dp(ui, 6), dp(ui, 4), t->accent);
    bx += bs + dp(ui, 4);
    {
        float pw = dp(ui, 84), ph = dp(ui, 34);
        static const SDL_DialogFileFilter f[] = { { "Playlist", "m3u8;m3u" } };
        if (ui_pill(ui, ui_id("queue-export"), (SDL_FRect){ bx + dp(ui, 4), y + (bs - ph) / 2, pw, ph }, "Export", false))
            SDL_ShowSaveFileDialog(export_chosen, (void *)(intptr_t)(g_tab + 1), a->win, f, 1,
                                   g_tab == QUEUE_VIDEO ? "thc-video-queue.m3u8" : "thc-music-queue.m3u8");
        bx += pw + dp(ui, 10);
        if (ui_pill(ui, ui_id("queue-import"), (SDL_FRect){ bx, y + (bs - ph) / 2, pw, ph }, "Import", false))
            SDL_ShowOpenFileDialog(import_chosen, (void *)(intptr_t)1, a->win, f, 1, NULL, false);
        bx += pw;
    }
    float cw = dp(ui, 96);
    if (ui_button(ui, ui_id("queue-clear"), (SDL_FRect){ x + w - cw, y, cw, bs }, "Clear", ICON_NONE, false) && q->n > 0)
        app_confirm(a, CONFIRM_CLEAR_QUEUE, g_tab, "Clear the queue?",
                    g_tab == QUEUE_VIDEO ? "This removes every video from the video queue, including the ones already played. Playback of the current video stops."
                                         : "This removes every item from the music queue, including the ones already played. Playback of the current item stops.",
                    "Clear");
    y += bs + dp(ui, 8);
    char st[96];
    if (q->n == 0) SDL_snprintf(st, sizeof st, "Nothing queued");
    else SDL_snprintf(st, sizeof st, "%d item%s \xC2\xB7 %d to play", q->n, q->n == 1 ? "" : "s", queue_upcoming(q));
    ui_text_fit(ui, FONT_REGULAR, TS_SMALL, x, y, w, t->text_dim, st);
    y += ui_line_h(ui, TS_SMALL) + dp(ui, 8);

    /* the list */
    SDL_FRect view = { r.x, y, r.w, r.y + r.h - y };
    float rh = dp(ui, 60);
    if (q->n == 0) {
        ui_text_wrap(ui, FONT_REGULAR, TS_BODY, x, y + dp(ui, 12), w, t->text_dim,
                     "Right-click anything in the library, or press A on it, to add it here. Add to the end, or play it next.", 4);
    }
    Uint32 sid = ui_idi("queue-list", g_tab);
    float off = ui_scroll_begin(ui, sid, view, false);
    g_row_focus_id = 0;
    int drop = -1;
    for (int i = 0; i < q->n; i++) {
        QueueItem *it = &q->items[i];
        SDL_FRect row = { x, y - off + i * rh, w, rh - dp(ui, 4) };
        if (row.y + row.h < view.y - rh || row.y > view.y + view.h + rh) {
            /* off screen: still counts for the content height */
            continue;
        }
        bool cur = i == q->cur;
        bool done = it->played && !cur;
        Uint32 id = ui_idi("qrow", g_tab * 100000 + i);
        UiItemState st = ui_item(ui, id, row);
        if (st.focused) { g_row_focus_id = id; g_row_focus_q = g_tab; g_row_focus_i = i; }
        float hv = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 16);
        Color bg = cur ? with_alpha(t->accent, t->dark ? 44 : 34) : color_mix(t->surface, t->surface_hi, hv);
        ui_fill(ui, row, bg, dp(ui, 12));
        if (st.focused) ui_focus_ring(ui, row, dp(ui, 12));
        /* grip: drag to reorder */
        SDL_FRect grip = { row.x + dp(ui, 4), row.y, dp(ui, 30), row.h };
        UiItemState gs = ui_item(ui, ui_idi("qgrip", g_tab * 100000 + i), grip);
        ui_icon(ui, ICON_GRIP, (SDL_FRect){ grip.x + dp(ui, 4), grip.y + (grip.h - dp(ui, 22)) / 2, dp(ui, 22), dp(ui, 22) }, t->text_dim);
        if (gs.pressed) {
            g_drag_from = i;
            drop = SDL_clamp((int)((ui->my - y + off) / rh), 0, q->n - 1);
            ui_fill(ui, (SDL_FRect){ row.x, y - off + drop * rh - dp(ui, 2), row.w, dp(ui, 3) }, t->accent, dp(ui, 2));
        }
        Color fg = done ? t->text_dim : t->text;
        float tx = row.x + dp(ui, 40);
        float rw = row.w - dp(ui, 40) - dp(ui, 52);
        ui_text_fit(ui, FONT_BOLD, TS_BODY, tx, row.y + dp(ui, 8), rw, cur ? t->accent : fg, it->title);
        char sub[260];
        const char *kind = it->kind == QK_VIDEO ? "Video" : it->kind == QK_BOOK ? "Audiobook" : "Song";
        if (it->kind == QK_VIDEO && g_tab == QUEUE_MUSIC) {
            static const char *const mn[] = { "window", "listen only", "skipped" };
            SDL_snprintf(sub, sizeof sub, "%s \xC2\xB7 %s%s%s", kind, mn[it->mode], it->sub[0] ? " \xC2\xB7 " : "", it->sub);
        } else {
            SDL_snprintf(sub, sizeof sub, "%s%s%s%s", it->sub, it->sub[0] ? " \xC2\xB7 " : "", kind, done ? " \xC2\xB7 played" : "");
        }
        ui_text_fit(ui, FONT_REGULAR, TS_SMALL, tx, row.y + dp(ui, 8) + ui_line_h(ui, TS_BODY), rw, t->text_dim, sub);
        if (cur) ui_icon(ui, ICON_PLAY, (SDL_FRect){ row.x + row.w - dp(ui, 84), row.y + (row.h - dp(ui, 18)) / 2, dp(ui, 18), dp(ui, 18) }, t->accent);
        /* remove */
        if (ui_icon_button(ui, ui_idi("qrm", g_tab * 100000 + i), (SDL_FRect){ row.x + row.w - dp(ui, 44), row.y + (row.h - dp(ui, 36)) / 2, dp(ui, 36), dp(ui, 36) },
                           ICON_CLOSE, "Remove from the queue")) {
            queue_remove(q, i);
            ui->layer = saved;
            ui_scroll_end(ui, q->n * rh + dp(ui, 24));
            return;
        }
        if (st.activated) player_play_index(a, g_tab, i);
        if (st.context) queue_menu_qrow(a, ui->mx, ui->my, g_tab, i);
        if (st.context && ui->key_context == false && ui->keyboard_mode) { M.x = row.x + row.w / 2; M.y = row.y + row.h; }
    }
    ui_scroll_end(ui, q->n * rh + dp(ui, 24));
    /* the drop: the mouse button was released after dragging a grip */
    if (g_drag_from >= 0 && !ui->mouse_down) {
        int to = SDL_clamp((int)((ui->my - y + off) / rh), 0, q->n - 1);
        if (to != g_drag_from) queue_move(q, g_drag_from, to);
        g_drag_from = -1;
    }
    ui->layer = saved;
}

/* Keyboard actions on the queue: toggle the panel, add the focused thing, move or remove a row. */
bool queue_hotkey(App *a, Action act)
{
    switch (act) {
    case ACT_QUEUE_PANEL:
        a->queue_open = !a->queue_open;
        return true;
    case ACT_QUEUE_ADD:
    case ACT_QUEUE_PLAY_NEXT:
        if (a->focus_file && !a->focus_file->dir) {
            add_node(a, a->focus_root, a->focus_file, false, act == ACT_QUEUE_PLAY_NEXT);
            return true;
        }
        return false;
    case ACT_QUEUE_UP:
    case ACT_QUEUE_DOWN:
    case ACT_QUEUE_REMOVE:
        if (a->queue_open && g_row_focus_id && a->ui.focus == g_row_focus_id) {
            Queue *q = player_queue(g_row_focus_q);
            int i = g_row_focus_i;
            if (act == ACT_QUEUE_REMOVE) {
                queue_remove(q, i);
            } else {
                int to = i + (act == ACT_QUEUE_UP ? -1 : 1);
                if (queue_move(q, i, to)) {
                    g_row_focus_i = to;
                    ui_focus(&a->ui, ui_idi("qrow", g_row_focus_q * 100000 + to));
                }
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------------ */
/* Add buttons used by the portals                                            */
/* ------------------------------------------------------------------------ */

void queue_add_dir(App *a, int root, const FsNode *dir, bool next)
{
    QueueItem *buf = SDL_malloc(sizeof *buf * MAX_GROUP);
    if (!buf) return;
    int n = player_items_for(a, root, dir, buf, MAX_GROUP);
    add_items(a, buf, n, false, next);
    SDL_free(buf);
}

void queue_add_show(App *a, const char *show_key, int season, bool unwatched_only)
{
    QueueItem *buf = SDL_malloc(sizeof *buf * MAX_GROUP);
    if (!buf) return;
    int n = show_items(a, show_key, season, unwatched_only || a->set.queue_season_unwatched_only, buf, MAX_GROUP);
    if (!n) app_toast(a, "Nothing to add: everything here is watched");
    add_items(a, buf, n, false, false);
    SDL_free(buf);
}

void queue_add_file(App *a, int root, const FsNode *f, bool next)
{
    add_node(a, root, f, false, next);
}

/* ------------------------------------------------------------------------ */
/* Export and import                                                          */
/* ------------------------------------------------------------------------ */

static SDL_Mutex *g_mu;
static char g_pending_import[THC_PATH_MAX];

static void SDLCALL export_chosen(void *ud, const char *const *list, int filter)
{
    (void)filter;
    if (!list || !list[0]) return;
    int qid = (int)(intptr_t)ud - 1;
    FILE *f = fopen(list[0], "wb");
    /* The queue is only read here, on the dialog's thread, in one short pass. */
    bool ok = f && queue_export_m3u8(player_queue(qid), f, true);
    if (f) fclose(f);
    SDL_Log("queue export to %s: %s", list[0], ok ? "ok" : "failed");
}

static void SDLCALL import_chosen(void *ud, const char *const *list, int filter)
{
    (void)ud;
    (void)filter;
    if (!list || !list[0]) return;
    if (!g_mu) return;
    SDL_LockMutex(g_mu);
    SDL_strlcpy(g_pending_import, list[0], sizeof g_pending_import);
    SDL_UnlockMutex(g_mu);
}

typedef struct ImportCtx { App *a; } ImportCtx;

static bool is_abs(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

static bool import_resolve(void *ud, int root, int kind, const char *line, QueueItem *out)
{
    App *a = ((ImportCtx *)ud)->a;
    (void)kind;
    char full[THC_PATH_MAX];
    const FsNode *node = NULL;
    int found_root = -1;
    if (is_abs(line)) {
        SDL_strlcpy(full, line, sizeof full);
    } else {
        /* relative to a library folder: the root the file says, else try them in turn */
        static const int order[] = { ROOT_VIDEO, ROOT_MUSIC, ROOT_BOOKS };
        bool ok = false;
        for (int i = -1; i < 3 && !ok; i++) {
            int r = i < 0 ? root : order[i];
            if (r < 0 || r >= ROOT_COUNT || !a->lib.path[r][0]) continue;
            SDL_snprintf(full, sizeof full, "%s/%s", a->lib.path[r], line);
            SDL_PathInfo info;
            if (SDL_GetPathInfo(full, &info) && info.type == SDL_PATHTYPE_FILE) { ok = true; found_root = r; }
        }
        if (!ok) return false;
    }
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(full, &info) || info.type != SDL_PATHTYPE_FILE) return false;
    (void)node;
    out->root = found_root >= 0 ? found_root : root;
    SDL_strlcpy(out->path, full, sizeof out->path);
    SDL_strlcpy(out->rel, is_abs(line) ? "" : line, sizeof out->rel);
    out->size = (Sint64)info.size;
    out->mode = QMODE_WINDOW;
    if (out->kind < 0) {
        MediaKind mk = media_kind(full);
        out->kind = mk == MK_VIDEO ? QK_VIDEO : mk == MK_BOOK ? QK_BOOK : QK_SONG;
    }
    if (!out->title[0]) {
        const char *name = SDL_strrchr(full, '/');
        SDL_strlcpy(out->title, name ? name + 1 : full, sizeof out->title);
    }
    return true;
}

static void do_import(App *a, const char *path)
{
    size_t len = 0;
    char *text = SDL_LoadFile(path, &len);
    if (!text) { app_toast(a, "Could not read that file"); return; }
    char *z = SDL_malloc(len + 1);
    if (!z) { SDL_free(text); return; }
    memcpy(z, text, len);
    z[len] = 0;
    SDL_free(text);
    /* the file says which queue it was exported from; otherwise the tab being shown */
    Queue tmp;
    queue_init(&tmp, g_tab);
    QueueImport imp;
    ImportCtx ctx = { a };
    queue_import_m3u8(&tmp, z, import_resolve, &ctx, &imp);
    SDL_free(z);
    int target = imp.home >= 0 ? imp.home : g_tab;
    Queue *q = player_queue(target);
    for (int i = 0; i < tmp.n; i++) queue_add(q, &tmp.items[i], false);
    queue_free(&tmp);
    g_tab = target;
    if (imp.missing) {
        char names[400] = "";
        for (int i = 0; i < imp.missing && i < 3; i++) {
            SDL_strlcat(names, i ? ", " : "", sizeof names);
            SDL_strlcat(names, imp.missing_names[i], sizeof names);
        }
        app_toast(a, "Imported %d. Missing and skipped: %d (%s)", imp.added, imp.missing, names);
    } else {
        app_toast(a, "Imported %d item%s", imp.added, imp.added == 1 ? "" : "s");
    }
    a->queue_open = true;
}

void queue_poll(App *a)
{
    if (!g_mu) g_mu = SDL_CreateMutex();
    char path[THC_PATH_MAX] = "";
    SDL_LockMutex(g_mu);
    if (g_pending_import[0]) {
        SDL_strlcpy(path, g_pending_import, sizeof path);
        g_pending_import[0] = 0;
    }
    SDL_UnlockMutex(g_mu);
    if (path[0]) do_import(a, path);
}

/* ------------------------------------------------------------------------ */
/* The backlog kept between sessions (only when the user turns that on)       */
/* ------------------------------------------------------------------------ */

void queue_backlog_save(App *a)
{
    if (!a->hist) return;
    for (int qid = 0; qid < QUEUE_COUNT; qid++) {
        Queue *q = player_queue(qid);
        if (!a->set.queue_keep_backlog) {
            history_backlog_save(a->hist, qid, NULL, 0);
            continue;
        }
        BacklogRow *rows = SDL_malloc(sizeof *rows * 500);
        if (!rows) continue;
        int n = 0;
        int end = q->cur >= 0 ? q->cur + 1 : 0;      /* the played items and the current one */
        int start = end > 500 ? end - 500 : 0;
        for (int i = start; i < end && n < 500; i++) {
            const QueueItem *it = &q->items[i];
            if (!it->played && i != q->cur) continue;
            if (!settings_history_on(&a->set, it->kind)) continue;     /* off for that portal: never saved */
            SDL_strlcpy(rows[n].path, it->rel, sizeof rows[n].path);
            rows[n].size = it->size;
            rows[n].kind = it->kind;
            SDL_strlcpy(rows[n].title, it->title, sizeof rows[n].title);
            rows[n].played_at = 0;
            n++;
        }
        history_backlog_save(a->hist, qid, rows, n);
        SDL_free(rows);
    }
}

void queue_backlog_restore(App *a)
{
    if (!a->hist || !a->set.queue_keep_backlog) return;
    for (int qid = 0; qid < QUEUE_COUNT; qid++) {
        Queue *q = player_queue(qid);
        if (q->n > 0) continue;
        BacklogRow *rows = SDL_malloc(sizeof *rows * 500);
        if (!rows) continue;
        int n = history_backlog_load(a->hist, qid, rows, 500);
        for (int i = 0; i < n; i++) {
            if (!settings_history_on(&a->set, rows[i].kind)) continue;
            static const int order[] = { ROOT_VIDEO, ROOT_MUSIC, ROOT_BOOKS };
            QueueItem it;
            SDL_zero(it);
            for (int k = 0; k < 3; k++) {
                int r = order[k];
                if ((rows[i].kind == QK_VIDEO) != (r == ROOT_VIDEO)) continue;
                if ((rows[i].kind == QK_BOOK) != (r == ROOT_BOOKS)) continue;
                if (!a->lib.path[r][0]) continue;
                char full[THC_PATH_MAX];
                SDL_snprintf(full, sizeof full, "%s/%s", a->lib.path[r], rows[i].path);
                SDL_PathInfo info;
                if (!SDL_GetPathInfo(full, &info) || info.type != SDL_PATHTYPE_FILE) continue;
                it.kind = rows[i].kind;
                it.root = r;
                SDL_strlcpy(it.path, full, sizeof it.path);
                SDL_strlcpy(it.rel, rows[i].path, sizeof it.rel);
                SDL_strlcpy(it.title, rows[i].title, sizeof it.title);
                it.size = (Sint64)info.size;
                it.played = true;
                if (queue_add(q, &it, false) >= 0) q->items[q->n - 1].played = true;
                break;
            }
        }
        SDL_free(rows);
        if (q->n > 0) q->cur = q->n - 1;     /* the last one played is "current" (not playing) */
    }
}
