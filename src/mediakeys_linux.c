/* MPRIS (org.mpris.MediaPlayer2) over D-Bus, for media keys, the desktop's media controls and
 * playerctl on Linux and the BSDs. libdbus is loaded at run time, so THC neither needs it to build
 * nor to start: without it the backend is simply absent. The few libdbus types we touch are declared
 * here instead of included, so there is no build dependency at all. */
#include "mediakeys.h"

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef unsigned int dbus_bool_t;
typedef struct { void *a, *b; unsigned char pad[200]; } DBusIter;   /* far larger than libdbus's own */
typedef struct { const char *name; const char *message; unsigned char pad[64]; } DBusErr;

enum { T_BYTE = 'y', T_BOOL = 'b', T_INT32 = 'i', T_INT64 = 'x', T_DOUBLE = 'd', T_STRING = 's', T_PATH = 'o',
       T_ARRAY = 'a', T_VARIANT = 'v', T_DICT = 'e' };

static struct {
    SDL_SharedObject *lib;
    DBusConnection *(*bus_get)(int, DBusErr *);
    int (*request_name)(DBusConnection *, const char *, unsigned, DBusErr *);
    int (*release_name)(DBusConnection *, const char *, DBusErr *);
    void (*exit_on_disconnect)(DBusConnection *, dbus_bool_t);
    dbus_bool_t (*read_write)(DBusConnection *, int);
    DBusMessage *(*pop)(DBusConnection *);
    dbus_bool_t (*send)(DBusConnection *, DBusMessage *, unsigned *);
    void (*flush)(DBusConnection *);
    void (*conn_unref)(DBusConnection *);
    void (*msg_unref)(DBusMessage *);
    const char *(*get_member)(DBusMessage *);
    const char *(*get_iface)(DBusMessage *);
    const char *(*get_path)(DBusMessage *);
    int (*get_type)(DBusMessage *);
    DBusMessage *(*new_return)(DBusMessage *);
    DBusMessage *(*new_error)(DBusMessage *, const char *, const char *);
    DBusMessage *(*new_signal)(const char *, const char *, const char *);
    dbus_bool_t (*iter_init)(DBusMessage *, DBusIter *);
    void (*iter_init_append)(DBusMessage *, DBusIter *);
    dbus_bool_t (*append_basic)(DBusIter *, int, const void *);
    dbus_bool_t (*open_container)(DBusIter *, int, const char *, DBusIter *);
    dbus_bool_t (*close_container)(DBusIter *, DBusIter *);
    int (*arg_type)(DBusIter *);
    void (*get_basic)(DBusIter *, void *);
    dbus_bool_t (*next)(DBusIter *);
    void (*recurse)(DBusIter *, DBusIter *);
    void (*err_init)(DBusErr *);
    void (*err_free)(DBusErr *);
    dbus_bool_t (*err_is_set)(const DBusErr *);
} D;

#define LOAD(field, name)                                                                                   \
    do {                                                                                                    \
        SDL_FunctionPointer f_ = SDL_LoadFunction(D.lib, name);                                             \
        if (!f_) return false;                                                                              \
        memcpy(&D.field, &f_, sizeof f_);                                                                   \
    } while (0)

static bool load_dbus(void)
{
    if (D.lib) return true;
    static const char *const names[] = { "libdbus-1.so.3", "libdbus-1.so", NULL };
    for (int i = 0; names[i] && !D.lib; i++) D.lib = SDL_LoadObject(names[i]);
    if (!D.lib) return false;
    LOAD(bus_get, "dbus_bus_get");
    LOAD(request_name, "dbus_bus_request_name");
    LOAD(release_name, "dbus_bus_release_name");
    LOAD(exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
    LOAD(read_write, "dbus_connection_read_write");
    LOAD(pop, "dbus_connection_pop_message");
    LOAD(send, "dbus_connection_send");
    LOAD(flush, "dbus_connection_flush");
    LOAD(conn_unref, "dbus_connection_unref");
    LOAD(msg_unref, "dbus_message_unref");
    LOAD(get_member, "dbus_message_get_member");
    LOAD(get_iface, "dbus_message_get_interface");
    LOAD(get_path, "dbus_message_get_path");
    LOAD(get_type, "dbus_message_get_type");
    LOAD(new_return, "dbus_message_new_method_return");
    LOAD(new_error, "dbus_message_new_error");
    LOAD(new_signal, "dbus_message_new_signal");
    LOAD(iter_init, "dbus_message_iter_init");
    LOAD(iter_init_append, "dbus_message_iter_init_append");
    LOAD(append_basic, "dbus_message_iter_append_basic");
    LOAD(open_container, "dbus_message_iter_open_container");
    LOAD(close_container, "dbus_message_iter_close_container");
    LOAD(arg_type, "dbus_message_iter_get_arg_type");
    LOAD(get_basic, "dbus_message_iter_get_basic");
    LOAD(next, "dbus_message_iter_next");
    LOAD(recurse, "dbus_message_iter_recurse");
    LOAD(err_init, "dbus_error_init");
    LOAD(err_free, "dbus_error_free");
    LOAD(err_is_set, "dbus_error_is_set");
    return true;
}

#define BUS_NAME "org.mpris.MediaPlayer2.thc"
#define OBJ_PATH "/org/mpris/MediaPlayer2"
#define IF_ROOT "org.mpris.MediaPlayer2"
#define IF_PLAYER "org.mpris.MediaPlayer2.Player"
#define IF_PROPS "org.freedesktop.DBus.Properties"

static DBusConnection *conn;
static bool owns_name;
static MediaActionFn act_fn;
static void *act_ud;
static NowPlaying np;

/* ---- appending helpers ------------------------------------------------- */

static void app_variant_begin(DBusIter *it, const char *sig, DBusIter *v) { D.open_container(it, T_VARIANT, sig, v); }

static void add_str(DBusIter *it, const char *s) { D.append_basic(it, T_STRING, &s); }

static void variant_bool(DBusIter *it, bool b)
{
    DBusIter v;
    dbus_bool_t x = b;
    app_variant_begin(it, "b", &v);
    D.append_basic(&v, T_BOOL, &x);
    D.close_container(it, &v);
}
static void variant_double(DBusIter *it, double d)
{
    DBusIter v;
    app_variant_begin(it, "d", &v);
    D.append_basic(&v, T_DOUBLE, &d);
    D.close_container(it, &v);
}
static void variant_str(DBusIter *it, const char *s)
{
    DBusIter v;
    app_variant_begin(it, "s", &v);
    add_str(&v, s);
    D.close_container(it, &v);
}
static void variant_i64(DBusIter *it, int64_t x)
{
    DBusIter v;
    app_variant_begin(it, "x", &v);
    D.append_basic(&v, T_INT64, &x);
    D.close_container(it, &v);
}

static void entry_begin(DBusIter *dict, const char *key, DBusIter *e)
{
    D.open_container(dict, T_DICT, NULL, e);
    add_str(e, key);
}

static void metadata_value(DBusIter *it)
{
    DBusIter v, arr, e;
    app_variant_begin(it, "a{sv}", &v);
    D.open_container(&v, T_ARRAY, "{sv}", &arr);
    bool has = np.claimed && np.title[0];
    entry_begin(&arr, "mpris:trackid", &e);
    {
        DBusIter vv;
        const char *p = has ? "/org/thc/track/current" : "/org/mpris/MediaPlayer2/TrackList/NoTrack";
        app_variant_begin(&e, "o", &vv);
        D.append_basic(&vv, T_PATH, &p);
        D.close_container(&e, &vv);
    }
    D.close_container(&arr, &e);
    if (has) {
        if (np.length > 0) {
            entry_begin(&arr, "mpris:length", &e);
            variant_i64(&e, (int64_t)(np.length * 1e6));
            D.close_container(&arr, &e);
        }
        entry_begin(&arr, "xesam:title", &e);
        variant_str(&e, np.title);
        D.close_container(&arr, &e);
        if (np.artist[0]) {
            DBusIter vv, a2;
            entry_begin(&arr, "xesam:artist", &e);
            app_variant_begin(&e, "as", &vv);
            D.open_container(&vv, T_ARRAY, "s", &a2);
            add_str(&a2, np.artist);
            D.close_container(&vv, &a2);
            D.close_container(&e, &vv);
            D.close_container(&arr, &e);
        }
        if (np.album[0]) {
            entry_begin(&arr, "xesam:album", &e);
            variant_str(&e, np.album);
            D.close_container(&arr, &e);
        }
        if (np.art[0] && np.art[0] == '/') {
            char url[1100];
            snprintf(url, sizeof url, "file://%s", np.art);
            entry_begin(&arr, "mpris:artUrl", &e);
            variant_str(&e, url);
            D.close_container(&arr, &e);
        }
    }
    D.close_container(&v, &arr);
    D.close_container(it, &v);
}

static const char *status(void)
{
    if (!np.claimed) return "Stopped";
    return np.playing ? "Playing" : "Paused";
}

/* Append one property (as a variant) by name; false if unknown. */
static bool property(const char *iface, const char *name, DBusIter *it)
{
    if (!strcmp(iface, IF_ROOT)) {
        if (!strcmp(name, "CanQuit") || !strcmp(name, "CanRaise") || !strcmp(name, "HasTrackList") ||
            !strcmp(name, "CanSetFullscreen") || !strcmp(name, "Fullscreen")) return variant_bool(it, false), true;
        if (!strcmp(name, "Identity")) return variant_str(it, "THC"), true;
        if (!strcmp(name, "DesktopEntry")) return variant_str(it, "thc"), true;
        if (!strcmp(name, "SupportedUriSchemes") || !strcmp(name, "SupportedMimeTypes")) {
            DBusIter v, a;
            app_variant_begin(it, "as", &v);
            D.open_container(&v, T_ARRAY, "s", &a);
            D.close_container(&v, &a);
            D.close_container(it, &v);
            return true;
        }
        return false;
    }
    if (!strcmp(iface, IF_PLAYER)) {
        if (!strcmp(name, "PlaybackStatus")) return variant_str(it, status()), true;
        if (!strcmp(name, "LoopStatus")) return variant_str(it, "None"), true;
        if (!strcmp(name, "Rate") || !strcmp(name, "MinimumRate") || !strcmp(name, "MaximumRate") ||
            !strcmp(name, "Volume")) return variant_double(it, 1.0), true;
        if (!strcmp(name, "Shuffle")) return variant_bool(it, false), true;
        if (!strcmp(name, "Metadata")) return metadata_value(it), true;
        if (!strcmp(name, "Position")) return variant_i64(it, (int64_t)(np.position * 1e6)), true;
        if (!strcmp(name, "CanGoNext") || !strcmp(name, "CanGoPrevious") || !strcmp(name, "CanPlay") ||
            !strcmp(name, "CanPause") || !strcmp(name, "CanSeek") || !strcmp(name, "CanControl"))
            return variant_bool(it, np.claimed), true;
    }
    return false;
}

static const char *const k_root_props[] = { "CanQuit", "CanRaise", "HasTrackList", "Identity", "DesktopEntry",
                                            "SupportedUriSchemes", "SupportedMimeTypes", NULL };
static const char *const k_player_props[] = { "PlaybackStatus", "LoopStatus", "Rate", "Shuffle", "Metadata", "Volume",
                                              "Position", "MinimumRate", "MaximumRate", "CanGoNext", "CanGoPrevious",
                                              "CanPlay", "CanPause", "CanSeek", "CanControl", NULL };

static void props_dict(DBusIter *parent, const char *iface, const char *const *names)
{
    DBusIter arr, e;
    D.open_container(parent, T_ARRAY, "{sv}", &arr);
    for (int i = 0; names[i]; i++) {
        entry_begin(&arr, names[i], &e);
        property(iface, names[i], &e);
        D.close_container(&arr, &e);
    }
    D.close_container(parent, &arr);
}

static void reply_error(DBusMessage *m, const char *name, const char *text)
{
    DBusMessage *r = D.new_error(m, name, text);
    if (r) { D.send(conn, r, NULL); D.msg_unref(r); }
}

static const char *const k_introspect =
    "<node><interface name=\"" IF_ROOT "\"><method name=\"Raise\"/><method name=\"Quit\"/>"
    "<property name=\"CanQuit\" type=\"b\" access=\"read\"/><property name=\"CanRaise\" type=\"b\" access=\"read\"/>"
    "<property name=\"HasTrackList\" type=\"b\" access=\"read\"/><property name=\"Identity\" type=\"s\" access=\"read\"/>"
    "</interface><interface name=\"" IF_PLAYER "\"><method name=\"Next\"/><method name=\"Previous\"/>"
    "<method name=\"Pause\"/><method name=\"PlayPause\"/><method name=\"Stop\"/><method name=\"Play\"/>"
    "<method name=\"Seek\"><arg type=\"x\" direction=\"in\"/></method>"
    "<method name=\"SetPosition\"><arg type=\"o\" direction=\"in\"/><arg type=\"x\" direction=\"in\"/></method>"
    "<method name=\"OpenUri\"><arg type=\"s\" direction=\"in\"/></method>"
    "<property name=\"PlaybackStatus\" type=\"s\" access=\"read\"/><property name=\"Metadata\" type=\"a{sv}\" access=\"read\"/>"
    "<property name=\"Position\" type=\"x\" access=\"read\"/><property name=\"CanControl\" type=\"b\" access=\"read\"/>"
    "</interface></node>";

static void handle(DBusMessage *m)
{
    if (D.get_type(m) != 1) return;   /* method calls only */
    const char *iface = D.get_iface(m), *member = D.get_member(m);
    if (!member) return;
    if (!iface) iface = "";
    DBusMessage *r = NULL;

    if (!strcmp(iface, "org.freedesktop.DBus.Introspectable") && !strcmp(member, "Introspect")) {
        r = D.new_return(m);
        DBusIter it;
        D.iter_init_append(r, &it);
        add_str(&it, k_introspect);
    } else if (!strcmp(iface, IF_PROPS)) {
        DBusIter in;
        const char *i_name = "", *p_name = "";
        if (D.iter_init(m, &in) && D.arg_type(&in) == T_STRING) {
            D.get_basic(&in, &i_name);
            if (D.next(&in) && D.arg_type(&in) == T_STRING) D.get_basic(&in, &p_name);
        }
        if (!strcmp(member, "Get")) {
            r = D.new_return(m);
            DBusIter it;
            D.iter_init_append(r, &it);
            if (!property(i_name, p_name, &it)) {
                D.msg_unref(r);
                reply_error(m, "org.freedesktop.DBus.Error.UnknownProperty", "no such property");
                return;
            }
        } else if (!strcmp(member, "GetAll")) {
            r = D.new_return(m);
            DBusIter it;
            D.iter_init_append(r, &it);
            if (!strcmp(i_name, IF_ROOT)) props_dict(&it, IF_ROOT, k_root_props);
            else if (!strcmp(i_name, IF_PLAYER)) props_dict(&it, IF_PLAYER, k_player_props);
            else {
                DBusIter arr;
                D.open_container(&it, T_ARRAY, "{sv}", &arr);
                D.close_container(&it, &arr);
            }
        } else if (!strcmp(member, "Set")) {
            r = D.new_return(m);          /* nothing is writable; accept and ignore */
        }
    } else if (!strcmp(iface, IF_ROOT)) {
        if (!strcmp(member, "Raise") || !strcmp(member, "Quit")) r = D.new_return(m);
    } else if (!strcmp(iface, IF_PLAYER)) {
        if (!np.claimed) {
            reply_error(m, "org.mpris.MediaPlayer2.Error", "nothing is playing");
            return;
        }
        int action = 0;
        double arg = 0;
        if (!strcmp(member, "Next")) action = MKA_NEXT;
        else if (!strcmp(member, "Previous")) action = MKA_PREV;
        else if (!strcmp(member, "Pause")) action = MKA_PAUSE;
        else if (!strcmp(member, "PlayPause")) action = MKA_TOGGLE;
        else if (!strcmp(member, "Play")) action = MKA_PLAY;
        else if (!strcmp(member, "Stop")) action = MKA_STOP;
        else if (!strcmp(member, "Seek") || !strcmp(member, "SetPosition")) {
            DBusIter in;
            if (D.iter_init(m, &in)) {
                if (!strcmp(member, "SetPosition") && D.arg_type(&in) == T_PATH) D.next(&in);
                if (D.arg_type(&in) == T_INT64) {
                    int64_t us = 0;
                    D.get_basic(&in, &us);
                    arg = (double)us / 1e6;
                    action = !strcmp(member, "Seek") ? MKA_SEEK_REL : MKA_SEEK_ABS;
                }
            }
        } else if (!strcmp(member, "OpenUri")) r = D.new_return(m);   /* THC does not open URIs */
        if (action && act_fn) {
            act_fn(action, arg, act_ud);
            r = D.new_return(m);
        }
    }
    if (r) {
        D.send(conn, r, NULL);
        D.msg_unref(r);
    } else {
        reply_error(m, "org.freedesktop.DBus.Error.UnknownMethod", "unknown method");
    }
}

/* ---- backend ------------------------------------------------------------ */

static bool mk_init(MediaActionFn fn, void *ud)
{
    if (!load_dbus()) return false;
    act_fn = fn;
    act_ud = ud;
    DBusErr err;
    D.err_init(&err);
    conn = D.bus_get(0 /* session */, &err);
    if (!conn) {
        D.err_free(&err);
        return false;
    }
    D.exit_on_disconnect(conn, 0);
    D.err_free(&err);
    return true;
}

static void own(bool want)
{
    if (!conn || want == owns_name) return;
    DBusErr err;
    D.err_init(&err);
    if (want) {
        /* DBUS_NAME_FLAG_DO_NOT_QUEUE = 4: if another THC has it, we just do not claim */
        int rc = D.request_name(conn, BUS_NAME, 4, &err);
        owns_name = rc == 1 || rc == 4;   /* primary owner / already owner */
    } else {
        D.release_name(conn, BUS_NAME, &err);
        owns_name = false;
    }
    D.err_free(&err);
}

static void emit_changed(const NowPlaying *old)
{
    if (!owns_name) return;
    DBusMessage *s = D.new_signal(OBJ_PATH, IF_PROPS, "PropertiesChanged");
    if (!s) return;
    DBusIter it, arr, e;
    D.iter_init_append(s, &it);
    add_str(&it, IF_PLAYER);
    D.open_container(&it, T_ARRAY, "{sv}", &arr);
    entry_begin(&arr, "PlaybackStatus", &e);
    variant_str(&e, status());
    D.close_container(&arr, &e);
    entry_begin(&arr, "Metadata", &e);
    metadata_value(&e);
    D.close_container(&arr, &e);
    const char *flags[] = { "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause", "CanSeek", "CanControl" };
    if (old->claimed != np.claimed)
        for (int i = 0; i < 6; i++) {
            entry_begin(&arr, flags[i], &e);
            variant_bool(&e, np.claimed);
            D.close_container(&arr, &e);
        }
    D.close_container(&it, &arr);
    D.open_container(&it, T_ARRAY, "s", &arr);     /* no invalidated properties */
    D.close_container(&it, &arr);
    D.send(conn, s, NULL);
    D.msg_unref(s);
    D.flush(conn);
}

static void mk_update(const NowPlaying *n)
{
    NowPlaying old = np;
    np = *n;
    own(np.claimed);                   /* claim the bus name only while something is loaded */
    bool changed = old.claimed != np.claimed || old.playing != np.playing || strcmp(old.title, np.title) ||
                   strcmp(old.artist, np.artist) || strcmp(old.album, np.album) || strcmp(old.art, np.art) ||
                   (int)old.length != (int)np.length;
    if (changed) emit_changed(&old);
}

static void mk_pump(void)
{
    if (!conn) return;
    D.read_write(conn, 0);
    DBusMessage *m;
    while ((m = D.pop(conn)) != NULL) {
        if (owns_name) handle(m);
        D.msg_unref(m);
    }
}

static void mk_shutdown(void)
{
    if (conn) {
        own(false);
        D.conn_unref(conn);
        conn = NULL;
    }
}

static const MediaBackend k_backend = { "mpris", mk_init, mk_update, mk_pump, mk_shutdown };

const MediaBackend *mediakeys_linux_backend(void) { return &k_backend; }

#else

const MediaBackend *mediakeys_linux_backend(void) { return NULL; }

#endif
