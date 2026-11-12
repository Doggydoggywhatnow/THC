#include "mpvconf.h"
#include "ontop.h"
#include "test_util.h"

int main(void)
{
    /* modes */
    CHECK(!ontop_wanted(ONTOP_OFF, true, false, true));
    CHECK(!ontop_wanted(ONTOP_OFF, false, false, true));
    CHECK(ontop_wanted(ONTOP_PLAYING, true, false, true));
    CHECK(!ontop_wanted(ONTOP_PLAYING, false, false, true));    /* paused: not on top */
    CHECK(ontop_wanted(ONTOP_ALWAYS, true, false, true));
    CHECK(ontop_wanted(ONTOP_ALWAYS, false, false, true));
    /* fullscreen is ignored by default, and honoured when asked */
    CHECK(!ontop_wanted(ONTOP_ALWAYS, true, true, true));
    CHECK(!ontop_wanted(ONTOP_PLAYING, true, true, true));
    CHECK(ontop_wanted(ONTOP_ALWAYS, true, true, false));
    CHECK(ontop_wanted(ONTOP_PLAYING, true, true, false));
    CHECK(!ontop_wanted(99, true, false, true));                /* unknown mode: off */
    /* macOS level and the hotkey cycle */
    CHECK_STR(ontop_level_name(0), "window");
    CHECK_STR(ontop_level_name(1), "system");
    CHECK(ontop_next_mode(ONTOP_OFF) == ONTOP_PLAYING);
    CHECK(ontop_next_mode(ONTOP_PLAYING) == ONTOP_ALWAYS);
    CHECK(ontop_next_mode(ONTOP_ALWAYS) == ONTOP_OFF);
    CHECK_STR(ontop_mode_label(ONTOP_PLAYING), "While playing");
    /* Wayland detection decides whether the settings page shows its note */
    CHECK(ontop_is_wayland("wayland", NULL));
    CHECK(!ontop_is_wayland("x11", "wayland-0"));               /* explicit session type wins */
    CHECK(ontop_is_wayland(NULL, "wayland-0"));
    CHECK(!ontop_is_wayland(NULL, NULL));
    CHECK(!ontop_is_wayland("", ""));
    /* mpv.conf cannot override it */
    CHECK(mpvconf_is_thc_controlled("ontop"));
    CHECK(mpvconf_is_thc_controlled("ontop-level"));
    CHECK(!mpvconf_is_thc_controlled("fullscreen"));
    TEST_DONE();
}
