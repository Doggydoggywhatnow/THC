/* tablayout.c: the top bar's tab positions (#33). See include/tablayout.h. */
#include "tablayout.h"

static float labelled_w(float label_w, float s)
{
    return label_w + (TAB_PAD_X + TAB_ICON + TAB_ICON_LABEL_GAP + TAB_PAD_RIGHT) * s;
}

static float tab_w(float label_w, bool labelled, float s)
{
    return labelled ? labelled_w(label_w, s) : TAB_MIN_W * s;
}

bool tab_layout(const float *label_w, int n, int selected, float x0, float bar_w, float s, TabRect *out)
{
    if (n > TAB_MAX) n = TAB_MAX;
    float need = x0 + TAB_RIGHT_RESERVE * s;
    for (int i = 0; i < n; i++) need += labelled_w(label_w[i], s) + TAB_GAP * s;
    bool compact = need > bar_w;
    float x = x0;
    for (int i = 0; i < n; i++) {
        bool lab = !compact || i == selected;
        out[i].labelled = lab;
        out[i].x = x;
        out[i].w = tab_w(label_w[i], lab, s);
        x += out[i].w + TAB_GAP * s;
    }
    return compact;
}

float tab_layout_min_bar(const float *label_w, int n, int selected, float x0, float s)
{
    if (n > TAB_MAX) n = TAB_MAX;
    float w = x0 + TAB_RIGHT_RESERVE * s;
    for (int i = 0; i < n; i++) w += tab_w(label_w[i], i == selected, s) + TAB_GAP * s;
    return w;
}
