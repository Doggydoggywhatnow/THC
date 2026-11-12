/* test_tablayout.c: the top bar's tab spacing (#33): equal gaps, no overlap, everything inside the bar. */
#include "tablayout.h"
#include "test_util.h"

#include <math.h>

static const float k_labels[] = { 40, 52, 96, 88, 62, 120, 48, 70, 33, 101 };   /* uneven on purpose */

int main(void)
{
    /* the 0.3.0 look is the baseline: 5 tabs, scale 1, a wide bar */
    {
        float lw[5] = { 50, 52, 96, 88, 44 };
        TabRect t[5];
        bool compact = tab_layout(lw, 5, 0, 120, 1400, 1.0f, t);
        CHECK(!compact);
        CHECK_NEAR(t[0].x, 120);
        CHECK_NEAR(t[0].w, 50 + 52);                      /* label + 14 + 18 + 6 + 14 */
        CHECK_NEAR(t[1].x, 120 + 102 + 8);
        CHECK_NEAR(t[2].x - (t[1].x + t[1].w), 8);
        for (int i = 0; i < 5; i++) CHECK(t[i].labelled);
    }
    const int counts[] = { 3, 5, 8, 10 };
    const float scales[] = { 0.75f, 1.0f, 1.5f, 2.0f };
    const float widths[] = { 700, 900, 1100, 1280, 1600, 2560 };
    for (int ci = 0; ci < 4; ci++)
        for (int si = 0; si < 4; si++)
            for (int wi = 0; wi < 6; wi++) {
                int n = counts[ci];
                float s = scales[si], bar = widths[wi];
                float lw[TAB_MAX];
                for (int i = 0; i < n; i++) lw[i] = k_labels[i % 10] * s;         /* text scales with the UI */
                float x0 = 28 * s + 70 * s + LOGO_TO_TABS_GAP * s;                 /* margin + logo + gap */
                for (int sel = 0; sel < n; sel++) {
                    float min_bar = tab_layout_min_bar(lw, n, sel, x0, s);
                    TabRect t[TAB_MAX];
                    bool compact = tab_layout(lw, n, sel, x0, bar, s, t);
                    for (int i = 0; i < n; i++) {
                        CHECK(t[i].x >= x0 - 0.001f);                              /* never over the logo */
                        CHECK(t[i].labelled == (!compact || i == sel));
                        CHECK(t[i].w >= TAB_MIN_W * s - 0.001f);                   /* same minimum width */
                        if (i > 0) {
                            float gap = t[i].x - (t[i - 1].x + t[i - 1].w);
                            CHECK(fabsf(gap - TAB_GAP * s) < 0.001f);              /* equal gaps, labelled or not */
                        }
                    }
                    if (bar >= min_bar) {                                          /* inside the bar, room left for the buttons */
                        float end = t[n - 1].x + t[n - 1].w;
                        CHECK(end + TAB_RIGHT_RESERVE * s <= bar + 0.01f);
                    }
                    if (!compact) CHECK(bar >= x0 + TAB_RIGHT_RESERVE * s);
                    /* an icon-only tab is exactly the minimum width */
                    for (int i = 0; i < n; i++)
                        if (!t[i].labelled) CHECK_NEAR(t[i].w, TAB_MIN_W * s);
                }
            }
    /* a bar that fits compact but not full is compact; a roomy one is not */
    {
        float lw[8] = { 60, 70, 90, 100, 80, 70, 60, 50 };
        TabRect t[8];
        CHECK(tab_layout(lw, 8, 2, 140, 800, 1.0f, t));
        CHECK(!tab_layout(lw, 8, 2, 140, 2000, 1.0f, t));
    }
    TEST_DONE();
}
