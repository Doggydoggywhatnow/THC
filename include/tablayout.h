/* tablayout.h: where the top bar's portal tabs go (#33). One place for every spacing value, in dp, so the gaps are
 * equal whatever the labels are and whether a tab shows its label. No SDL: tested on its own.
 *
 *   [ logo ] <LOGO_TO_TABS_GAP> [tab] <TAB_GAP> [tab] <TAB_GAP> [tab] ...  (right side keeps TAB_RIGHT_RESERVE)
 *   labelled tab:  PAD_X icon ICON_LABEL_GAP label PAD_RIGHT      icon-only tab: TAB_MIN_W, icon centered
 * Every tab is TAB_H high. The values are the 0.3.0 look, so introducing this changed nothing on screen. */
#ifndef THC_TABLAYOUT_H
#define THC_TABLAYOUT_H

#include <stdbool.h>

#define TAB_H 40.0f               /* height of every tab, so focus rings and hit targets line up */
#define TAB_GAP 8.0f              /* between neighbouring tabs */
#define TAB_PAD_X 14.0f           /* left edge of a labelled tab to its icon */
#define TAB_ICON 18.0f            /* icon size */
#define TAB_ICON_LABEL_GAP 6.0f   /* icon to label */
#define TAB_PAD_RIGHT 14.0f       /* label to the right edge */
#define TAB_MIN_W 46.0f           /* narrowest tab: an icon-only one */
#define TOPBAR_EDGE_MARGIN 28.0f  /* window edge to the logo, and to the right-hand buttons */
#define LOGO_TO_TABS_GAP 36.0f    /* logo text to the first tab */
#define TAB_RIGHT_RESERVE 150.0f  /* room kept free at the right for the queue button and scan status */
#define TAB_MAX 16

typedef struct TabRect {
    float x, w;               /* left edge and width in pixels */
    bool labelled;            /* shows its label (else icon only) */
} TabRect;

/* `label_w[i]` is the width in pixels of tab i's label text, `selected` the current tab, `s` the pixels per dp
 * (UI scale). Tabs start at `x0` and the bar is `bar_w` wide. When labels everywhere do not fit, only the selected
 * tab keeps its label. Returns true in that compact case. Writes n rects (n <= TAB_MAX). */
bool tab_layout(const float *label_w, int n, int selected, float x0, float bar_w, float s, TabRect *out);
/* The narrowest bar (pixels) that holds n tabs in the compact layout, with tab `selected` labelled. */
float tab_layout_min_bar(const float *label_w, int n, int selected, float x0, float s);

#endif
