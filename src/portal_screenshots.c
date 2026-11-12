/* portal_screenshots.c: the Screenshots tab. A grid of the pictures MPV's s key saved into THC's folder,
 * newest first. Select one to see it larger; open its folder, copy its path or delete it (#25). */
#include "app.h"
#include "screenshots.h"

#define MAX_SHOTS 600

/* Two views of the same code: the Screenshots tab, and the NSFW portal's own folder (screenshots/nsfw). */
static ShotInfo g_shots_s[2][MAX_SHOTS];
static int g_n_s[2];
static Uint64 g_loaded_s[2];
static int g_sel_s[2] = { -1, -1 };      /* the picture shown large, or -1 */
static char g_pending_delete[THC_PATH_MAX];
static char g_pending_root[THC_PATH_MAX];
static int g_mode;                       /* 0 = normal, 1 = NSFW */
#define g_shots g_shots_s[g_mode]
#define g_n g_n_s[g_mode]
#define g_loaded_at g_loaded_s[g_mode]
#define g_sel g_sel_s[g_mode]

static void shots_dir(const App *a, char *out, size_t cap)
{
    screenshot_dir_resolve(a->data_dir, a->set.screenshot_dir, out, cap);
    if (g_mode == 1) {
        size_t n = SDL_strlen(out);
        SDL_snprintf(out + n, cap - n, "/nsfw");
    }
}

static void refresh(App *a, bool force)
{
    Uint64 now = SDL_GetTicks();
    if (!force && g_loaded_at && now - g_loaded_at < 2000) return;
    g_loaded_at = now ? now : 1;
    char dir[THC_PATH_MAX];
    shots_dir(a, dir, sizeof dir);
    g_n = screenshots_list(dir, g_shots, MAX_SHOTS);
    if (g_sel >= g_n) g_sel = -1;
}

bool screenshots_back_nsfw(void)
{
    g_mode = 1;
    bool r = g_sel >= 0;
    if (r) g_sel = -1;
    g_mode = 0;
    return r;
}

bool screenshots_back(void)
{
    g_mode = 0;
    if (g_sel < 0) return false;
    g_sel = -1;
    return true;
}

void screenshots_confirmed_delete(App *a)
{
    g_mode = g_pending_root[0] == 'n' ? 1 : 0;
    char dir[THC_PATH_MAX];
    shots_dir(a, dir, sizeof dir);
    if (screenshot_delete(dir, g_pending_delete)) {
        art_clear();                      /* the picture is gone from the cache, too */
        app_toast(a, "Screenshot deleted");
        g_sel = -1;
    } else {
        app_toast(a, "That file is not in THC's screenshots folder, so it was not deleted");
    }
    g_pending_delete[0] = 0;
    refresh(a, true);
}

static void full_path(const App *a, const ShotInfo *s, char *out, size_t cap)
{
    char dir[THC_PATH_MAX];
    shots_dir(a, dir, sizeof dir);
    SDL_snprintf(out, cap, "%s/%s", dir, s->name);
}

static void open_dir(App *a)
{
    char dir[THC_PATH_MAX], url[THC_PATH_MAX + 16];
    shots_dir(a, dir, sizeof dir);
    SDL_CreateDirectory(dir);
    SDL_snprintf(url, sizeof url, "file://%s%s", dir[0] == '/' ? "" : "/", dir);
    if (!SDL_OpenURL(url)) app_toast(a, "Could not open the folder");
}

static void viewer(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    const ShotInfo *s = &g_shots[g_sel];
    char path[THC_PATH_MAX];
    full_path(a, s, path, sizeof path);
    float pad = dp(ui, 32);
    float top = area.y + dp(ui, 20);
    if (ui_button(ui, ui_id("shot-back"), (SDL_FRect){ area.x + pad, top, dp(ui, 120), dp(ui, 44) }, "Back", ICON_BACK, false))
        g_sel = -1;
    float bx = area.x + pad + dp(ui, 132);
    if (ui_button(ui, ui_id("shot-folder"), (SDL_FRect){ bx, top, dp(ui, 190), dp(ui, 44) }, "Open folder", ICON_FOLDER, false))
        open_dir(a);
    bx += dp(ui, 202);
    if (ui_button(ui, ui_id("shot-copy"), (SDL_FRect){ bx, top, dp(ui, 170), dp(ui, 44) }, "Copy path", ICON_CHECK, false)) {
        SDL_SetClipboardText(path);
        app_toast(a, "Path copied");
    }
    bx += dp(ui, 182);
    if (ui_button(ui, ui_id("shot-delete"), (SDL_FRect){ bx, top, dp(ui, 150), dp(ui, 44) }, "Delete", ICON_CLOSE, false)) {
        SDL_strlcpy(g_pending_delete, path, sizeof g_pending_delete);
        g_pending_root[0] = g_mode == 1 ? 'n' : 'm';
        app_confirm(a, CONFIRM_DELETE_SHOT, 0, "Delete this screenshot?",
                    "The picture is removed from THC's screenshots folder. This cannot be undone.", "Delete");
    }
    float y = top + dp(ui, 60);
    SDL_FRect box = { area.x + pad, y, area.w - 2 * pad, area.y + area.h - y - dp(ui, 70) };
    SDL_Texture *tex = art_get(a, path);
    if (tex && box.w > 0 && box.h > 0) {
        float tw = 0, th = 0;
        SDL_GetTextureSize(tex, &tw, &th);
        if (tw > 0 && th > 0) {
            float sc = SDL_min(box.w / tw, box.h / th);
            SDL_FRect r = { box.x + (box.w - tw * sc) / 2, box.y + (box.h - th * sc) / 2, tw * sc, th * sc };
            ui_image(ui, tex, r, dp(ui, 8), 255);
        }
    } else {
        ui_fill(ui, box, t->surface, dp(ui, 12));
        ui_text_center(ui, FONT_REGULAR, TS_BODY, box, t->text_dim, tex ? "" : "Loading", false);
    }
    char line[400];
    if (s->at[0]) SDL_snprintf(line, sizeof line, "%s \xC2\xB7 %s", s->title, s->at);
    else SDL_strlcpy(line, s->title, sizeof line);
    ui_text_fit(ui, FONT_BOLD, TS_BODY, area.x + pad, box.y + box.h + dp(ui, 14), area.w - 2 * pad, t->text, line);
}

static void portal_screenshots_impl(App *a, SDL_FRect area);

void portal_screenshots(App *a, SDL_FRect area)
{
    g_mode = 0;
    portal_screenshots_impl(a, area);
}

/* The NSFW portal's Screenshots sub-tab: only the pictures taken while an NSFW item played. */
void portal_screenshots_nsfw(App *a, SDL_FRect area)
{
    g_mode = 1;
    portal_screenshots_impl(a, area);
    g_mode = 0;
}

int screenshots_nsfw_count(App *a)
{
    g_mode = 1;
    refresh(a, false);
    int n = g_n;
    g_mode = 0;
    return n;
}

static void portal_screenshots_impl(App *a, SDL_FRect area)
{
    Ui *ui = &a->ui;
    const Theme *t = &ui->th;
    refresh(a, false);
    if (g_n == 0) {
        g_sel = -1;
        char dir[THC_PATH_MAX], body[THC_PATH_MAX + 200];
        shots_dir(a, dir, sizeof dir);
        SDL_snprintf(body, sizeof body,
                     "Press S while a video plays in MPV and the picture is saved here, in THC's own folder (%s), never next to your videos.",
                     dir);
        bool open = false;
        draw_empty(a, area, g_mode == 1 ? ICON_BEER : ICON_CAMERA, "No screenshots yet", body, "Open folder", &open);
        if (open) open_dir(a);
        ui->needs_redraw = true;       /* keep looking for new pictures */
        return;
    }
    if (g_sel >= 0) {
        viewer(a, area);
        return;
    }
    float pad = SDL_max(dp(ui, 32), (area.w - dp(ui, 1400)) / 2);
    float x = area.x + pad, w = area.w - 2 * pad;
    float off = ui_scroll_begin(ui, ui_id("shots-page"), area, false);
    float y = area.y + dp(ui, 28) - off;
    ui_text(ui, FONT_BOLD, TS_H1, x, y, t->text, g_mode == 1 ? "Screenshots (NSFW)" : "Screenshots");
    y += ui_line_h(ui, TS_H1);
    char sub[64];
    SDL_snprintf(sub, sizeof sub, "%d picture%s, newest first", g_n, g_n == 1 ? "" : "s");
    ui_text(ui, FONT_REGULAR, TS_BODY, x, y, t->text_dim, sub);
    y += ui_line_h(ui, TS_BODY) + dp(ui, 24);

    float gap = dp(ui, 20), cw = dp(ui, 280);
    int cols = (int)SDL_max(1.0f, (w + gap) / (cw + gap));
    cw = (w - gap * (float)(cols - 1)) / (float)cols;
    float ch = cw * 9.0f / 16.0f;
    for (int i = 0; i < g_n; i++) {
        int col = i % cols;
        float cx = x + (float)col * (cw + gap);
        SDL_FRect r = { cx, y, cw, ch };
        Uint32 id = ui_idi("shot", i);
        UiItemState st = ui_item(ui, id, r);
        if (ui_visible(ui, r)) {
            char path[THC_PATH_MAX];
            full_path(a, &g_shots[i], path, sizeof path);
            float lift = ui_anim_id(ui, id, (st.hover || st.focused) ? 1.0f : 0.0f, 14);
            SDL_FRect rr = { r.x - lift * dp(ui, 3), r.y - lift * dp(ui, 3), r.w + lift * dp(ui, 6), r.h + lift * dp(ui, 6) };
            draw_art(a, rr, g_shots[i].title, path, dp(ui, 12));
            if (st.focused) ui_focus_ring(ui, rr, dp(ui, 12));
            ui_text_fit(ui, FONT_BOLD, TS_SMALL, r.x, r.y + r.h + dp(ui, 8), r.w, t->text, g_shots[i].title);
            if (g_shots[i].at[0])
                ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x, r.y + r.h + dp(ui, 8) + ui_line_h(ui, TS_SMALL), r.w, t->text_dim,
                            g_shots[i].at);
        }
        if (st.activated) g_sel = i;
        if (col == cols - 1 || i == g_n - 1) y += ch + dp(ui, 8) + ui_line_h(ui, TS_SMALL) * 2 + dp(ui, 20);
    }
    ui_scroll_end(ui, y + off - area.y);
}
