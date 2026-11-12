/* ui.h: THC's small immediate-mode UI toolkit on top of SDL_Renderer.
 *
 * - Everything is laid out in "dp" (density-independent units); dp() turns
 *   them into pixels using the display density and the user's UI scale.
 * - Text is drawn with stb_truetype into a glyph cache; missing characters
 *   (CJK, etc.) fall back to system fonts. Text size scales on its own.
 * - Every interactive element is "focusable": arrow keys move focus to the
 *   nearest element in that direction, Tab/Shift+Tab cycle, Enter activates,
 *   and the focus ring is always visible while navigating by keyboard.
 */
#ifndef THC_UI_H
#define THC_UI_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Color { Uint8 r, g, b, a; } Color;

static inline Color rgba(Uint32 hex, Uint8 a) {
    Color c = { (Uint8)(hex >> 16), (Uint8)(hex >> 8), (Uint8)hex, a };
    return c;
}
static inline Color rgb(Uint32 hex) { return rgba(hex, 255); }
static inline Color with_alpha(Color c, Uint8 a) { c.a = a; return c; }
Color color_mix(Color a, Color b, float t);

typedef struct Theme {
    bool dark;
    bool high_contrast;
    Color bg, bg2;          /* page background gradient (top, bottom) */
    Color surface;          /* cards, panels */
    Color surface_hi;       /* hovered surface */
    Color border;
    Color text, text_dim;
    Color accent, accent2;  /* primary accent and a secondary for gradients */
    Color on_accent;        /* text on accent */
    Color focus;            /* focus ring */
    Color watched;          /* checkmark */
    Color track;            /* empty part of progress bars */
    Color shadow;
} Theme;

enum { FONT_REGULAR = 0, FONT_BOLD = 1 };

/* Text roles, in dp before text scaling. */
enum { TS_SMALL = 12, TS_BODY = 15, TS_LABEL = 16, TS_TITLE = 20, TS_H2 = 26, TS_H1 = 34, TS_HERO = 46 };

enum { NAV_NONE = 0, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_NEXT, NAV_PREV };

typedef struct UiItemState {
    bool hover;      /* mouse is over it */
    bool focused;    /* has keyboard focus */
    bool activated;  /* clicked, or Enter pressed while focused */
    bool pressed;    /* mouse button held on it */
    bool context;    /* right-clicked, or the Menu key / Shift+F10 while focused */
} UiItemState;

typedef struct UiRegion {
    Uint32 id;
    bool horizontal;
    float off, target;  /* scroll offset (dp-free: pixels) */
    float view;         /* visible length in pixels */
    float content;      /* content length in pixels (from last frame) */
    SDL_FRect rect;
    Uint64 last_frame;
} UiRegion;

typedef struct UiItem {
    Uint32 id;
    SDL_FRect r;
    int layer;
} UiItem;

#define UI_MAX_ITEMS 4096
#define UI_MAX_REGIONS 128
#define UI_MAX_CLIP 16

typedef struct Ui {
    SDL_Window *win;
    SDL_Renderer *ren;
    int w, h;               /* window size in pixels */
    float density;          /* display pixel density */
    float ui_scale;         /* user setting */
    float text_scale;       /* user setting */
    float s;                /* density * ui_scale */
    bool reduced_motion;
    Theme th;
    double time;            /* seconds */
    float dt;
    Uint64 frame;

    /* input (this frame) */
    float mx, my;
    bool mouse_down, mouse_pressed, mouse_released;
    bool mouse_moved;
    float wheel_x, wheel_y;
    int nav;
    bool key_activate;
    bool right_pressed;      /* right button went down this frame (at mx, my) */
    bool key_context;        /* Menu key or Shift+F10 */
    bool key_back;
    bool keyboard_mode;      /* last input was keyboard: show the focus ring */
    char text_in[64];        /* SDL text input this frame */
    int text_backspace;
    bool text_enter;

    /* focus */
    Uint32 focus;
    Uint32 pressed_id;
    Uint32 hover_id;         /* hovered item from last frame (topmost) */
    Uint32 hover_next;
    int hover_layer_next;
    bool scroll_to_focus;
    Uint32 want_focus;       /* request focus next frame */
    UiItem items[2][UI_MAX_ITEMS];
    int nitems[2];
    int cur;
    int layer;               /* items in higher layers (overlays) win */
    bool modal;              /* only layer >= modal_layer is focusable */
    int modal_layer;

    /* scrolling and clipping */
    UiRegion regions[UI_MAX_REGIONS];
    UiRegion *rstack[8];
    int rdepth;
    SDL_Rect clip[UI_MAX_CLIP];
    int nclip;

    bool needs_redraw;        /* animations still running */
    bool text_active;         /* a text field has focus: keys are typing */
} Ui;

bool ui_init(Ui *ui, SDL_Window *win, SDL_Renderer *ren, const char *font_override);
void ui_shutdown(Ui *ui);
/* Reload the primary font (NULL/empty = the bundled Atkinson Hyperlegible). */
bool ui_set_font(Ui *ui, const char *path);

/* Feed SDL events; returns true if the event was used by the UI. */
bool ui_event(Ui *ui, SDL_Event *ev);
void ui_begin(Ui *ui);
void ui_end(Ui *ui);

static inline float dp(const Ui *ui, float v) { return v * ui->s; }
float ui_font_px(const Ui *ui, float size_dp);

Uint32 ui_id(const char *s);
Uint32 ui_idi(const char *s, int i);
Uint32 ui_idp(const char *s, const void *p);

/* Register an interactive element. */
UiItemState ui_item(Ui *ui, Uint32 id, SDL_FRect r);
void ui_focus(Ui *ui, Uint32 id);
bool ui_focus_exists(const Ui *ui, Uint32 id);
/* Draw the focus ring for an element (only in keyboard mode). */
void ui_focus_ring(Ui *ui, SDL_FRect r, float radius);

/* Scrolling regions. Content is drawn at view.x/y minus the returned offset. */
float ui_scroll_begin(Ui *ui, Uint32 id, SDL_FRect view, bool horizontal);
void ui_scroll_end(Ui *ui, float content_len);
void ui_scroll_reset(Ui *ui, Uint32 id);
/* Smoothly scroll a region to an offset in pixels (clamped on the next frame). */
void ui_scroll_to(Ui *ui, Uint32 id, float pixels);

void ui_clip_push(Ui *ui, SDL_FRect r);
void ui_clip_pop(Ui *ui);
bool ui_visible(const Ui *ui, SDL_FRect r);

/* Smoothly approach a target (or jump, with reduced motion). */
float ui_anim(Ui *ui, float *v, float target, float speed);
/* Same, with the value stored per element id (hover lifts, highlights). */
float ui_anim_id(Ui *ui, Uint32 id, float target, float speed);

/* Drawing. Sizes are in pixels. */
void ui_fill(Ui *ui, SDL_FRect r, Color c, float radius);
void ui_fill_grad(Ui *ui, SDL_FRect r, Color top, Color bottom, float radius);
void ui_fill_grad_h(Ui *ui, SDL_FRect r, Color left, Color right, float radius);
void ui_stroke(Ui *ui, SDL_FRect r, Color c, float radius, float width);
void ui_shadow(Ui *ui, SDL_FRect r, float radius, float blur, Color c);
void ui_circle(Ui *ui, float cx, float cy, float rad, Color c);
void ui_line(Ui *ui, float x0, float y0, float x1, float y1, float width, Color c);
void ui_tri(Ui *ui, float x0, float y0, float x1, float y1, float x2, float y2, Color c);
void ui_image(Ui *ui, SDL_Texture *t, SDL_FRect r, float radius, Uint8 alpha);

/* Text. `size` is in dp (TS_*); text scaling is applied. Returns width in px. */
float ui_text(Ui *ui, int font, float size, float x, float y, Color c, const char *s);
float ui_text_w(Ui *ui, int font, float size, const char *s);
float ui_line_h(Ui *ui, float size);
float ui_ascent(Ui *ui, float size);
/* Single line, cut with an ellipsis to fit max_w. */
float ui_text_fit(Ui *ui, int font, float size, float x, float y, float max_w, Color c, const char *s);
/* Centered in a box (vertically and optionally horizontally). */
void ui_text_center(Ui *ui, int font, float size, SDL_FRect r, Color c, const char *s, bool hcenter);
/* Word-wrapped; returns the height used. */
float ui_text_wrap(Ui *ui, int font, float size, float x, float y, float max_w, Color c, const char *s, int max_lines);

/* Icons, drawn as vectors in a square box. */
typedef enum UiIcon {
    ICON_NONE = -1,
    ICON_PLAY, ICON_PAUSE, ICON_NEXT, ICON_PREV, ICON_CHECK, ICON_GEAR, ICON_FILM,
    ICON_NOTE, ICON_BOOK, ICON_FOLDER, ICON_BACK, ICON_CHEVRON_RIGHT, ICON_CLOCK,
    ICON_VOLUME, ICON_REWIND, ICON_FORWARD, ICON_TV, ICON_STAR, ICON_SPARK, ICON_MOON,
    ICON_SUN, ICON_KEYBOARD, ICON_EYE, ICON_SHIELD, ICON_INFO, ICON_PLUS, ICON_CLOSE,
    ICON_SORT, ICON_QUEUE, ICON_SHUFFLE, ICON_REPEAT, ICON_REPEAT_ONE, ICON_UP, ICON_DOWN, ICON_GRIP, ICON_MORE, ICON_CAMERA, ICON_BEER
} UiIcon;
void ui_icon(Ui *ui, UiIcon icon, SDL_FRect box, Color c);

/* Widgets. All return true when activated. */
bool ui_button(Ui *ui, Uint32 id, SDL_FRect r, const char *label, UiIcon icon, bool primary);
bool ui_icon_button(Ui *ui, Uint32 id, SDL_FRect r, UiIcon icon, const char *tooltip);
/* A small tooltip below (or above) an anchor rectangle. */
void ui_tooltip(Ui *ui, SDL_FRect anchor, const char *text);
bool ui_pill(Ui *ui, Uint32 id, SDL_FRect r, const char *label, bool selected);
bool ui_toggle(Ui *ui, Uint32 id, SDL_FRect r, const char *label, const char *hint, bool *value);
/* Horizontal slider. Arrow keys adjust while focused. Returns true on change. */
bool ui_slider(Ui *ui, Uint32 id, SDL_FRect r, const char *label, float *value, float lo, float hi,
               float step, const char *fmt);
/* Segmented choice. Returns true on change. */
bool ui_segmented(Ui *ui, Uint32 id, SDL_FRect r, const char *label, const char *const *options,
                  int count, int *value);
/* Single-line text field. Returns true when Enter is pressed. */
bool ui_textfield(Ui *ui, Uint32 id, SDL_FRect r, char *buf, size_t cap, const char *placeholder);
/* Like ui_textfield, but draws a star for each character (PINs). */
bool ui_passfield(Ui *ui, Uint32 id, SDL_FRect r, char *buf, size_t cap, const char *placeholder);

/* For custom controls: while `id` has focus, Left/Right adjust it instead of
 * moving focus. Returns -1, 0 or +1 for this frame. */
int ui_capture_lr(Ui *ui, Uint32 id);

/* A real modal dialog: dims the window and takes all input until answered. The caller keeps
 * `ui->modal_layer = 5` while it is open (so nothing underneath can be reached) and calls this
 * last in the frame. Returns 0 while open, 1 for the confirm button, 2 for cancel. */
#define UI_MODAL_LAYER 5
int ui_confirm_dialog(Ui *ui, const char *title, const char *body, const char *yes, const char *no, bool first_frame);

/* Progress bar with an optional watched checkmark at its end. */
void ui_progress(Ui *ui, SDL_FRect r, float frac, Color fill);
void ui_watch_status(Ui *ui, SDL_FRect r, float frac, bool watched, Color fill);

/* Theme helpers. */
Theme theme_make(int portal, bool dark, bool high_contrast, int accent_preset);
enum { PORTAL_VIDEO = 0, PORTAL_MUSIC = 1, PORTAL_BOOKS = 2, PORTAL_NEUTRAL = 3, PORTAL_NSFW = 4 };
#define ACCENT_PRESET_COUNT 7
extern const char *const k_accent_names[ACCENT_PRESET_COUNT];

/* A colorful gradient from a string (for generated cover art). */
void art_colors(const char *seed, Color *a, Color *b);

#endif
