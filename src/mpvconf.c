#include "mpvconf.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *const k_osc_options[] = {
    "layout", "windowcontrols", "seekbarstyle", "seekbarhandlesize", "seekrangestyle", "seekrangeseparate",
    "seekrangealpha", "boxalpha", "boxmaxchars", "boxvideo", "vidscale", "scalewindowed", "scalefullscreen",
    "showwindowed", "showfullscreen", "hidetimeout", "fadeduration", "minmousemove", "deadzonesize",
    "visibility", "valign", "halign", "barmargin", "timetotal", "timems", "tooltipborder", "greenandgrumpy",
    "livemarkers", "chapters_osd", "playlist_osd", "chapter_fmt", "unicodeminus", "title", "tooltip_hints",
    "windowcontrols_title", "windowcontrols_alignment", "background_color", "timecolor", "hovereffect", NULL
};

static const char *const k_thc_controlled[] = {
    "resume-playback", "save-position-on-quit", "watch-later-directory", "start", "ontop", "ontop-level", "screenshot-directory", "screenshot-format", "screenshot-template", NULL
};

/* Names the mpv.conf parser accepts as aliases but the client API does not. */
static const char *const k_aliases[][2] = { { "glsl-shader", "glsl-shaders" }, { NULL, NULL } };

static bool in_list(const char *const *list, const char *name)
{
    for (int i = 0; list[i]; i++)
        if (!strcmp(list[i], name)) return true;
    return false;
}

bool mpvconf_is_osc_option(const char *name)
{
    if (!strncmp(name, "osc-", 4)) return true;  /* "osc-layout" is not an option either */
    return in_list(k_osc_options, name);
}

bool mpvconf_is_thc_controlled(const char *name)
{
    return in_list(k_thc_controlled, name);
}

static char *trim(char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)e[-1])) *--e = 0;
    return p;
}

/* Split one config line. Returns false for blank lines, comments and [section] headers. */
static bool split_line(char *line, char **key, char **value)
{
    char *p = trim(line);
    if (!*p || *p == '#' || *p == '[') return false;
    char *eq = strchr(p, '=');
    if (eq) {
        *eq = 0;
        *key = trim(p);
        *value = trim(eq + 1);
        size_t n = strlen(*value);
        if (n >= 2 && ((*value)[0] == '"' || (*value)[0] == '\'') && (*value)[n - 1] == (*value)[0]) {
            (*value)[n - 1] = 0;
            (*value)++;
        }
    } else {
        *key = p;
        *value = (char *)"yes";   /* a bare flag */
    }
    return **key != 0;
}

int mpvconf_check(const char *text, MpvOptionSetter set, void *ud, MpvConfIssue *out, int max)
{
    int n = 0, lineno = 0;
    const char *p = text;
    while (*p && n < max) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[512];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        if (len && line[len - 1] == '\r') line[len - 1] = 0;
        p = nl ? nl + 1 : p + len;
        lineno++;

        char *key, *value;
        if (!split_line(line, &key, &value)) continue;
        char name[64];
        snprintf(name, sizeof name, "%s", key);
        const char *val = value;
        if (!strncmp(name, "no-", 3) && !strchr(key, '=')) {   /* no-osc  ==  osc=no */
            memmove(name, name + 3, strlen(name + 3) + 1);
            val = "no";
        }
        int rc = set(ud, name, val);
        if (rc == -5)
            for (int k = 0; k_aliases[k][0]; k++)
                if (!strcmp(name, k_aliases[k][0])) rc = set(ud, k_aliases[k][1], val);
        MpvConfIssue *is = &out[n];
        memset(is, 0, sizeof *is);
        is->line = lineno;
        snprintf(is->option, sizeof is->option, "%s", name);
        snprintf(is->value, sizeof is->value, "%s", val);
        if (rc == 0) {
            if (!mpvconf_is_thc_controlled(name)) continue;
            is->kind = MCK_THC_CONTROLLED;
            snprintf(is->hint, sizeof is->hint,
                     "THC controls this so its own watch history decides where you resume. The line has no effect.");
        } else if (rc == -5) {
            if (mpvconf_is_osc_option(name)) {
                is->kind = MCK_OSC_OPTION;
                const char *bare = !strncmp(name, "osc-", 4) ? name + 4 : name;
                snprintf(is->hint, sizeof is->hint,
                         "Controller settings go in script-opts/osc.conf as %s=%s, not in mpv.conf.", bare, val);
            } else {
                is->kind = MCK_NOT_FOUND;
                snprintf(is->hint, sizeof is->hint,
                         "MPV has no option with this name, so it skips the line. Check the spelling and your MPV version.");
            }
        } else {
            is->kind = MCK_BAD_VALUE;
            snprintf(is->hint, sizeof is->hint, "MPV did not accept this value, so it skips the line.");
        }
        n++;
    }
    return n;
}

bool mpvconf_get_key(const char *text, const char *key, char *value, size_t cap)
{
    const char *p = text;
    bool found = false;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[512];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p = nl ? nl + 1 : p + len;
        char *k, *v;
        if (split_line(line, &k, &v) && !strcmp(k, key)) {
            snprintf(value, cap, "%s", v);
            found = true;   /* the last one wins, as in MPV */
        }
    }
    return found;
}

bool mpvconf_set_key(const char *text, const char *key, const char *value, char *out, size_t cap)
{
    size_t o = 0;
    bool done = false;
    const char *p = text;
    #define PUT(s, n) do { if (o + (n) + 1 > cap) return false; memcpy(out + o, (s), (n)); o += (n); } while (0)
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        size_t full = len + (nl ? 1 : 0);
        char line[512];
        size_t cl = len < sizeof line - 1 ? len : sizeof line - 1;
        memcpy(line, p, cl);
        line[cl] = 0;
        char *k, *v;
        if (split_line(line, &k, &v) && !strcmp(k, key) && !done) {
            char buf[700];
            int bl = snprintf(buf, sizeof buf, "%s=%s%s", key, value, nl ? "\n" : "");
            PUT(buf, (size_t)bl);
            done = true;
        } else {
            PUT(p, full);
        }
        p += full;
    }
    if (!done) {
        if (o && out[o - 1] != '\n') PUT("\n", 1);
        char buf[700];
        int bl = snprintf(buf, sizeof buf, "%s=%s\n", key, value);
        PUT(buf, (size_t)bl);
    }
    out[o] = 0;
    #undef PUT
    return true;
}
