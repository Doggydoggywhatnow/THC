#include "mpvconf.h"
#include "test_util.h"

/* A fake MPV that knows a few options. */
static int fake(void *ud, const char *name, const char *value)
{
    (void)ud;
    static const char *const known[] = { "profile", "hwdec", "deband", "volume", "osc", "save-position-on-quit",
                                         "glsl-shaders", "glsl-shaders-append", "screenshot-format", NULL };
    (void)0;
    for (int i = 0; known[i]; i++)
        if (!strcmp(name, known[i])) return !strcmp(name, "volume") && value[0] == 'x' ? -6 : 0;
    return -5;
}

int main(void)
{
    const char *conf =
        "# my config\n"
        "profile=high-quality\n"
        "hwdec=vaapi\n"
        "save-position-on-quit=yes\n"
        "layout=topbar\n"
        "osc-border=yes\n"
        "osc-window-control=quit\r\n"
        "drag-to-pan=yes\n"
        "\n"
        "[extra]\n"
        "volume=xyz\n"
        "no-osc\n"
        "glsl-shader=\"~~/shaders/a.glsl\"\n"
        "deband\n";
    MpvConfIssue is[16];
    int n = mpvconf_check(conf, fake, NULL, is, 16);
    CHECK(n == 6);   /* glsl-shader is an alias of glsl-shaders-append, so it is fine */
    CHECK(is[0].line == 4 && is[0].kind == MCK_THC_CONTROLLED && !strcmp(is[0].option, "save-position-on-quit"));
    CHECK(is[1].line == 5 && is[1].kind == MCK_OSC_OPTION && !strcmp(is[1].option, "layout"));
    CHECK(strstr(is[1].hint, "layout=topbar") && strstr(is[1].hint, "osc.conf"));
    CHECK(is[2].line == 6 && is[2].kind == MCK_OSC_OPTION);
    CHECK(strstr(is[2].hint, "border=yes"));      /* the osc- prefix is dropped in the hint */
    CHECK(is[3].line == 7 && is[3].kind == MCK_OSC_OPTION);
    CHECK(is[4].line == 8 && is[4].kind == MCK_NOT_FOUND && !strcmp(is[4].option, "drag-to-pan"));
    CHECK(is[5].line == 11 && is[5].kind == MCK_BAD_VALUE && !strcmp(is[5].option, "volume"));
    /* quoted value is unquoted before MPV sees it */
    n = mpvconf_check("glsl-shaders-append=\"~~/s.glsl\"\n", fake, NULL, is, 16);
    CHECK(n == 0);
    CHECK(mpvconf_check("", fake, NULL, is, 16) == 0);
    CHECK(mpvconf_check("# only a comment\n", fake, NULL, is, 16) == 0);

    CHECK(mpvconf_is_osc_option("windowcontrols"));
    CHECK(!mpvconf_is_osc_option("hwdec"));
    CHECK(mpvconf_is_thc_controlled("start"));

    /* get / set keep comments and order */
    char v[64], out[512];
    CHECK(mpvconf_get_key("# c\nlayout=box\n", "layout", v, sizeof v));
    CHECK_STR(v, "box");
    CHECK(!mpvconf_get_key("# layout=box\n", "layout", v, sizeof v));
    CHECK(mpvconf_set_key("# c\nlayout=box\nboxalpha=60\n", "layout", "topbar", out, sizeof out));
    CHECK_STR(out, "# c\nlayout=topbar\nboxalpha=60\n");
    CHECK(mpvconf_set_key("# c\nboxalpha=60", "layout", "topbar", out, sizeof out));
    CHECK_STR(out, "# c\nboxalpha=60\nlayout=topbar\n");
    CHECK(mpvconf_set_key("", "layout", "slimbox", out, sizeof out));
    CHECK_STR(out, "layout=slimbox\n");
    CHECK(!mpvconf_set_key("a=b\n", "layout", "topbar", out, 8));   /* does not fit */
    /* a hotkey as an mpv key name (the NSFW panic key is bound inside mpv's window) */
    {
        char k[48];
        CHECK(mpvconf_key_from_hotkey("Ctrl+Shift+X", k, sizeof k) && !strcmp(k, "Ctrl+X"));
        CHECK(mpvconf_key_from_hotkey("Ctrl+x", k, sizeof k) && !strcmp(k, "Ctrl+x"));
        CHECK(mpvconf_key_from_hotkey("X", k, sizeof k) && !strcmp(k, "x"));
        CHECK(mpvconf_key_from_hotkey("Shift+X", k, sizeof k) && !strcmp(k, "X"));
        CHECK(mpvconf_key_from_hotkey("Alt+F9", k, sizeof k) && !strcmp(k, "Alt+F9"));
        CHECK(mpvconf_key_from_hotkey("Ctrl+Shift+F9", k, sizeof k) && !strcmp(k, "Ctrl+Shift+F9"));
        CHECK(mpvconf_key_from_hotkey("Ctrl+Space", k, sizeof k) && !strcmp(k, "Ctrl+SPACE"));
        CHECK(mpvconf_key_from_hotkey("Escape", k, sizeof k) && !strcmp(k, "ESC"));
        CHECK(!mpvconf_key_from_hotkey("", k, sizeof k) && !mpvconf_key_from_hotkey("Ctrl+", k, sizeof k) && !mpvconf_key_from_hotkey(NULL, k, sizeof k));
        CHECK(!mpvconf_key_from_hotkey("Ctrl+Shift+X", k, 4));                  /* too small a buffer */
    }
    TEST_DONE();
}
