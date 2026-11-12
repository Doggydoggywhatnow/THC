#include "ui.h"

#include <math.h>
#include <string.h>

#include "stb_truetype.h"

/* Bundled fonts (generated from assets/fonts at build time). */
extern const unsigned char thc_font_regular[];
extern const unsigned int thc_font_regular_len;
extern const unsigned char thc_font_bold[];
extern const unsigned int thc_font_bold_len;

/* Controls that use Left/Right themselves (sliders, segmented controls, text
 * fields): while one has focus, those keys adjust it instead of moving focus. */
static Uint32 g_capture_lr, g_capture_lr_next;
static Uint32 g_text_focus_next;
static int g_lr_pending;

/* ------------------------------------------------------------------------ */
/* Colors and themes                                                          */
/* ------------------------------------------------------------------------ */

Color color_mix(Color a, Color b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    Color c = {
        (Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t),
        (Uint8)(a.b + (b.b - a.b) * t), (Uint8)(a.a + (b.a - a.a) * t),
    };
    return c;
}

const char *const k_accent_names[ACCENT_PRESET_COUNT] = {
    "Portal default", "Ember", "Rose", "Ocean", "Mint", "Violet", "Gold"
};

/* Accent pairs for dark and light backgrounds (light ones are darker so they
 * keep at least 4.5:1 contrast against white). */
static const Uint32 k_accent_dark[ACCENT_PRESET_COUNT][2] = {
    { 0, 0 }, { 0xFF8A3D, 0xFF4D6D }, { 0xFF5C8A, 0xC74BFF }, { 0x4DA8FF, 0x37E0D0 },
    { 0x5EE6B4, 0x4DA8FF }, { 0xB18CFF, 0xFF6FB5 }, { 0xFFC94D, 0xFF8A3D },
};
static const Uint32 k_accent_light[ACCENT_PRESET_COUNT][2] = {
    { 0, 0 }, { 0xC2410C, 0xBE123C }, { 0xBE185D, 0x7E22CE }, { 0x1D4ED8, 0x0E7490 },
    { 0x047857, 0x1D4ED8 }, { 0x6D28D9, 0xBE185D }, { 0x92400E, 0xC2410C },
};

Theme theme_make(int portal, bool dark, bool hc, int accent)
{
    Theme t;
    memset(&t, 0, sizeof t);
    t.dark = dark;
    t.high_contrast = hc;
    if (hc) {
        if (dark) {
            t.bg = t.bg2 = rgb(0x000000);
            t.surface = rgb(0x000000);
            t.surface_hi = rgb(0x1A1A1A);
            t.border = rgb(0xFFFFFF);
            t.text = rgb(0xFFFFFF);
            t.text_dim = rgb(0xE6E6E6);
            t.accent = t.accent2 = rgb(0xFFE500);
            t.on_accent = rgb(0x000000);
            t.focus = rgb(0x00E5FF);
            t.watched = rgb(0x00FF85);
            t.track = rgb(0x5C5C5C);
        } else {
            t.bg = t.bg2 = rgb(0xFFFFFF);
            t.surface = rgb(0xFFFFFF);
            t.surface_hi = rgb(0xEDEDED);
            t.border = rgb(0x000000);
            t.text = rgb(0x000000);
            t.text_dim = rgb(0x1A1A1A);
            t.accent = t.accent2 = rgb(0x0033CC);
            t.on_accent = rgb(0xFFFFFF);
            t.focus = rgb(0xD10000);
            t.watched = rgb(0x006B2E);
            t.track = rgb(0xA0A0A0);
        }
        t.shadow = rgba(0, 0);
        return t;
    }
    switch (portal) {
    case PORTAL_MUSIC: /* "Vinyl": plum night with mint and violet */
        if (dark) {
            t.bg = rgb(0x1A1024); t.bg2 = rgb(0x0B0910);
            t.surface = rgb(0x221A2E); t.surface_hi = rgb(0x2F2540); t.border = rgb(0x3A2F4D);
            t.text = rgb(0xF5F0FA); t.text_dim = rgb(0xB5A9C7);
            t.accent = rgb(0x6EE7C5); t.accent2 = rgb(0xB18CFF);
        } else {
            t.bg = rgb(0xF6F1FB); t.bg2 = rgb(0xECE4F5);
            t.surface = rgb(0xFFFFFF); t.surface_hi = rgb(0xF1EAF9); t.border = rgb(0xDCD0EA);
            t.text = rgb(0x1E1429); t.text_dim = rgb(0x5B4E6B);
            t.accent = rgb(0x047857); t.accent2 = rgb(0x6D28D9);
        }
        break;
    case PORTAL_NSFW: /* "After hours": wine and neon, deliberately unlike the three public portals */
        if (dark) {
            t.bg = rgb(0x1C0A14); t.bg2 = rgb(0x0A0408);
            t.surface = rgb(0x2A1220); t.surface_hi = rgb(0x3A1A2D); t.border = rgb(0x4D2640);
            t.text = rgb(0xFBEFF4); t.text_dim = rgb(0xC9A6B8);
            t.accent = rgb(0xFF4F9A); t.accent2 = rgb(0xFFB020);
        } else {
            t.bg = rgb(0xFFF1F5); t.bg2 = rgb(0xFBE3EB);
            t.surface = rgb(0xFFFFFF); t.surface_hi = rgb(0xFCE9F0); t.border = rgb(0xEFC6D5);
            t.text = rgb(0x2A0E1B); t.text_dim = rgb(0x6B3F54);
            t.accent = rgb(0xBE185D); t.accent2 = rgb(0xB45309);
        }
        break;
    case PORTAL_BOOKS: /* "Library": warm paper and ink */
        if (dark) {
            t.bg = rgb(0x17130F); t.bg2 = rgb(0x0D0B09);
            t.surface = rgb(0x221D17); t.surface_hi = rgb(0x2E271F); t.border = rgb(0x3B3328);
            t.text = rgb(0xF7F1E8); t.text_dim = rgb(0xBDB09C);
            t.accent = rgb(0xF2B866); t.accent2 = rgb(0xE07A5F);
        } else {
            t.bg = rgb(0xFAF6EF); t.bg2 = rgb(0xF1EADF);
            t.surface = rgb(0xFFFFFF); t.surface_hi = rgb(0xF5EFE4); t.border = rgb(0xE2D7C6);
            t.text = rgb(0x231B12); t.text_dim = rgb(0x5E5243);
            t.accent = rgb(0x9A3412); t.accent2 = rgb(0xB45309);
        }
        break;
    case PORTAL_VIDEO: /* "Cinema": deep navy with an ember glow */
    default:
        if (dark) {
            t.bg = rgb(0x111421); t.bg2 = rgb(0x08090F);
            t.surface = rgb(0x1A1E2E); t.surface_hi = rgb(0x252A3F); t.border = rgb(0x2E3450);
            t.text = rgb(0xF3F4F8); t.text_dim = rgb(0xA6ACC2);
            t.accent = rgb(0xFF8A3D); t.accent2 = rgb(0xFF4D6D);
        } else {
            t.bg = rgb(0xF5F6FA); t.bg2 = rgb(0xE9EBF2);
            t.surface = rgb(0xFFFFFF); t.surface_hi = rgb(0xEEF0F6); t.border = rgb(0xD6DAE6);
            t.text = rgb(0x151824); t.text_dim = rgb(0x4F5568);
            t.accent = rgb(0xC2410C); t.accent2 = rgb(0xBE123C);
        }
        break;
    }
    if (accent > 0 && accent < ACCENT_PRESET_COUNT) {
        const Uint32 *p = dark ? k_accent_dark[accent] : k_accent_light[accent];
        t.accent = rgb(p[0]);
        t.accent2 = rgb(p[1]);
    }
    t.on_accent = dark ? rgb(0x10121A) : rgb(0xFFFFFF);
    t.focus = dark ? rgb(0xFFFFFF) : rgb(0x111111);
    t.watched = dark ? rgb(0x3DDC97) : rgb(0x047857);
    t.track = dark ? rgba(0xFFFFFF, 46) : rgba(0x000000, 36);
    t.shadow = dark ? rgba(0x000000, 150) : rgba(0x1E2440, 50);
    return t;
}

void art_colors(const char *seed, Color *a, Color *b)
{
    Uint32 h = ui_id(seed ? seed : "");
    static const Uint32 pal[][2] = {
        { 0xFF8A3D, 0x9B2C5A }, { 0x4DA8FF, 0x3B1F7A }, { 0x5EE6B4, 0x14576B },
        { 0xB18CFF, 0x2D1B69 }, { 0xFF5C8A, 0x5B1A4A }, { 0xFFC94D, 0xA3361F },
        { 0x37E0D0, 0x1D3A8A }, { 0xF472B6, 0x4C1D95 }, { 0xA3E635, 0x14532D },
        { 0x60A5FA, 0x831843 }, { 0xFB923C, 0x1E3A8A }, { 0x2DD4BF, 0x581C87 },
    };
    int n = (int)(sizeof pal / sizeof pal[0]);
    *a = rgb(pal[h % n][0]);
    *b = rgb(pal[(h / 7) % n][1]);
}

/* ------------------------------------------------------------------------ */
/* Fonts and the glyph cache                                                  */
/* ------------------------------------------------------------------------ */

#define ATLAS 2048
#define GLYPH_SLOTS 16384
#define MAX_FALLBACK 8

typedef struct Font {
    unsigned char *data;
    bool owned;
    bool ok;
    stbtt_fontinfo info;
} Font;

typedef struct Glyph {
    Uint64 key;
    Sint16 x, y, w, h, xoff, yoff;
    float adv;
} Glyph;

static Font g_font[2];
static Font g_fallback[MAX_FALLBACK];
static int g_nfallback;
static bool g_fallback_scanned;
static Glyph *g_glyphs;
static SDL_Texture *g_atlas;
static int g_ax, g_ay, g_arow;
static Uint8 *g_scratch;

static bool font_from_memory(Font *f, unsigned char *data, bool owned, int index)
{
    memset(f, 0, sizeof *f);
    int off = stbtt_GetFontOffsetForIndex(data, index);
    if (off < 0 || !stbtt_InitFont(&f->info, data, off)) {
        if (owned) SDL_free(data);
        return false;
    }
    f->data = data;
    f->owned = owned;
    f->ok = true;
    return true;
}

static bool font_from_file(Font *f, const char *path, int index)
{
    size_t len = 0;
    unsigned char *data = SDL_LoadFile(path, &len);
    if (!data) return false;
    return font_from_memory(f, data, true, index);
}

static void font_free(Font *f)
{
    if (f->owned) SDL_free(f->data);
    memset(f, 0, sizeof *f);
}

/* System fonts that cover scripts the bundled font does not (CJK, Cyrillic,
 * Greek, Arabic, Hebrew, Thai, symbols). Loaded the first time they are needed. */
static void scan_fallbacks(void)
{
    static const char *const paths[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\YuGothM.ttc",
        "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\malgun.ttf",
        "C:\\Windows\\Fonts\\seguisym.ttf", "C:\\Windows\\Fonts\\arial.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/Library/Fonts/Arial Unicode.ttf", "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
#endif
        NULL
    };
    g_fallback_scanned = true;
    for (int i = 0; paths[i] && g_nfallback < MAX_FALLBACK; i++)
        if (font_from_file(&g_fallback[g_nfallback], paths[i], 0)) g_nfallback++;
}

static void atlas_reset(Ui *ui)
{
    if (g_atlas) SDL_FlushRenderer(ui->ren);
    memset(g_glyphs, 0, sizeof(Glyph) * GLYPH_SLOTS);
    g_ax = g_ay = g_arow = 0;
}

static Glyph *glyph_slot(Uint64 key)
{
    Uint64 h = key * 0x9E3779B97F4A7C15ull;
    Uint32 i = (Uint32)(h >> 40) & (GLYPH_SLOTS - 1);
    for (int n = 0; n < GLYPH_SLOTS; n++) {
        Glyph *g = &g_glyphs[i];
        if (g->key == key || g->key == 0) return g;
        i = (i + 1) & (GLYPH_SLOTS - 1);
    }
    return NULL;
}

static Font *font_for(int font, int slot)
{
    if (slot == 0) return &g_font[font];
    return &g_fallback[slot - 1];
}

/* Find (or render) a glyph. `slot_out` is the font that had it. */
static Glyph *get_glyph(Ui *ui, int font, int px, Uint32 cp, int *slot_out)
{
    int slot = 0;
    Font *f = &g_font[font];
    int gi = stbtt_FindGlyphIndex(&f->info, (int)cp);
    if (!gi && cp > 32) {
        if (!g_fallback_scanned) scan_fallbacks();
        for (int i = 0; i < g_nfallback; i++) {
            int fi = stbtt_FindGlyphIndex(&g_fallback[i].info, (int)cp);
            if (fi) { slot = i + 1; gi = fi; f = &g_fallback[i]; break; }
        }
    }
    *slot_out = slot;
    Uint64 key = ((Uint64)(font * 16 + slot + 1) << 48) | ((Uint64)px << 32) | cp;
    Glyph *g = glyph_slot(key);
    if (!g) { atlas_reset(ui); g = glyph_slot(key); }
    if (g->key == key) return g;

    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(&f->info, gi, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&f->info, gi, scale, scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
        if (w + 2 > ATLAS || h + 2 > ATLAS) w = h = 0;
        if (g_ax + w + 1 > ATLAS) { g_ax = 0; g_ay += g_arow + 1; g_arow = 0; }
        if (g_ay + h + 1 > ATLAS) {
            atlas_reset(ui);
            g = glyph_slot(key);
        }
    }
    g->key = key;
    g->adv = adv * scale;
    g->xoff = (Sint16)x0;
    g->yoff = (Sint16)y0;
    g->w = (Sint16)w;
    g->h = (Sint16)h;
    if (w > 0 && h > 0) {
        stbtt_MakeGlyphBitmap(&f->info, g_scratch, w, h, w, scale, scale, gi);
        Uint32 *rgba32 = (Uint32 *)(g_scratch + ATLAS * ATLAS / 4);
        for (int i = 0; i < w * h; i++) {
            Uint8 a = g_scratch[i];
            Uint8 *p = (Uint8 *)&rgba32[i];
            p[0] = p[1] = p[2] = 255;
            p[3] = a;
        }
        SDL_Rect dst = { g_ax, g_ay, w, h };
        SDL_UpdateTexture(g_atlas, &dst, rgba32, w * 4);
        g->x = (Sint16)g_ax;
        g->y = (Sint16)g_ay;
        g_ax += w + 1;
        if (h > g_arow) g_arow = h;
    }
    return g;
}

float ui_font_px(const Ui *ui, float size_dp)
{
    float px = size_dp * ui->s * ui->text_scale;
    return px < 6 ? 6 : px;
}

static int font_px(const Ui *ui, float size) { return (int)(ui_font_px(ui, size) + 0.5f); }

float ui_ascent(Ui *ui, float size)
{
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font[0].info, &asc, &desc, &gap);
    return asc * stbtt_ScaleForMappingEmToPixels(&g_font[0].info, (float)font_px(ui, size));
}

float ui_line_h(Ui *ui, float size)
{
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font[0].info, &asc, &desc, &gap);
    float sc = stbtt_ScaleForMappingEmToPixels(&g_font[0].info, (float)font_px(ui, size));
    return (asc - desc + gap) * sc;
}

bool ui_set_font(Ui *ui, const char *path)
{
    Font reg, bold;
    bool custom = path && *path;
    if (custom && font_from_file(&reg, path, 0)) {
        font_free(&g_font[0]);
        font_free(&g_font[1]);
        g_font[0] = reg;
        /* A custom font is used for both weights. */
        g_font[1] = reg;
        g_font[1].owned = false;
    } else {
        font_free(&g_font[0]);
        if (g_font[1].owned) font_free(&g_font[1]);
        font_from_memory(&reg, (unsigned char *)thc_font_regular, false, 0);
        font_from_memory(&bold, (unsigned char *)thc_font_bold, false, 0);
        g_font[0] = reg;
        g_font[1] = bold;
        if (custom) return false;
    }
    if (g_atlas) atlas_reset(ui);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Init and frame                                                             */
/* ------------------------------------------------------------------------ */

bool ui_init(Ui *ui, SDL_Window *win, SDL_Renderer *ren, const char *font_override)
{
    memset(ui, 0, sizeof *ui);
    ui->win = win;
    ui->ren = ren;
    ui->ui_scale = 1;
    ui->text_scale = 1;
    g_glyphs = SDL_calloc(GLYPH_SLOTS, sizeof(Glyph));
    g_scratch = SDL_malloc(ATLAS * ATLAS / 4 + ATLAS * ATLAS * 4 / 4 + 4096);
    g_atlas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, ATLAS, ATLAS);
    if (!g_glyphs || !g_scratch || !g_atlas) return false;
    SDL_SetTextureBlendMode(g_atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(g_atlas, SDL_SCALEMODE_LINEAR);
    ui_set_font(ui, font_override);
    ui->th = theme_make(PORTAL_VIDEO, true, false, 0);
    return true;
}

void ui_shutdown(Ui *ui)
{
    (void)ui;
    if (g_atlas) SDL_DestroyTexture(g_atlas);
    g_atlas = NULL;
    SDL_free(g_glyphs);
    SDL_free(g_scratch);
    font_free(&g_font[0]);
    if (g_font[1].owned) font_free(&g_font[1]);
    for (int i = 0; i < g_nfallback; i++) font_free(&g_fallback[i]);
    g_nfallback = 0;
    g_fallback_scanned = false;
}

Uint32 ui_id(const char *s)
{
    Uint32 h = 2166136261u;
    while (*s) { h ^= (Uint8)*s++; h *= 16777619u; }
    return h ? h : 1;
}

Uint32 ui_idi(const char *s, int i)
{
    Uint32 h = ui_id(s);
    h ^= (Uint32)i * 2654435761u;
    h *= 16777619u;
    return h ? h : 1;
}

Uint32 ui_idp(const char *s, const void *p)
{
    Uint64 v = (Uint64)(uintptr_t)p;
    return ui_idi(s, (int)(v ^ (v >> 32)));
}

bool ui_event(Ui *ui, SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_MOUSE_MOTION:
        SDL_ConvertEventToRenderCoordinates(ui->ren, ev);
        ui->mx = ev->motion.x;
        ui->my = ev->motion.y;
        ui->mouse_moved = true;
        ui->keyboard_mode = false;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        SDL_ConvertEventToRenderCoordinates(ui->ren, ev);
        ui->mx = ev->button.x;
        ui->my = ev->button.y;
        if (ev->button.button == SDL_BUTTON_LEFT) {
            ui->mouse_down = true;
            ui->mouse_pressed = true;
        } else if (ev->button.button == SDL_BUTTON_RIGHT) {
            ui->right_pressed = true;
        } else if (ev->button.button == SDL_BUTTON_X1) {
            ui->key_back = true;
        }
        ui->keyboard_mode = false;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        SDL_ConvertEventToRenderCoordinates(ui->ren, ev);
        ui->mx = ev->button.x;
        ui->my = ev->button.y;
        if (ev->button.button == SDL_BUTTON_LEFT) {
            ui->mouse_down = false;
            ui->mouse_released = true;
        }
        return true;
    case SDL_EVENT_MOUSE_WHEEL: {
        float x = ev->wheel.x, y = ev->wheel.y;
        if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { x = -x; y = -y; }
        if (SDL_GetModState() & SDL_KMOD_SHIFT) { x = y; y = 0; }
        ui->wheel_x += x;
        ui->wheel_y += y;
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {
        size_t len = strlen(ui->text_in);
        SDL_strlcpy(ui->text_in + len, ev->text.text, sizeof ui->text_in - len);
        return true;
    }
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode k = ev->key.key;
        bool shift = (ev->key.mod & SDL_KMOD_SHIFT) != 0;
        int nav = NAV_NONE;
        switch (k) {
        case SDLK_UP: nav = NAV_UP; break;
        case SDLK_DOWN: nav = NAV_DOWN; break;
        case SDLK_LEFT: nav = NAV_LEFT; break;
        case SDLK_RIGHT: nav = NAV_RIGHT; break;
        case SDLK_TAB: nav = shift ? NAV_PREV : NAV_NEXT; break;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            ui->key_activate = true;
            ui->text_enter = true;
            ui->keyboard_mode = true;
            return true;
        case SDLK_APPLICATION:
            ui->key_context = true;
            ui->keyboard_mode = true;
            return true;
        case SDLK_F10:
            if (shift) {
                ui->key_context = true;
                ui->keyboard_mode = true;
                return true;
            }
            break;
        case SDLK_BACKSPACE:
            ui->text_backspace++;
            return false; /* also "back" unless a text field takes it */
        default: break;
        }
        if (nav) {
            ui->nav = nav;
            ui->keyboard_mode = true;
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static bool rect_has(SDL_FRect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void resolve_nav(Ui *ui)
{
    int p = ui->cur ^ 1;
    int n = ui->nitems[p];
    UiItem *items = ui->items[p];
    if (!n || !ui->nav) return;
    int fi = -1;
    for (int i = 0; i < n; i++)
        if (items[i].id == ui->focus && items[i].layer >= ui->modal_layer) { fi = i; break; }
    if (fi < 0) {
        for (int i = 0; i < n; i++)
            if (items[i].layer >= ui->modal_layer) {
                ui->focus = items[i].id;
                ui->scroll_to_focus = true;
                break;
            }
        return;
    }
    if (ui->nav == NAV_NEXT || ui->nav == NAV_PREV) {
        int step = ui->nav == NAV_NEXT ? 1 : -1;
        for (int k = 1; k <= n; k++) {
            int j = ((fi + step * k) % n + n) % n;
            if (items[j].layer >= ui->modal_layer) {
                ui->focus = items[j].id;
                break;
            }
        }
        ui->scroll_to_focus = true;
        return;
    }
    SDL_FRect f = items[fi].r;
    float fcx = f.x + f.w / 2, fcy = f.y + f.h / 2;
    float best = 1e30f;
    int bi = -1;
    for (int i = 0; i < n; i++) {
        if (i == fi || items[i].layer < ui->modal_layer || items[i].id == items[fi].id) continue;
        SDL_FRect c = items[i].r;
        float ccx = c.x + c.w / 2, ccy = c.y + c.h / 2;
        float major, gap, minor;
        switch (ui->nav) {
        case NAV_UP:
            if (c.y + c.h > f.y + 1 && ccy >= fcy - 1) continue;
            major = fcy - ccy;
            gap = SDL_max(0, SDL_max(c.x, f.x) - SDL_min(c.x + c.w, f.x + f.w));
            minor = SDL_fabsf(ccx - fcx);
            break;
        case NAV_DOWN:
            if (c.y < f.y + f.h - 1 && ccy <= fcy + 1) continue;
            major = ccy - fcy;
            gap = SDL_max(0, SDL_max(c.x, f.x) - SDL_min(c.x + c.w, f.x + f.w));
            minor = SDL_fabsf(ccx - fcx);
            break;
        case NAV_LEFT:
            if (c.x + c.w > f.x + 1 && ccx >= fcx - 1) continue;
            major = fcx - ccx;
            gap = SDL_max(0, SDL_max(c.y, f.y) - SDL_min(c.y + c.h, f.y + f.h));
            minor = SDL_fabsf(ccy - fcy);
            break;
        default: /* NAV_RIGHT */
            if (c.x < f.x + f.w - 1 && ccx <= fcx + 1) continue;
            major = ccx - fcx;
            gap = SDL_max(0, SDL_max(c.y, f.y) - SDL_min(c.y + c.h, f.y + f.h));
            minor = SDL_fabsf(ccy - fcy);
            break;
        }
        if (major <= 0) continue;
        float score = major + gap * 6 + minor * 0.25f;
        if (score < best) { best = score; bi = i; }
    }
    if (bi >= 0) {
        ui->focus = items[bi].id;
        ui->scroll_to_focus = true;
    }
}

void ui_begin(Ui *ui)
{
    Uint64 now = SDL_GetTicksNS();
    double t = now / 1e9;
    ui->dt = ui->time > 0 ? (float)(t - ui->time) : 1.0f / 60;
    if (ui->dt > 0.1f) ui->dt = 0.1f;
    ui->time = t;
    ui->frame++;
    SDL_GetCurrentRenderOutputSize(ui->ren, &ui->w, &ui->h);
    float d = SDL_GetWindowDisplayScale(ui->win);
    ui->density = d > 0 ? d : 1;
    ui->s = ui->density * ui->ui_scale;
    ui->needs_redraw = false;

    if (ui->want_focus) {
        ui->focus = ui->want_focus;
        ui->want_focus = 0;
        ui->scroll_to_focus = true;
    }
    g_lr_pending = 0;
    if (ui->focus && ui->focus == g_capture_lr && (ui->nav == NAV_LEFT || ui->nav == NAV_RIGHT)) {
        g_lr_pending = ui->nav == NAV_LEFT ? -1 : 1;
        ui->nav = NAV_NONE;
    }
    resolve_nav(ui);
    ui->cur ^= 1;
    ui->nitems[ui->cur] = 0;
    ui->hover_next = 0;
    ui->hover_layer_next = -1;
    ui->layer = 0;
    ui->nclip = 0;
    ui->rdepth = 0;
    SDL_SetRenderClipRect(ui->ren, NULL);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
}

void ui_end(Ui *ui)
{
    ui->hover_id = ui->hover_next;
    if (ui->mouse_released) ui->pressed_id = 0;
    /* Keep a focus target that still exists; otherwise drop it. */
    bool found = false;
    for (int i = 0; i < ui->nitems[ui->cur]; i++)
        if (ui->items[ui->cur][i].id == ui->focus) { found = true; break; }
    if (!found && ui->focus && !ui->want_focus) ui->focus = 0;
    if (ui->mouse_moved || ui->wheel_x != 0 || ui->wheel_y != 0) ui->needs_redraw = true;
    ui->mouse_pressed = ui->mouse_released = ui->mouse_moved = false;
    ui->wheel_x = ui->wheel_y = 0;
    ui->nav = NAV_NONE;
    ui->key_activate = ui->key_back = ui->right_pressed = ui->key_context = false;
    ui->text_in[0] = 0;
    ui->text_backspace = 0;
    ui->text_enter = false;
    ui->scroll_to_focus = false;
    g_capture_lr = g_capture_lr_next;
    g_capture_lr_next = 0;
    ui->text_active = g_text_focus_next != 0;
    g_text_focus_next = 0;
    SDL_SetRenderClipRect(ui->ren, NULL);
}

/* ------------------------------------------------------------------------ */
/* Clipping and scrolling                                                     */
/* ------------------------------------------------------------------------ */

static SDL_FRect cur_clip(const Ui *ui)
{
    if (!ui->nclip) {
        SDL_FRect r = { 0, 0, (float)ui->w, (float)ui->h };
        return r;
    }
    SDL_Rect c = ui->clip[ui->nclip - 1];
    SDL_FRect r = { (float)c.x, (float)c.y, (float)c.w, (float)c.h };
    return r;
}

bool ui_visible(const Ui *ui, SDL_FRect r)
{
    SDL_FRect c = cur_clip(ui);
    return r.x < c.x + c.w && r.y < c.y + c.h && r.x + r.w > c.x && r.y + r.h > c.y;
}

void ui_clip_push(Ui *ui, SDL_FRect r)
{
    SDL_FRect c = cur_clip(ui);
    float x0 = SDL_max(r.x, c.x), y0 = SDL_max(r.y, c.y);
    float x1 = SDL_min(r.x + r.w, c.x + c.w), y1 = SDL_min(r.y + r.h, c.y + c.h);
    SDL_Rect n = { (int)floorf(x0), (int)floorf(y0), (int)SDL_max(0, ceilf(x1) - floorf(x0)),
                   (int)SDL_max(0, ceilf(y1) - floorf(y0)) };
    if (ui->nclip < UI_MAX_CLIP) ui->clip[ui->nclip++] = n;
    SDL_SetRenderClipRect(ui->ren, &n);
}

void ui_clip_pop(Ui *ui)
{
    if (ui->nclip > 0) ui->nclip--;
    if (ui->nclip) SDL_SetRenderClipRect(ui->ren, &ui->clip[ui->nclip - 1]);
    else SDL_SetRenderClipRect(ui->ren, NULL);
}

static UiRegion *region_get(Ui *ui, Uint32 id)
{
    UiRegion *free_r = NULL, *oldest = &ui->regions[0];
    for (int i = 0; i < UI_MAX_REGIONS; i++) {
        UiRegion *r = &ui->regions[i];
        if (r->id == id) return r;
        if (!r->id && !free_r) free_r = r;
        if (r->last_frame < oldest->last_frame) oldest = r;
    }
    UiRegion *r = free_r ? free_r : oldest;
    memset(r, 0, sizeof *r);
    r->id = id;
    return r;
}

void ui_scroll_reset(Ui *ui, Uint32 id)
{
    UiRegion *r = region_get(ui, id);
    r->off = r->target = 0;
}

void ui_scroll_to(Ui *ui, Uint32 id, float pixels)
{
    UiRegion *r = region_get(ui, id);
    r->target = pixels < 0 ? 0 : pixels;
}

float ui_anim(Ui *ui, float *v, float target, float speed)
{
    if (ui->reduced_motion) {
        *v = target;
        return target;
    }
    float d = target - *v;
    if (SDL_fabsf(d) < 0.5f) {
        *v = target;
    } else {
        *v += d * (1.0f - expf(-speed * ui->dt));
        ui->needs_redraw = true;
    }
    return *v;
}

#define ANIM_SLOTS 1024
static struct { Uint32 id; float v; } g_anim[ANIM_SLOTS];

float ui_anim_id(Ui *ui, Uint32 id, float target, float speed)
{
    Uint32 i = (id * 2654435761u) & (ANIM_SLOTS - 1);
    for (int n = 0; n < 8; n++, i = (i + 1) & (ANIM_SLOTS - 1)) {
        if (g_anim[i].id == id) return ui_anim(ui, &g_anim[i].v, target, speed);
        if (g_anim[i].id == 0) break;
    }
    /* new (or evict): start at the target, no animation on first sight */
    g_anim[i].id = id;
    g_anim[i].v = target;
    return target;
}

float ui_scroll_begin(Ui *ui, Uint32 id, SDL_FRect view, bool horizontal)
{
    UiRegion *r = region_get(ui, id);
    r->horizontal = horizontal;
    r->rect = view;
    r->view = horizontal ? view.w : view.h;
    r->last_frame = ui->frame;
    float max = SDL_max(0, r->content - r->view);
    if (r->target > max) r->target = max;
    if (r->target < 0) r->target = 0;
    ui_anim(ui, &r->off, r->target, 18);
    if (ui->rdepth < 8) ui->rstack[ui->rdepth++] = r;
    ui_clip_push(ui, view);
    return SDL_floorf(r->off);
}

void ui_scroll_end(Ui *ui, float content_len)
{
    if (!ui->rdepth) return;
    UiRegion *r = ui->rstack[--ui->rdepth];
    r->content = content_len;
    ui_clip_pop(ui);
    SDL_FRect c = cur_clip(ui);
    bool over = rect_has(r->rect, ui->mx, ui->my) && rect_has(c, ui->mx, ui->my);
    float step = dp(ui, 90);
    if (over) {
        if (r->horizontal && ui->wheel_x != 0) {
            r->target -= ui->wheel_x * step;
            ui->wheel_x = 0;
        } else if (!r->horizontal && ui->wheel_y != 0) {
            r->target -= ui->wheel_y * step;
            ui->wheel_y = 0;
        }
    }
    float max = SDL_max(0, r->content - r->view);
    if (r->target > max) r->target = max;
    if (r->target < 0) r->target = 0;
}

/* ------------------------------------------------------------------------ */
/* Items and focus                                                            */
/* ------------------------------------------------------------------------ */

UiItemState ui_item(Ui *ui, Uint32 id, SDL_FRect r)
{
    UiItemState st = { 0 };
    SDL_FRect clip = cur_clip(ui);
    bool modal_ok = ui->layer >= ui->modal_layer;
    if (ui->nitems[ui->cur] < UI_MAX_ITEMS && modal_ok) {
        /* Register the visible part, so navigation works with what is on screen,
         * but keep off-screen items so focus can move into scrolled content. */
        UiItem *it = &ui->items[ui->cur][ui->nitems[ui->cur]++];
        it->id = id;
        it->r = r;
        it->layer = ui->layer;
    }
    if (!modal_ok) return st;

    bool inside = rect_has(r, ui->mx, ui->my) && rect_has(clip, ui->mx, ui->my);
    if (inside && ui->layer >= ui->hover_layer_next) {
        ui->hover_next = id;
        ui->hover_layer_next = ui->layer;
    }
    st.hover = ui->hover_id == id && inside && !ui->keyboard_mode;
    if (st.hover && ui->mouse_pressed) {
        ui->pressed_id = id;
        ui->focus = id;
    }
    st.pressed = ui->pressed_id == id && ui->mouse_down;
    if (ui->pressed_id == id && ui->mouse_released && inside) st.activated = true;
    st.focused = ui->focus == id;
    if (ui->right_pressed && st.hover) st.context = true;
    if (st.focused && ui->key_context) {
        st.context = true;
        ui->key_context = false;
    }
    if (st.focused && ui->key_activate) {
        st.activated = true;
        ui->key_activate = false;
    }

    if (st.focused && ui->scroll_to_focus) {
        for (int i = 0; i < ui->rdepth; i++) {
            UiRegion *g = ui->rstack[i];
            /* vertical pages keep room for the labels under a focused card */
            float margin = dp(ui, g->horizontal ? 24 : 72);
            /* position in content coordinates (independent of animation) */
            float pos = (g->horizontal ? r.x - g->rect.x : r.y - g->rect.y) + SDL_floorf(g->off);
            float len = g->horizontal ? r.w : r.h;
            if (pos - g->target < margin) g->target = pos - margin;
            else if (pos + len - g->target > g->view - margin) g->target = pos + len + margin - g->view;
            if (g->target < 0) g->target = 0;
        }
    }
    return st;
}

void ui_focus(Ui *ui, Uint32 id)
{
    ui->want_focus = id;
}

bool ui_focus_exists(const Ui *ui, Uint32 id)
{
    int p = ui->cur;
    for (int i = 0; i < ui->nitems[p]; i++)
        if (ui->items[p][i].id == id) return true;
    return false;
}

void ui_focus_ring(Ui *ui, SDL_FRect r, float radius)
{
    if (!ui->keyboard_mode) return;
    float g = dp(ui, 3), w = dp(ui, ui->th.high_contrast ? 4 : 3);
    SDL_FRect o = { r.x - g - w, r.y - g - w, r.w + 2 * (g + w), r.h + 2 * (g + w) };
    ui_stroke(ui, o, ui->th.focus, radius + g + w, w);
}

/* ------------------------------------------------------------------------ */
/* Geometry                                                                   */
/* ------------------------------------------------------------------------ */

#define MAXV 1024
static SDL_Vertex g_v[MAXV * 3];
static int g_idx[MAXV * 9];

static SDL_FColor fcol(Color c)
{
    SDL_FColor f = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    return f;
}

/* Rounded-rect outline points, clockwise in screen space. */
static int rrect_path(SDL_FRect r, float rad, float inset, SDL_FPoint *pts, int max, SDL_FPoint *centers)
{
    float m = SDL_min(r.w, r.h) / 2;
    if (rad > m) rad = m;
    if (rad < 0) rad = 0;
    int seg = rad < 2 ? 1 : (int)SDL_clamp(rad / 2.5f, 3, 14);
    float cx[4] = { r.x + r.w - rad, r.x + r.w - rad, r.x + rad, r.x + rad };
    float cy[4] = { r.y + rad, r.y + r.h - rad, r.y + r.h - rad, r.y + rad };
    float a0[4] = { -SDL_PI_F / 2, 0, SDL_PI_F / 2, SDL_PI_F };
    float pr = SDL_max(0, rad - inset);
    int n = 0;
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i <= seg && n < max; i++) {
            float a = a0[c] + (SDL_PI_F / 2) * i / seg;
            float ca = cosf(a), sa = sinf(a);
            float rr = rad < 0.01f ? 0 : pr;
            pts[n].x = cx[c] + ca * rr;
            pts[n].y = cy[c] + sa * rr;
            if (rad < 0.01f) {
                /* square corner: push the point out along the diagonal */
                float dx = (c == 0 || c == 1) ? -inset : inset;
                float dy = (c == 1 || c == 2) ? -inset : inset;
                pts[n].x += dx;
                pts[n].y += dy;
            }
            if (centers) centers[n].x = ca, centers[n].y = sa;
            n++;
        }
    }
    return n;
}

typedef struct Grad {
    Color c0, c1;
    int mode; /* 0 flat, 1 vertical, 2 horizontal */
    SDL_FRect r;
} Grad;

static Color grad_at(const Grad *g, float x, float y)
{
    if (g->mode == 0) return g->c0;
    float t = g->mode == 1 ? (y - g->r.y) / (g->r.h > 0 ? g->r.h : 1)
                           : (x - g->r.x) / (g->r.w > 0 ? g->r.w : 1);
    return color_mix(g->c0, g->c1, t);
}

/* Convex polygon with an anti-aliased (or soft, for shadows) fringe. */
static void poly_fill(Ui *ui, const SDL_FPoint *p, int n, const Grad *g, float fringe)
{
    if (n < 3 || n > MAXV) return;
    float cx = 0, cy = 0;
    for (int i = 0; i < n; i++) { cx += p[i].x; cy += p[i].y; }
    cx /= n;
    cy /= n;
    int nv = 0, ni = 0;
    SDL_Vertex *v = g_v;
    v[nv].position.x = cx;
    v[nv].position.y = cy;
    v[nv].color = fcol(grad_at(g, cx, cy));
    v[nv].tex_coord.x = v[nv].tex_coord.y = 0;
    nv++;
    for (int i = 0; i < n; i++) {
        /* vertex normal from the two adjacent edges */
        SDL_FPoint a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
        float e1x = b.x - a.x, e1y = b.y - a.y, e2x = c.x - b.x, e2y = c.y - b.y;
        float l1 = SDL_sqrtf(e1x * e1x + e1y * e1y), l2 = SDL_sqrtf(e2x * e2x + e2y * e2y);
        float n1x = 0, n1y = 0, n2x = 0, n2y = 0;
        if (l1 > 1e-4f) { n1x = e1y / l1; n1y = -e1x / l1; }
        if (l2 > 1e-4f) { n2x = e2y / l2; n2y = -e2x / l2; }
        float nx = n1x + n2x, ny = n1y + n2y;
        float ln = SDL_sqrtf(nx * nx + ny * ny);
        if (ln < 1e-4f) { nx = b.x - cx; ny = b.y - cy; ln = SDL_sqrtf(nx * nx + ny * ny) + 1e-4f; }
        nx /= ln;
        ny /= ln;
        if (nx * (b.x - cx) + ny * (b.y - cy) < 0) { nx = -nx; ny = -ny; }
        float dotn = nx * (l1 > 1e-4f ? n1x : n2x) + ny * (l1 > 1e-4f ? n1y : n2y);
        if (dotn < 0) dotn = -dotn;
        float miter = dotn > 0.35f ? 1.0f / dotn : 2.85f;
        Color col = grad_at(g, b.x, b.y);
        v[nv].position = b;
        v[nv].color = fcol(col);
        v[nv].tex_coord.x = v[nv].tex_coord.y = 0;
        nv++;
        v[nv].position.x = b.x + nx * fringe * miter;
        v[nv].position.y = b.y + ny * fringe * miter;
        v[nv].color = fcol(with_alpha(col, 0));
        v[nv].tex_coord.x = v[nv].tex_coord.y = 0;
        nv++;
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int pi = 1 + i * 2, pj = 1 + j * 2;
        g_idx[ni++] = 0; g_idx[ni++] = pi; g_idx[ni++] = pj;
        if (fringe > 0) {
            g_idx[ni++] = pi; g_idx[ni++] = pi + 1; g_idx[ni++] = pj + 1;
            g_idx[ni++] = pi; g_idx[ni++] = pj + 1; g_idx[ni++] = pj;
        }
    }
    SDL_RenderGeometry(ui->ren, NULL, v, nv, g_idx, ni);
}

static void rrect_draw(Ui *ui, SDL_FRect r, float rad, const Grad *g, float fringe)
{
    if (r.w <= 0 || r.h <= 0) return;
    if (!ui_visible(ui, (SDL_FRect){ r.x - fringe, r.y - fringe, r.w + 2 * fringe, r.h + 2 * fringe }))
        return;
    SDL_FPoint pts[64];
    /* The fringe straddles the edge: shrink by half a pixel so AA is centered. */
    SDL_FRect in = r;
    if (fringe <= 1.01f) { in.x += 0.5f; in.y += 0.5f; in.w -= 1; in.h -= 1; }
    int n = rrect_path(in, rad, 0, pts, 64, NULL);
    poly_fill(ui, pts, n, g, fringe);
}

void ui_fill(Ui *ui, SDL_FRect r, Color c, float radius)
{
    Grad g = { c, c, 0, r };
    rrect_draw(ui, r, radius, &g, 1.0f);
}

void ui_fill_grad(Ui *ui, SDL_FRect r, Color top, Color bottom, float radius)
{
    Grad g = { top, bottom, 1, r };
    rrect_draw(ui, r, radius, &g, 1.0f);
}

void ui_fill_grad_h(Ui *ui, SDL_FRect r, Color left, Color right, float radius)
{
    Grad g = { left, right, 2, r };
    rrect_draw(ui, r, radius, &g, 1.0f);
}

void ui_shadow(Ui *ui, SDL_FRect r, float radius, float blur, Color c)
{
    if (c.a == 0) return;
    SDL_FRect s = { r.x + blur * 0.25f, r.y + blur * 0.45f, r.w - blur * 0.5f, r.h - blur * 0.4f };
    Grad g = { c, c, 0, s };
    rrect_draw(ui, s, radius, &g, blur);
}

void ui_circle(Ui *ui, float cx, float cy, float rad, Color c)
{
    SDL_FRect r = { cx - rad, cy - rad, rad * 2, rad * 2 };
    ui_fill(ui, r, c, rad);
}

void ui_stroke(Ui *ui, SDL_FRect r, Color c, float rad, float w)
{
    if (r.w <= 0 || r.h <= 0 || w <= 0) return;
    SDL_FPoint outer[64], inner[64], no[64];
    float m = SDL_min(r.w, r.h) / 2;
    if (rad > m) rad = m;
    if (rad < w) rad = w;
    int n = rrect_path(r, rad, 0, outer, 64, no);
    rrect_path(r, rad, w, inner, 64, NULL);
    SDL_Vertex *v = g_v;
    int nv = 0, ni = 0;
    SDL_FColor fc = fcol(c), f0 = fcol(with_alpha(c, 0));
    for (int i = 0; i < n; i++) {
        SDL_FPoint o = outer[i], in = inner[i];
        SDL_FPoint nn = no[i];
        /* 4 vertices: outer fringe, outer, inner, inner fringe */
        SDL_FPoint ps[4] = {
            { o.x + nn.x * 0.5f, o.y + nn.y * 0.5f }, { o.x - nn.x * 0.5f, o.y - nn.y * 0.5f },
            { in.x + nn.x * 0.5f, in.y + nn.y * 0.5f }, { in.x - nn.x * 0.5f, in.y - nn.y * 0.5f },
        };
        for (int k = 0; k < 4; k++) {
            v[nv].position = ps[k];
            v[nv].color = (k == 0 || k == 3) ? f0 : fc;
            v[nv].tex_coord.x = v[nv].tex_coord.y = 0;
            nv++;
        }
    }
    for (int i = 0; i < n; i++) {
        int a = i * 4, b = ((i + 1) % n) * 4;
        for (int k = 0; k < 3; k++) {
            g_idx[ni++] = a + k; g_idx[ni++] = a + k + 1; g_idx[ni++] = b + k + 1;
            g_idx[ni++] = a + k; g_idx[ni++] = b + k + 1; g_idx[ni++] = b + k;
        }
    }
    SDL_RenderGeometry(ui->ren, NULL, v, nv, g_idx, ni);
}

void ui_line(Ui *ui, float x0, float y0, float x1, float y1, float w, Color c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float l = SDL_sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float ux = dx / l, uy = dy / l, nx = -uy * w / 2, ny = ux * w / 2;
    SDL_FPoint p[4] = {
        { x0 + nx, y0 + ny }, { x1 + nx, y1 + ny }, { x1 - nx, y1 - ny }, { x0 - nx, y0 - ny },
    };
    Grad g = { c, c, 0, { 0, 0, 0, 0 } };
    poly_fill(ui, p, 4, &g, 1.0f);
}

void ui_tri(Ui *ui, float x0, float y0, float x1, float y1, float x2, float y2, Color c)
{
    SDL_FPoint p[3] = { { x0, y0 }, { x1, y1 }, { x2, y2 } };
    Grad g = { c, c, 0, { 0, 0, 0, 0 } };
    poly_fill(ui, p, 3, &g, 1.0f);
}

static void polyline(Ui *ui, const float *pts, int n, float w, Color c)
{
    for (int i = 0; i + 1 < n; i++)
        ui_line(ui, pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3], w, c);
    for (int i = 0; i < n; i++) ui_circle(ui, pts[i * 2], pts[i * 2 + 1], w / 2, c);
}

void ui_image(Ui *ui, SDL_Texture *t, SDL_FRect r, float radius, Uint8 alpha)
{
    if (!t || !ui_visible(ui, r)) return;
    float tw, th;
    SDL_GetTextureSize(t, &tw, &th);
    /* cover-fit: crop the texture to the box's aspect ratio */
    float sa = tw / th, da = r.w / r.h;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (sa > da) { float k = da / sa; u0 = (1 - k) / 2; u1 = u0 + k; }
    else { float k = sa / da; v0 = (1 - k) / 2; v1 = v0 + k; }
    SDL_FPoint pts[64];
    int n = rrect_path(r, radius, 0, pts, 64, NULL);
    SDL_Vertex *v = g_v;
    SDL_FColor col = { 1, 1, 1, alpha / 255.0f };
    for (int i = 0; i < n; i++) {
        v[i].position = pts[i];
        v[i].color = col;
        v[i].tex_coord.x = u0 + (u1 - u0) * (pts[i].x - r.x) / r.w;
        v[i].tex_coord.y = v0 + (v1 - v0) * (pts[i].y - r.y) / r.h;
    }
    v[n].position.x = r.x + r.w / 2;
    v[n].position.y = r.y + r.h / 2;
    v[n].color = col;
    v[n].tex_coord.x = (u0 + u1) / 2;
    v[n].tex_coord.y = (v0 + v1) / 2;
    int ni = 0;
    for (int i = 0; i < n; i++) {
        g_idx[ni++] = n;
        g_idx[ni++] = i;
        g_idx[ni++] = (i + 1) % n;
    }
    SDL_RenderGeometry(ui->ren, t, v, n + 1, g_idx, ni);
}

/* ------------------------------------------------------------------------ */
/* Text                                                                       */
/* ------------------------------------------------------------------------ */

#define TEXT_BATCH 512
static SDL_Vertex g_tv[TEXT_BATCH * 4];
static int g_ti[TEXT_BATCH * 6];

static float text_run(Ui *ui, int font, float size, float x, float y, Color c, const char *s,
                      size_t len, bool draw)
{
    int px = font_px(ui, size);
    float base = y + ui_ascent(ui, size);
    float pen = x;
    int n = 0;
    SDL_FColor fc = fcol(c);
    const char *p = s;
    size_t left = len;
    int prev_gi = 0, prev_slot = -1;
    float inv = 1.0f / ATLAS;
    while (left > 0) {
        Uint32 cp = SDL_StepUTF8(&p, &left);
        if (cp == 0) break;
        if (cp == '\n' || cp == '\r') continue;
        int slot;
        Glyph *g = get_glyph(ui, font, px, cp, &slot);
        Font *f = font_for(font, slot);
        if (slot == prev_slot && prev_gi) {
            int gi = stbtt_FindGlyphIndex(&f->info, (int)cp);
            pen += stbtt_GetGlyphKernAdvance(&f->info, prev_gi, gi) *
                   stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
            prev_gi = gi;
        } else {
            prev_gi = stbtt_FindGlyphIndex(&f->info, (int)cp);
        }
        prev_slot = slot;
        if (draw && g->w > 0) {
            float gx = SDL_floorf(pen + 0.5f) + g->xoff, gy = SDL_floorf(base + 0.5f) + g->yoff;
            SDL_Vertex *v = &g_tv[n * 4];
            float u0 = g->x * inv, v0 = g->y * inv, u1 = (g->x + g->w) * inv, v1 = (g->y + g->h) * inv;
            v[0].position.x = gx;         v[0].position.y = gy;         v[0].tex_coord.x = u0; v[0].tex_coord.y = v0;
            v[1].position.x = gx + g->w;  v[1].position.y = gy;         v[1].tex_coord.x = u1; v[1].tex_coord.y = v0;
            v[2].position.x = gx + g->w;  v[2].position.y = gy + g->h;  v[2].tex_coord.x = u1; v[2].tex_coord.y = v1;
            v[3].position.x = gx;         v[3].position.y = gy + g->h;  v[3].tex_coord.x = u0; v[3].tex_coord.y = v1;
            for (int k = 0; k < 4; k++) v[k].color = fc;
            int *ix = &g_ti[n * 6];
            ix[0] = n * 4; ix[1] = n * 4 + 1; ix[2] = n * 4 + 2;
            ix[3] = n * 4; ix[4] = n * 4 + 2; ix[5] = n * 4 + 3;
            if (++n == TEXT_BATCH) {
                SDL_RenderGeometry(ui->ren, g_atlas, g_tv, n * 4, g_ti, n * 6);
                n = 0;
            }
        }
        pen += g->adv;
    }
    if (draw && n) SDL_RenderGeometry(ui->ren, g_atlas, g_tv, n * 4, g_ti, n * 6);
    return pen - x;
}

float ui_text(Ui *ui, int font, float size, float x, float y, Color c, const char *s)
{
    if (!s) return 0;
    return text_run(ui, font, size, x, y, c, s, strlen(s), true);
}

float ui_text_w(Ui *ui, int font, float size, const char *s)
{
    if (!s) return 0;
    return text_run(ui, font, size, 0, 0, (Color){ 0 }, s, strlen(s), false);
}

float ui_text_fit(Ui *ui, int font, float size, float x, float y, float max_w, Color c, const char *s)
{
    if (!s) return 0;
    size_t len = strlen(s);
    float w = text_run(ui, font, size, x, y, c, s, len, false);
    if (w <= max_w) return text_run(ui, font, size, x, y, c, s, len, true);
    float ell = ui_text_w(ui, font, size, "\xE2\x80\xA6");
    /* binary search on byte length, snapping to UTF-8 boundaries */
    size_t lo = 0, hi = len;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        while (mid > 0 && (s[mid] & 0xC0) == 0x80) mid--;
        if (mid <= lo) break;
        if (text_run(ui, font, size, 0, 0, c, s, mid, false) + ell <= max_w) lo = mid;
        else hi = mid - 1;
    }
    while (lo > 0 && (s[lo] & 0xC0) == 0x80) lo--;
    while (lo > 0 && s[lo - 1] == ' ') lo--;
    float dw = text_run(ui, font, size, x, y, c, s, lo, true);
    dw += ui_text(ui, font, size, x + dw, y, c, "\xE2\x80\xA6");
    return dw;
}

void ui_text_center(Ui *ui, int font, float size, SDL_FRect r, Color c, const char *s, bool hcenter)
{
    float lh = ui_line_h(ui, size);
    float y = r.y + (r.h - lh) / 2;
    float x = r.x;
    if (hcenter) {
        float w = ui_text_w(ui, font, size, s);
        if (w > r.w) w = r.w;
        x = r.x + (r.w - w) / 2;
    }
    ui_text_fit(ui, font, size, x, y, r.w, c, s);
}

float ui_text_wrap(Ui *ui, int font, float size, float x, float y, float max_w, Color c,
                   const char *s, int max_lines)
{
    float lh = ui_line_h(ui, size);
    int lines = 0;
    const char *p = s;
    while (*p && (max_lines <= 0 || lines < max_lines)) {
        /* find the longest run of words that fits */
        const char *end = p, *best = NULL;
        while (1) {
            const char *q = end;
            while (*q == ' ') q++;
            while (*q && *q != ' ' && *q != '\n') q++;
            if (text_run(ui, font, size, 0, 0, c, p, (size_t)(q - p), false) > max_w && best) break;
            best = q;
            end = q;
            if (!*q || *q == '\n') break;
        }
        bool last = max_lines > 0 && lines == max_lines - 1 && *best;
        if (last) {
            ui_text_fit(ui, font, size, x, y + lines * lh, max_w, c, p);
        } else {
            char tmp[1024];
            size_t n = SDL_min((size_t)(best - p), sizeof tmp - 1);
            memcpy(tmp, p, n);
            tmp[n] = 0;
            ui_text_fit(ui, font, size, x, y + lines * lh, max_w, c, tmp);
        }
        lines++;
        p = best;
        while (*p == ' ' || *p == '\n') p++;
        if (last) break;
    }
    return lines * lh;
}

/* ------------------------------------------------------------------------ */
/* Icons                                                                      */
/* ------------------------------------------------------------------------ */

void ui_icon(Ui *ui, UiIcon icon, SDL_FRect b, Color c)
{
    float s = SDL_min(b.w, b.h);
    float x = b.x + (b.w - s) / 2, y = b.y + (b.h - s) / 2;
    float w = SDL_max(1.5f, s * 0.09f);
#define X(u) (x + (u) * s)
#define Y(v) (y + (v) * s)
    switch (icon) {
    case ICON_NONE:
        break;
    case ICON_PLAY:
        ui_tri(ui, X(0.30f), Y(0.18f), X(0.82f), Y(0.50f), X(0.30f), Y(0.82f), c);
        break;
    case ICON_PAUSE:
        ui_fill(ui, (SDL_FRect){ X(0.24f), Y(0.18f), s * 0.17f, s * 0.64f }, c, s * 0.04f);
        ui_fill(ui, (SDL_FRect){ X(0.59f), Y(0.18f), s * 0.17f, s * 0.64f }, c, s * 0.04f);
        break;
    case ICON_NEXT:
        ui_tri(ui, X(0.20f), Y(0.20f), X(0.64f), Y(0.50f), X(0.20f), Y(0.80f), c);
        ui_fill(ui, (SDL_FRect){ X(0.66f), Y(0.20f), s * 0.13f, s * 0.60f }, c, s * 0.03f);
        break;
    case ICON_PREV:
        ui_tri(ui, X(0.80f), Y(0.20f), X(0.36f), Y(0.50f), X(0.80f), Y(0.80f), c);
        ui_fill(ui, (SDL_FRect){ X(0.21f), Y(0.20f), s * 0.13f, s * 0.60f }, c, s * 0.03f);
        break;
    case ICON_REWIND:
        ui_tri(ui, X(0.50f), Y(0.24f), X(0.14f), Y(0.50f), X(0.50f), Y(0.76f), c);
        ui_tri(ui, X(0.86f), Y(0.24f), X(0.50f), Y(0.50f), X(0.86f), Y(0.76f), c);
        break;
    case ICON_FORWARD:
        ui_tri(ui, X(0.14f), Y(0.24f), X(0.50f), Y(0.50f), X(0.14f), Y(0.76f), c);
        ui_tri(ui, X(0.50f), Y(0.24f), X(0.86f), Y(0.50f), X(0.50f), Y(0.76f), c);
        break;
    case ICON_CHECK: {
        float p[] = { X(0.22f), Y(0.52f), X(0.42f), Y(0.71f), X(0.79f), Y(0.31f) };
        polyline(ui, p, 3, s * 0.13f, c);
        break;
    }
    case ICON_GEAR: {
        float cx = X(0.5f), cy = Y(0.5f);
        for (int i = 0; i < 8; i++) {
            float a = i * SDL_PI_F / 4;
            ui_line(ui, cx + cosf(a) * s * 0.26f, cy + sinf(a) * s * 0.26f,
                    cx + cosf(a) * s * 0.43f, cy + sinf(a) * s * 0.43f, s * 0.15f, c);
        }
        ui_stroke(ui, (SDL_FRect){ X(0.22f), Y(0.22f), s * 0.56f, s * 0.56f }, c, s * 0.28f, s * 0.13f);
        break;
    }
    case ICON_FILM:
        ui_stroke(ui, (SDL_FRect){ X(0.14f), Y(0.18f), s * 0.72f, s * 0.64f }, c, s * 0.08f, w);
        ui_line(ui, X(0.32f), Y(0.18f), X(0.32f), Y(0.82f), w, c);
        ui_line(ui, X(0.68f), Y(0.18f), X(0.68f), Y(0.82f), w, c);
        ui_line(ui, X(0.14f), Y(0.50f), X(0.32f), Y(0.50f), w, c);
        ui_line(ui, X(0.68f), Y(0.50f), X(0.86f), Y(0.50f), w, c);
        break;
    case ICON_TV:
        ui_stroke(ui, (SDL_FRect){ X(0.12f), Y(0.28f), s * 0.76f, s * 0.54f }, c, s * 0.08f, w);
        ui_line(ui, X(0.36f), Y(0.10f), X(0.50f), Y(0.27f), w, c);
        ui_line(ui, X(0.64f), Y(0.10f), X(0.50f), Y(0.27f), w, c);
        break;
    case ICON_NOTE:
        ui_circle(ui, X(0.36f), Y(0.70f), s * 0.14f, c);
        ui_line(ui, X(0.48f), Y(0.70f), X(0.48f), Y(0.16f), w, c);
        ui_line(ui, X(0.48f), Y(0.18f), X(0.78f), Y(0.30f), w * 1.3f, c);
        break;
    case ICON_BOOK: {
        float l[] = { X(0.50f), Y(0.28f), X(0.30f), Y(0.20f), X(0.12f), Y(0.22f), X(0.12f), Y(0.76f),
                      X(0.30f), Y(0.74f), X(0.50f), Y(0.82f), X(0.70f), Y(0.74f), X(0.88f), Y(0.76f),
                      X(0.88f), Y(0.22f), X(0.70f), Y(0.20f), X(0.50f), Y(0.28f) };
        polyline(ui, l, 11, w, c);
        ui_line(ui, X(0.50f), Y(0.28f), X(0.50f), Y(0.82f), w, c);
        break;
    }
    case ICON_FOLDER:
        ui_stroke(ui, (SDL_FRect){ X(0.12f), Y(0.30f), s * 0.76f, s * 0.50f }, c, s * 0.07f, w);
        {
            float t[] = { X(0.14f), Y(0.30f), X(0.18f), Y(0.20f), X(0.40f), Y(0.20f), X(0.46f), Y(0.30f) };
            polyline(ui, t, 4, w, c);
        }
        break;
    case ICON_BEER: {
        /* a beer bottle with a label that reads XXX (three small crosses) */
        float body[] = { X(0.42f), Y(0.08f), X(0.42f), Y(0.30f), X(0.30f), Y(0.42f), X(0.30f), Y(0.90f), X(0.70f), Y(0.90f),
                         X(0.70f), Y(0.42f), X(0.58f), Y(0.30f), X(0.58f), Y(0.08f), X(0.42f), Y(0.08f) };
        polyline(ui, body, 9, w, c);
        ui_stroke(ui, (SDL_FRect){ X(0.30f), Y(0.52f), s * 0.40f, s * 0.26f }, c, s * 0.02f, w);
        for (int i = 0; i < 3; i++) {
            float cx = X(0.375f + 0.125f * (float)i), cy = Y(0.65f), d = s * 0.035f;
            float a1[] = { cx - d, cy - d, cx + d, cy + d };
            float a2[] = { cx - d, cy + d, cx + d, cy - d };
            polyline(ui, a1, 2, w * 0.7f, c);
            polyline(ui, a2, 2, w * 0.7f, c);
        }
        break;
    }
    case ICON_CAMERA:
        ui_stroke(ui, (SDL_FRect){ X(0.10f), Y(0.28f), s * 0.80f, s * 0.54f }, c, s * 0.10f, w);
        {
            float t[] = { X(0.30f), Y(0.28f), X(0.36f), Y(0.18f), X(0.64f), Y(0.18f), X(0.70f), Y(0.28f) };
            polyline(ui, t, 4, w, c);
        }
        ui_stroke(ui, (SDL_FRect){ X(0.36f), Y(0.36f), s * 0.28f, s * 0.28f }, c, s * 0.14f, w);
        break;
    case ICON_BACK: {
        float p[] = { X(0.62f), Y(0.20f), X(0.32f), Y(0.50f), X(0.62f), Y(0.80f) };
        polyline(ui, p, 3, s * 0.11f, c);
        break;
    }
    case ICON_CHEVRON_RIGHT: {
        float p[] = { X(0.38f), Y(0.20f), X(0.68f), Y(0.50f), X(0.38f), Y(0.80f) };
        polyline(ui, p, 3, s * 0.11f, c);
        break;
    }
    case ICON_CLOCK:
        ui_stroke(ui, (SDL_FRect){ X(0.14f), Y(0.14f), s * 0.72f, s * 0.72f }, c, s * 0.36f, w);
        {
            float p[] = { X(0.50f), Y(0.30f), X(0.50f), Y(0.50f), X(0.64f), Y(0.60f) };
            polyline(ui, p, 3, w, c);
        }
        break;
    case ICON_VOLUME: {
        SDL_FPoint sp[5] = { { X(0.14f), Y(0.38f) }, { X(0.30f), Y(0.38f) }, { X(0.50f), Y(0.20f) },
                             { X(0.50f), Y(0.80f) }, { X(0.30f), Y(0.62f) } };
        SDL_FPoint q[6] = { sp[0], sp[1], sp[2], sp[3], sp[4], { X(0.14f), Y(0.62f) } };
        Grad g = { c, c, 0, { 0, 0, 0, 0 } };
        poly_fill(ui, q, 6, &g, 1.0f);
        float a1[8], a2[10];
        for (int i = 0; i < 4; i++) {
            float a = -0.7f + 1.4f * i / 3;
            a1[i * 2] = X(0.50f) + cosf(a) * s * 0.18f;
            a1[i * 2 + 1] = Y(0.50f) + sinf(a) * s * 0.18f;
        }
        for (int i = 0; i < 5; i++) {
            float a = -0.85f + 1.7f * i / 4;
            a2[i * 2] = X(0.50f) + cosf(a) * s * 0.34f;
            a2[i * 2 + 1] = Y(0.50f) + sinf(a) * s * 0.34f;
        }
        polyline(ui, a1, 4, w, c);
        polyline(ui, a2, 5, w, c);
        break;
    }
    case ICON_STAR:
    case ICON_SPARK: {
        int pts = icon == ICON_STAR ? 5 : 4;
        float ro = s * 0.42f, ri = icon == ICON_STAR ? s * 0.18f : s * 0.10f;
        float cx = X(0.5f), cy = Y(0.52f);
        for (int i = 0; i < pts; i++) {
            float a = -SDL_PI_F / 2 + i * 2 * SDL_PI_F / pts;
            float a1 = a - SDL_PI_F / pts, a2 = a + SDL_PI_F / pts;
            ui_tri(ui, cx + cosf(a) * ro, cy + sinf(a) * ro, cx + cosf(a2) * ri, cy + sinf(a2) * ri,
                   cx + cosf(a1) * ri, cy + sinf(a1) * ri, c);
        }
        ui_circle(ui, cx, cy, ri * 1.05f, c);
        break;
    }
    case ICON_SUN: {
        ui_circle(ui, X(0.5f), Y(0.5f), s * 0.17f, c);
        for (int i = 0; i < 8; i++) {
            float a = i * SDL_PI_F / 4;
            ui_line(ui, X(0.5f) + cosf(a) * s * 0.29f, Y(0.5f) + sinf(a) * s * 0.29f,
                    X(0.5f) + cosf(a) * s * 0.41f, Y(0.5f) + sinf(a) * s * 0.41f, w, c);
        }
        break;
    }
    case ICON_MOON: {
        /* crescent: a strip between two arcs */
        float pts_[2 * 13];
        for (int i = 0; i <= 12; i++) {
            float a = SDL_PI_F * 0.35f + i * (SDL_PI_F * 1.3f) / 12;
            pts_[i * 2] = X(0.5f) + cosf(a) * s * 0.34f;
            pts_[i * 2 + 1] = Y(0.5f) + sinf(a) * s * 0.34f;
        }
        polyline(ui, pts_, 13, s * 0.16f, c);
        break;
    }
    case ICON_KEYBOARD:
        ui_stroke(ui, (SDL_FRect){ X(0.08f), Y(0.24f), s * 0.84f, s * 0.52f }, c, s * 0.08f, w);
        for (int r = 0; r < 2; r++)
            for (int k = 0; k < 5; k++)
                ui_circle(ui, X(0.22f + k * 0.14f), Y(0.40f + r * 0.13f), s * 0.035f, c);
        ui_line(ui, X(0.30f), Y(0.66f), X(0.70f), Y(0.66f), w, c);
        break;
    case ICON_EYE: {
        float top[14], bot[14];
        for (int i = 0; i < 7; i++) {
            float t = i / 6.0f;
            top[i * 2] = bot[i * 2] = X(0.10f + 0.80f * t);
            float hgt = sinf(t * SDL_PI_F) * s * 0.26f;
            top[i * 2 + 1] = Y(0.5f) - hgt;
            bot[i * 2 + 1] = Y(0.5f) + hgt;
        }
        polyline(ui, top, 7, w, c);
        polyline(ui, bot, 7, w, c);
        ui_circle(ui, X(0.5f), Y(0.5f), s * 0.11f, c);
        break;
    }
    case ICON_SHIELD: {
        float p[] = { X(0.5f), Y(0.12f), X(0.82f), Y(0.24f), X(0.78f), Y(0.56f), X(0.5f), Y(0.88f),
                      X(0.22f), Y(0.56f), X(0.18f), Y(0.24f), X(0.5f), Y(0.12f) };
        polyline(ui, p, 7, w, c);
        break;
    }
    case ICON_INFO:
        ui_stroke(ui, (SDL_FRect){ X(0.12f), Y(0.12f), s * 0.76f, s * 0.76f }, c, s * 0.38f, w);
        ui_circle(ui, X(0.5f), Y(0.32f), s * 0.06f, c);
        ui_line(ui, X(0.5f), Y(0.45f), X(0.5f), Y(0.70f), w * 1.2f, c);
        break;
    case ICON_PLUS:
        ui_line(ui, X(0.5f), Y(0.18f), X(0.5f), Y(0.82f), w * 1.3f, c);
        ui_line(ui, X(0.18f), Y(0.5f), X(0.82f), Y(0.5f), w * 1.3f, c);
        break;
    case ICON_CLOSE:
        ui_line(ui, X(0.22f), Y(0.22f), X(0.78f), Y(0.78f), w * 1.3f, c);
        ui_line(ui, X(0.78f), Y(0.22f), X(0.22f), Y(0.78f), w * 1.3f, c);
        break;
    case ICON_QUEUE:
        ui_line(ui, X(0.14f), Y(0.26f), X(0.62f), Y(0.26f), w, c);
        ui_line(ui, X(0.14f), Y(0.48f), X(0.62f), Y(0.48f), w, c);
        ui_line(ui, X(0.14f), Y(0.70f), X(0.46f), Y(0.70f), w, c);
        ui_tri(ui, X(0.68f), Y(0.50f), X(0.68f), Y(0.86f), X(0.92f), Y(0.68f), c);
        break;
    case ICON_SHUFFLE: {
        float a[] = { X(0.12f), Y(0.30f), X(0.40f), Y(0.30f), X(0.60f), Y(0.70f), X(0.80f), Y(0.70f) };
        float b[] = { X(0.12f), Y(0.70f), X(0.40f), Y(0.70f), X(0.60f), Y(0.30f), X(0.80f), Y(0.30f) };
        polyline(ui, a, 4, w, c);
        polyline(ui, b, 4, w, c);
        ui_tri(ui, X(0.78f), Y(0.18f), X(0.78f), Y(0.42f), X(0.94f), Y(0.30f), c);
        ui_tri(ui, X(0.78f), Y(0.58f), X(0.78f), Y(0.82f), X(0.94f), Y(0.70f), c);
        break;
    }
    case ICON_REPEAT:
    case ICON_REPEAT_ONE: {
        float a[] = { X(0.80f), Y(0.40f), X(0.80f), Y(0.28f), X(0.20f), Y(0.28f), X(0.20f), Y(0.48f) };
        float b[] = { X(0.20f), Y(0.60f), X(0.20f), Y(0.72f), X(0.80f), Y(0.72f), X(0.80f), Y(0.52f) };
        polyline(ui, a, 4, w, c);
        polyline(ui, b, 4, w, c);
        ui_tri(ui, X(0.68f), Y(0.12f), X(0.68f), Y(0.44f), X(0.92f), Y(0.28f), c);
        ui_tri(ui, X(0.32f), Y(0.56f), X(0.32f), Y(0.88f), X(0.08f), Y(0.72f), c);
        if (icon == ICON_REPEAT_ONE) {
            ui_circle(ui, X(0.50f), Y(0.50f), s * 0.17f, c);
            ui_line(ui, X(0.50f), Y(0.42f), X(0.50f), Y(0.58f), w * 0.8f, rgba(0, 255));
        }
        break;
    }
    case ICON_UP: {
        float p[] = { X(0.24f), Y(0.62f), X(0.50f), Y(0.34f), X(0.76f), Y(0.62f) };
        polyline(ui, p, 3, s * 0.11f, c);
        break;
    }
    case ICON_DOWN: {
        float p[] = { X(0.24f), Y(0.38f), X(0.50f), Y(0.66f), X(0.76f), Y(0.38f) };
        polyline(ui, p, 3, s * 0.11f, c);
        break;
    }
    case ICON_GRIP:
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 2; k++) ui_circle(ui, X(0.38f + k * 0.24f), Y(0.26f + r * 0.24f), s * 0.05f, c);
        break;
    case ICON_MORE:
        for (int k = 0; k < 3; k++) ui_circle(ui, X(0.50f), Y(0.24f + k * 0.26f), s * 0.07f, c);
        break;
    case ICON_SORT:
        ui_line(ui, X(0.16f), Y(0.28f), X(0.84f), Y(0.28f), w, c);
        ui_line(ui, X(0.16f), Y(0.50f), X(0.64f), Y(0.50f), w, c);
        ui_line(ui, X(0.16f), Y(0.72f), X(0.44f), Y(0.72f), w, c);
        break;
    }
#undef X
#undef Y
}

/* ------------------------------------------------------------------------ */
/* Widgets                                                                    */
/* ------------------------------------------------------------------------ */


bool ui_button(Ui *ui, Uint32 id, SDL_FRect r, const char *label, UiIcon icon, bool primary)
{
    UiItemState st = ui_item(ui, id, r);
    const Theme *t = &ui->th;
    float rad = r.h / 2;
    float lift = st.pressed ? 0.97f : 1.0f;
    SDL_FRect d = { r.x + r.w * (1 - lift) / 2, r.y + r.h * (1 - lift) / 2, r.w * lift, r.h * lift };
    if (primary) {
        if (!t->high_contrast) ui_shadow(ui, d, rad, dp(ui, 14), with_alpha(t->accent, st.hover ? 110 : 60));
        Color a = st.hover ? color_mix(t->accent, rgb(0xFFFFFF), 0.12f) : t->accent;
        ui_fill_grad_h(ui, d, a, t->high_contrast ? a : color_mix(a, t->accent2, 0.35f), rad);
    } else {
        ui_fill(ui, d, st.hover ? t->surface_hi : t->surface, rad);
        ui_stroke(ui, d, t->high_contrast ? t->border : with_alpha(t->border, 255), rad, dp(ui, 1));
    }
    Color fg = primary ? t->on_accent : t->text;
    float isz = dp(ui, 18);
    float tw = label ? ui_text_w(ui, FONT_BOLD, TS_LABEL, label) : 0;
    bool has_icon = icon != ICON_NONE;
    float total = tw + (has_icon ? isz + (label ? dp(ui, 8) : 0) : 0);
    float x = d.x + (d.w - total) / 2;
    if (x < d.x + dp(ui, 12)) x = d.x + dp(ui, 12);
    if (has_icon) {
        ui_icon(ui, icon, (SDL_FRect){ x, d.y + (d.h - isz) / 2, isz, isz }, fg);
        x += isz + dp(ui, 8);
    }
    if (label) {
        float lh = ui_line_h(ui, TS_LABEL);
        ui_text_fit(ui, FONT_BOLD, TS_LABEL, x, d.y + (d.h - lh) / 2, d.x + d.w - x - dp(ui, 12), fg, label);
    }
    if (st.focused) ui_focus_ring(ui, r, rad);
    return st.activated;
}

void ui_tooltip(Ui *ui, SDL_FRect r, const char *text)
{
    const Theme *t = &ui->th;
    float tw = ui_text_w(ui, FONT_REGULAR, TS_SMALL, text) + dp(ui, 16);
    float th = ui_line_h(ui, TS_SMALL) + dp(ui, 8);
    float tx = SDL_clamp(r.x + r.w / 2 - tw / 2, dp(ui, 4), ui->w - tw - dp(ui, 4));
    SDL_FRect tr = { tx, r.y + r.h + dp(ui, 6), tw, th };
    if (tr.y + tr.h > ui->h) tr.y = r.y - th - dp(ui, 6);
    ui_fill(ui, tr, t->text, dp(ui, 6));
    ui_text(ui, FONT_REGULAR, TS_SMALL, tr.x + dp(ui, 8), tr.y + dp(ui, 4), t->bg, text);
}

bool ui_icon_button(Ui *ui, Uint32 id, SDL_FRect r, UiIcon icon, const char *tooltip)
{
    UiItemState st = ui_item(ui, id, r);
    const Theme *t = &ui->th;
    float rad = SDL_min(r.w, r.h) / 2;
    if (st.hover || st.focused) ui_fill(ui, r, t->surface_hi, rad);
    float pad = r.h * 0.24f;
    ui_icon(ui, icon, (SDL_FRect){ r.x + pad, r.y + pad, r.w - 2 * pad, r.h - 2 * pad }, t->text);
    if (st.focused) ui_focus_ring(ui, r, rad);
    if ((st.hover || st.focused) && tooltip) ui_tooltip(ui, r, tooltip);
    return st.activated;
}

bool ui_pill(Ui *ui, Uint32 id, SDL_FRect r, const char *label, bool selected)
{
    UiItemState st = ui_item(ui, id, r);
    const Theme *t = &ui->th;
    float rad = r.h / 2;
    if (selected) ui_fill(ui, r, t->text, rad);
    else {
        ui_fill(ui, r, st.hover ? t->surface_hi : with_alpha(t->surface, t->high_contrast ? 255 : 200), rad);
        if (t->high_contrast) ui_stroke(ui, r, t->border, rad, dp(ui, 1));
    }
    ui_text_center(ui, FONT_BOLD, TS_BODY, r, selected ? t->bg2 : t->text, label, true);
    if (st.focused) ui_focus_ring(ui, r, rad);
    return st.activated;
}

static void row_frame(Ui *ui, SDL_FRect r, UiItemState st)
{
    const Theme *t = &ui->th;
    ui_fill(ui, r, st.hover ? t->surface_hi : t->surface, dp(ui, 12));
    if (t->high_contrast) ui_stroke(ui, r, t->border, dp(ui, 12), dp(ui, 1));
    if (st.focused) ui_focus_ring(ui, r, dp(ui, 12));
}

static void row_label(Ui *ui, SDL_FRect r, const char *label, const char *hint, float right_reserve)
{
    const Theme *t = &ui->th;
    float pad = dp(ui, 16);
    float maxw = r.w - 2 * pad - right_reserve;
    if (hint && *hint) {
        float l1 = ui_line_h(ui, TS_LABEL), l2 = ui_line_h(ui, TS_SMALL);
        float y = r.y + (r.h - l1 - l2) / 2;
        ui_text_fit(ui, FONT_BOLD, TS_LABEL, r.x + pad, y, maxw, t->text, label);
        ui_text_fit(ui, FONT_REGULAR, TS_SMALL, r.x + pad, y + l1, maxw, t->text_dim, hint);
    } else {
        ui_text_center(ui, FONT_BOLD, TS_LABEL, (SDL_FRect){ r.x + pad, r.y, maxw, r.h }, t->text, label, false);
    }
}

bool ui_toggle(Ui *ui, Uint32 id, SDL_FRect r, const char *label, const char *hint, bool *value)
{
    UiItemState st = ui_item(ui, id, r);
    const Theme *t = &ui->th;
    row_frame(ui, r, st);
    float sw = dp(ui, 46), sh = dp(ui, 26);
    SDL_FRect s = { r.x + r.w - sw - dp(ui, 16), r.y + (r.h - sh) / 2, sw, sh };
    row_label(ui, r, label, hint, sw + dp(ui, 16));
    if (st.activated) *value = !*value;
    ui_fill(ui, s, *value ? t->accent : t->track, sh / 2);
    if (t->high_contrast) ui_stroke(ui, s, t->border, sh / 2, dp(ui, 1));
    float k = sh - dp(ui, 6);
    float kx = *value ? s.x + s.w - k - dp(ui, 3) : s.x + dp(ui, 3);
    ui_circle(ui, kx + k / 2, s.y + sh / 2, k / 2, *value ? t->on_accent : t->text);
    return st.activated;
}

bool ui_slider(Ui *ui, Uint32 id, SDL_FRect r, const char *label, float *value, float lo, float hi,
               float step, const char *fmt)
{
    UiItemState st = ui_item(ui, id, r);
    const Theme *t = &ui->th;
    row_frame(ui, r, st);
    bool changed = false;
    float tw = SDL_min(dp(ui, 240), r.w * 0.45f);
    SDL_FRect tr = { r.x + r.w - tw - dp(ui, 76), r.y + r.h / 2 - dp(ui, 3), tw, dp(ui, 6) };
    row_label(ui, r, label, NULL, tw + dp(ui, 92));
    if (st.focused) {
        g_capture_lr_next = id;
        if (g_lr_pending) {
            *value += step * g_lr_pending;
            g_lr_pending = 0;
            changed = true;
        }
    }
    if (st.pressed) {
        float f = (ui->mx - tr.x) / tr.w;
        f = SDL_clamp(f, 0, 1);
        float v = lo + f * (hi - lo);
        v = lo + SDL_roundf((v - lo) / step) * step;
        if (v != *value) { *value = v; changed = true; }
    }
    *value = SDL_clamp(*value, lo, hi);
    float f = (*value - lo) / (hi - lo);
    ui_fill(ui, tr, t->track, tr.h / 2);
    ui_fill(ui, (SDL_FRect){ tr.x, tr.y, tr.w * f, tr.h }, t->accent, tr.h / 2);
    float k = dp(ui, 18);
    ui_circle(ui, tr.x + tr.w * f, tr.y + tr.h / 2, k / 2, t->high_contrast ? t->accent : t->text);
    char buf[32];
    SDL_snprintf(buf, sizeof buf, fmt ? fmt : "%.0f", *value);
    ui_text_center(ui, FONT_BOLD, TS_BODY, (SDL_FRect){ tr.x + tr.w + dp(ui, 12), r.y, dp(ui, 60), r.h }, t->text, buf, true);
    return changed;
}

bool ui_segmented(Ui *ui, Uint32 id, SDL_FRect r, const char *label, const char *const *opts,
                  int count, int *value)
{
    UiItemState st = ui_item(ui, id, r);
    const Theme *t = &ui->th;
    row_frame(ui, r, st);
    bool changed = false;
    float total = 0;
    float pad = dp(ui, 14);
    for (int i = 0; i < count; i++) total += ui_text_w(ui, FONT_BOLD, TS_BODY, opts[i]) + 2 * pad;
    float maxw = r.w * 0.62f;
    float scale = total > maxw ? maxw / total : 1;
    float sh = dp(ui, 34);
    float x = r.x + r.w - total * scale - dp(ui, 12);
    row_label(ui, r, label, NULL, total * scale + dp(ui, 24));
    SDL_FRect bg = { x - dp(ui, 3), r.y + (r.h - sh) / 2 - dp(ui, 3), total * scale + dp(ui, 6), sh + dp(ui, 6) };
    ui_fill(ui, bg, t->dark ? rgba(0x000000, 70) : rgba(0x000000, 18), bg.h / 2);
    if (st.focused) {
        g_capture_lr_next = id;
        if (g_lr_pending) {
            int v = SDL_clamp(*value + g_lr_pending, 0, count - 1);
            g_lr_pending = 0;
            if (v != *value) { *value = v; changed = true; }
        }
    }
    for (int i = 0; i < count; i++) {
        float w = (ui_text_w(ui, FONT_BOLD, TS_BODY, opts[i]) + 2 * pad) * scale;
        SDL_FRect sr = { x, r.y + (r.h - sh) / 2, w, sh };
        bool sel = *value == i;
        bool hov = rect_has(sr, ui->mx, ui->my) && st.hover;
        if (sel) ui_fill(ui, sr, t->accent, sh / 2);
        else if (hov) ui_fill(ui, sr, t->surface_hi, sh / 2);
        ui_text_center(ui, FONT_BOLD, TS_BODY, sr, sel ? t->on_accent : t->text, opts[i], true);
        if (hov && ui->mouse_released && !sel) { *value = i; changed = true; }
        x += w;
    }
    return changed;
}

bool ui_textfield(Ui *ui, Uint32 id, SDL_FRect r, char *buf, size_t cap, const char *placeholder)
{
    UiItemState st = ui_item(ui, id, r);
    const Theme *t = &ui->th;
    bool enter = false;
    ui_fill(ui, r, t->dark ? rgba(0x000000, 90) : rgb(0xFFFFFF), dp(ui, 10));
    ui_stroke(ui, r, st.focused ? t->accent : t->border, dp(ui, 10), dp(ui, st.focused ? 2 : 1));
    if (st.focused) {
        g_text_focus_next = id;
        g_capture_lr_next = id;
        g_lr_pending = 0;
        if (ui->text_in[0]) {
            size_t len = strlen(buf);
            SDL_strlcpy(buf + len, ui->text_in, cap - len);
            ui->text_in[0] = 0;
        }
        while (ui->text_backspace > 0) {
            size_t len = strlen(buf);
            if (!len) { ui->text_backspace = 0; break; }
            len--;
            while (len > 0 && (buf[len] & 0xC0) == 0x80) len--;
            buf[len] = 0;
            ui->text_backspace--;
        }
        if (ui->text_enter) enter = true;
    }
    float pad = dp(ui, 12);
    float lh = ui_line_h(ui, TS_BODY);
    float y = r.y + (r.h - lh) / 2;
    ui_clip_push(ui, (SDL_FRect){ r.x + pad, r.y, r.w - 2 * pad, r.h });
    float w = ui_text_w(ui, FONT_REGULAR, TS_BODY, buf);
    float x = r.x + pad;
    if (w > r.w - 2 * pad - dp(ui, 4)) x -= w - (r.w - 2 * pad - dp(ui, 4)); /* keep the end visible */
    if (buf[0]) ui_text(ui, FONT_REGULAR, TS_BODY, x, y, t->text, buf);
    else if (placeholder) ui_text(ui, FONT_REGULAR, TS_BODY, x, y, t->text_dim, placeholder);
    if (st.focused && fmodf((float)ui->time, 1.0f) < 0.6f)
        ui_fill(ui, (SDL_FRect){ x + (buf[0] ? w : 0) + 1, y, dp(ui, 2), lh }, t->accent, 0);
    if (st.focused) ui->needs_redraw = true;
    ui_clip_pop(ui);
    return enter;
}

void ui_progress(Ui *ui, SDL_FRect r, float frac, Color fill)
{
    frac = SDL_clamp(frac, 0, 1);
    ui_fill(ui, r, ui->th.track, r.h / 2);
    if (frac > 0) {
        float w = SDL_max(r.h, r.w * frac);
        ui_fill(ui, (SDL_FRect){ r.x, r.y, w, r.h }, fill, r.h / 2);
    }
}

void ui_watch_status(Ui *ui, SDL_FRect r, float frac, bool watched, Color fill)
{
    float cs = r.h; /* checkmark badge */
    if (watched) {
        SDL_FRect bar = { r.x, r.y + (r.h - dp(ui, 4)) / 2, r.w - cs - dp(ui, 6), dp(ui, 4) };
        ui_progress(ui, bar, frac > 0.01f ? frac : 1.0f, ui->th.watched);
        float cx = r.x + r.w - cs / 2, cy = r.y + r.h / 2;
        ui_circle(ui, cx, cy, cs / 2, ui->th.watched);
        ui_icon(ui, ICON_CHECK, (SDL_FRect){ cx - cs * 0.4f, cy - cs * 0.4f, cs * 0.8f, cs * 0.8f },
                ui->th.dark || ui->th.high_contrast ? rgb(0x0B0F0C) : rgb(0xFFFFFF));
    } else if (frac > 0) {
        SDL_FRect bar = { r.x, r.y + (r.h - dp(ui, 4)) / 2, r.w, dp(ui, 4) };
        ui_progress(ui, bar, frac, fill);
    }
}


int ui_capture_lr(Ui *ui, Uint32 id)
{
    if (ui->focus != id) return 0;
    g_capture_lr_next = id;
    int v = g_lr_pending;
    g_lr_pending = 0;
    return v;
}

int ui_confirm_dialog(Ui *ui, const char *title, const char *body, const char *yes, const char *no, bool first_frame)
{
    const Theme *t = &ui->th;
    int saved = ui->layer;
    ui->layer = UI_MODAL_LAYER;
    ui_fill(ui, (SDL_FRect){ 0, 0, (float)ui->w, (float)ui->h }, rgba(0x000000, 170), 0);
    float w = SDL_min(dp(ui, 480), ui->w - dp(ui, 40));
    float pad = dp(ui, 28);
    float th = ui_line_h(ui, TS_TITLE);
    /* measure the body by drawing it off-screen is not possible: estimate the lines */
    float lh = ui_line_h(ui, TS_BODY);
    int lines = (int)(ui_text_w(ui, FONT_REGULAR, TS_BODY, body) / (w - 2 * pad)) + 1;
    float h = pad + th + dp(ui, 10) + lines * lh + dp(ui, 26) + dp(ui, 48) + pad;
    SDL_FRect r = { (ui->w - w) / 2, (ui->h - h) / 2, w, h };
    ui_shadow(ui, r, dp(ui, 22), dp(ui, 40), rgba(0x000000, 200));
    ui_fill(ui, r, t->surface, dp(ui, 22));
    ui_stroke(ui, r, t->high_contrast ? t->border : with_alpha(t->border, 255), dp(ui, 22), dp(ui, t->high_contrast ? 2 : 1));
    float y = r.y + pad;
    ui_text_fit(ui, FONT_BOLD, TS_TITLE, r.x + pad, y, w - 2 * pad, t->text, title);
    y += th + dp(ui, 10);
    ui_text_wrap(ui, FONT_REGULAR, TS_BODY, r.x + pad, y, w - 2 * pad, t->text_dim, body, 8);
    float by = r.y + h - pad - dp(ui, 48);
    float bw = (w - 2 * pad - dp(ui, 12)) / 2;
    Uint32 id_no = ui_id("confirm-no");
    if (first_frame) ui_focus(ui, id_no);
    int result = 0;
    if (ui_button(ui, id_no, (SDL_FRect){ r.x + pad, by, bw, dp(ui, 48) }, no, ICON_NONE, false)) result = 2;
    if (ui_button(ui, ui_id("confirm-yes"), (SDL_FRect){ r.x + pad + bw + dp(ui, 12), by, bw, dp(ui, 48) }, yes, ICON_NONE, true))
        result = 1;
    ui->layer = saved;
    ui->needs_redraw = true;
    return result;
}
