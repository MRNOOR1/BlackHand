#include "avrcp_service.h"
#include "mp3_service.h"
#include "voice_memo_service.h"

#include <dbus/dbus.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PLAYER_PATH  "/org/blackhand/MediaPlayer"
#define PLAYER_IFACE "org.mpris.MediaPlayer2.Player"
#define PROPS_IFACE  "org.freedesktop.DBus.Properties"
#define INTRO_IFACE  "org.freedesktop.DBus.Introspectable"
#define BLUEZ_DEST   "org.bluez"
#define ADAPTER_PATH "/org/bluez/hci0"
#define BLUEZ_MEDIA1 "org.bluez.Media1"

static pthread_t     s_thread;
static int           s_thread_created = 0;
static volatile int  s_running        = 0;

/* ── Introspection ─────────────────────────────────────────────────────── */

static const char INTRO_XML[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\""
    " \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.mpris.MediaPlayer2.Player\">\n"
    "    <method name=\"Play\"/>\n"
    "    <method name=\"Pause\"/>\n"
    "    <method name=\"Stop\"/>\n"
    "    <method name=\"Next\"/>\n"
    "    <method name=\"Previous\"/>\n"
    "    <method name=\"FastForward\"/>\n"
    "    <method name=\"Rewind\"/>\n"
    "    <property name=\"PlaybackStatus\" type=\"s\"     access=\"read\"/>\n"
    "    <property name=\"Position\"       type=\"x\"     access=\"read\"/>\n"
    "    <property name=\"Metadata\"       type=\"a{sv}\" access=\"read\"/>\n"
    "    <property name=\"Shuffle\"        type=\"b\"     access=\"readwrite\"/>\n"
    "    <property name=\"LoopStatus\"     type=\"s\"     access=\"readwrite\"/>\n"
    "    <property name=\"Identity\"       type=\"s\"     access=\"read\"/>\n"
    "    <property name=\"CanPlay\"        type=\"b\"     access=\"read\"/>\n"
    "    <property name=\"CanPause\"       type=\"b\"     access=\"read\"/>\n"
    "    <property name=\"CanGoNext\"      type=\"b\"     access=\"read\"/>\n"
    "    <property name=\"CanGoPrevious\"  type=\"b\"     access=\"read\"/>\n"
    "    <property name=\"CanControl\"     type=\"b\"     access=\"read\"/>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "    <method name=\"Introspect\">\n"
    "      <arg name=\"xml\" direction=\"out\" type=\"s\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "    <method name=\"Get\">\n"
    "      <arg name=\"interface\" direction=\"in\"  type=\"s\"/>\n"
    "      <arg name=\"property\"  direction=\"in\"  type=\"s\"/>\n"
    "      <arg name=\"value\"     direction=\"out\" type=\"v\"/>\n"
    "    </method>\n"
    "    <method name=\"GetAll\">\n"
    "      <arg name=\"interface\" direction=\"in\"  type=\"s\"/>\n"
    "      <arg name=\"props\"     direction=\"out\" type=\"a{sv}\"/>\n"
    "    </method>\n"
    "    <method name=\"Set\">\n"
    "      <arg name=\"interface\" direction=\"in\" type=\"s\"/>\n"
    "      <arg name=\"property\"  direction=\"in\" type=\"s\"/>\n"
    "      <arg name=\"value\"     direction=\"in\" type=\"v\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "</node>\n";

/* ── Property helpers ──────────────────────────────────────────────────── */

static const char *player_status(void)
{
    switch (voice_memo_service_state()) {
    case VM_PLAYING: return "playing";
    case VM_PAUSED:  return "paused";
    default: break;
    }
    switch (mp3_service_get_state()) {
    case MP3_PLAYING: return "playing";
    case MP3_PAUSED:  return "paused";
    default:          return "stopped";
    }
}

/* Append variant(string) into *iter */
static void append_string_variant(DBusMessageIter *iter, const char *val)
{
    DBusMessageIter var;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(iter, &var);
}

/* Append variant(int64) into *iter */
static void append_int64_variant(DBusMessageIter *iter, dbus_int64_t val)
{
    DBusMessageIter var;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "x", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_INT64, &val);
    dbus_message_iter_close_container(iter, &var);
}

static void append_bool_variant(DBusMessageIter *iter, dbus_bool_t val)
{
    DBusMessageIter var;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(iter, &var);
}

/* Append a complete {s, variant(s)} dict entry into an a{sv} array iter */
static void append_dict_str(DBusMessageIter *arr, const char *key, const char *val)
{
    DBusMessageIter e;
    dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    append_string_variant(&e, val);
    dbus_message_iter_close_container(arr, &e);
}

/* Append a complete {s, variant(x)} dict entry into an a{sv} array iter */
static void append_dict_i64(DBusMessageIter *arr, const char *key, dbus_int64_t val)
{
    DBusMessageIter e;
    dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    append_int64_variant(&e, val);
    dbus_message_iter_close_container(arr, &e);
}

static void append_dict_bool(DBusMessageIter *arr, const char *key, dbus_bool_t val)
{
    DBusMessageIter e;
    dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    append_bool_variant(&e, val);
    dbus_message_iter_close_container(arr, &e);
}

static void append_status_variant(DBusMessageIter *iter)
{
    const char *s = player_status();
    const char *mpris = "Stopped";
    if (strcmp(s, "playing") == 0) mpris = "Playing";
    else if (strcmp(s, "paused") == 0) mpris = "Paused";
    append_string_variant(iter, mpris);
}

static void append_position_variant(DBusMessageIter *iter)
{
    dbus_int64_t pos_us = (dbus_int64_t)mp3_service_get_elapsed() * 1000000;
    append_int64_variant(iter, pos_us);
}

/* Append variant(a{sv}) MPRIS metadata dict into *iter.
 * All string pointers are NULL-guarded — this function is safe to call
 * regardless of playback state. */
static void append_track_variant(DBusMessageIter *iter)
{
    const char *title  = mp3_service_current_track_name();
    const char *artist = mp3_service_current_collection_name();
    const char *album  = mp3_service_current_category_name();
    dbus_int64_t dur_us = (dbus_int64_t)mp3_service_get_total_duration() * 1000000;

    if (!title)  title  = "";
    if (!artist) artist = "";
    if (!album)  album  = "";

    DBusMessageIter var, arr;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a{sv}", &var);
    dbus_message_iter_open_container(&var,  DBUS_TYPE_ARRAY,  "{sv}",  &arr);

    append_dict_str(&arr, "xesam:title", title);
    append_dict_str(&arr, "xesam:album", album);

    {
        DBusMessageIter e, var, artists;
        const char *k = "xesam:artist";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "as", &var);
        dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &artists);
        dbus_message_iter_append_basic(&artists, DBUS_TYPE_STRING, &artist);
        dbus_message_iter_close_container(&var, &artists);
        dbus_message_iter_close_container(&e, &var);
        dbus_message_iter_close_container(&arr, &e);
    }

    append_dict_i64(&arr, "mpris:length", dur_us);

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(iter, &var);
}

/* ── Properties.GetAll ─────────────────────────────────────────────────── */

static DBusMessage *handle_get_all(DBusMessage *msg)
{
    const char *iface = NULL;
    if (!dbus_message_get_args(msg, NULL,
            DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "bad args");
    if (strcmp(iface, PLAYER_IFACE) != 0)
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, iface);

    DBusMessage    *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr);

    append_dict_str(&arr,  "Identity",       "BlackHand");
    append_dict_bool(&arr, "CanPlay",        1);
    append_dict_bool(&arr, "CanPause",       1);
    append_dict_bool(&arr, "CanGoNext",      1);
    append_dict_bool(&arr, "CanGoPrevious",  1);
    append_dict_bool(&arr, "CanControl",     1);
    append_dict_bool(&arr, "Shuffle",        0);
    append_dict_str(&arr,  "LoopStatus",     "None");

    {
        DBusMessageIter e;
        const char *k = "PlaybackStatus";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k);
        append_status_variant(&e);
        dbus_message_iter_close_container(&arr, &e);
    }
    {
        DBusMessageIter e;
        const char *k = "Position";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k);
        append_position_variant(&e);
        dbus_message_iter_close_container(&arr, &e);
    }
    {
        DBusMessageIter e;
        const char *k = "Metadata";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k);
        append_track_variant(&e);
        dbus_message_iter_close_container(&arr, &e);
    }

    dbus_message_iter_close_container(&iter, &arr);
    return reply;
}

/* ── Properties.Get ────────────────────────────────────────────────────── */

static DBusMessage *handle_get(DBusMessage *msg)
{
    const char *iface = NULL, *prop = NULL;
    if (!dbus_message_get_args(msg, NULL,
            DBUS_TYPE_STRING, &iface,
            DBUS_TYPE_STRING, &prop,
            DBUS_TYPE_INVALID))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "bad args");
    if (strcmp(iface, PLAYER_IFACE) != 0)
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, iface);

    DBusMessage    *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    if      (strcmp(prop, "PlaybackStatus") == 0) append_status_variant(&iter);
    else if (strcmp(prop, "Position")       == 0) append_position_variant(&iter);
    else if (strcmp(prop, "Metadata")       == 0) append_track_variant(&iter);
    else if (strcmp(prop, "Shuffle")        == 0) append_bool_variant(&iter, 0);
    else if (strcmp(prop, "LoopStatus")     == 0) append_string_variant(&iter, "None");
    else if (strcmp(prop, "Identity")       == 0) append_string_variant(&iter, "BlackHand");
    else if (strcmp(prop, "CanPlay")        == 0) append_bool_variant(&iter, 1);
    else if (strcmp(prop, "CanPause")       == 0) append_bool_variant(&iter, 1);
    else if (strcmp(prop, "CanGoNext")      == 0) append_bool_variant(&iter, 1);
    else if (strcmp(prop, "CanGoPrevious")  == 0) append_bool_variant(&iter, 1);
    else if (strcmp(prop, "CanControl")     == 0) append_bool_variant(&iter, 1);
    else {
        dbus_message_unref(reply);
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, prop);
    }
    return reply;
}

static DBusMessage *handle_set(DBusMessage *msg)
{
    const char *iface = NULL, *prop = NULL;
    DBusMessageIter iter, var;

    if (!dbus_message_iter_init(msg, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "bad args");
    dbus_message_iter_get_basic(&iter, &iface);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "bad args");
    dbus_message_iter_get_basic(&iter, &prop);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "bad args");
    dbus_message_iter_recurse(&iter, &var);

    if (strcmp(iface, PLAYER_IFACE) != 0)
        return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, iface);

    /* BlueZ maps AVRCP repeat/shuffle changes to MPRIS Set calls. BlackHand
     * currently has no playlist shuffle/repeat state, so accept and ignore. */
    if ((strcmp(prop, "Shuffle") == 0 &&
         dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BOOLEAN) ||
        (strcmp(prop, "LoopStatus") == 0 &&
         dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING))
        return dbus_message_new_method_return(msg);

    return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, prop);
}

/* ── Object message handler (dispatch thread) ──────────────────────────── */

static DBusHandlerResult message_handler(DBusConnection *conn,
                                          DBusMessage    *msg,
                                          void           *data)
{
    (void)data;
    const char *iface  = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    DBusMessage *reply = NULL;

    if (iface && strcmp(iface, INTRO_IFACE) == 0
        && member && strcmp(member, "Introspect") == 0) {
        reply = dbus_message_new_method_return(msg);
        const char *xml = INTRO_XML;
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    }
    else if (iface && strcmp(iface, PROPS_IFACE) == 0 && member) {
        if      (strcmp(member, "GetAll") == 0) reply = handle_get_all(msg);
        else if (strcmp(member, "Get")    == 0) reply = handle_get(msg);
        else if (strcmp(member, "Set")    == 0) reply = handle_set(msg);
    }
    else if (iface && strcmp(iface, PLAYER_IFACE) == 0 && member) {
        VMState vm = voice_memo_service_state();
        if (strcmp(member, "Play") == 0) {
            if      (vm == VM_PAUSED)                       voice_memo_service_play_resume();
            else if (mp3_service_get_state() == MP3_PAUSED) mp3_service_resume();
        }
        else if (strcmp(member, "Pause") == 0) {
            if (vm == VM_PLAYING) voice_memo_service_play_pause();
            else                  mp3_service_pause();
        }
        else if (strcmp(member, "Stop") == 0) {
            if (vm == VM_PLAYING || vm == VM_PAUSED) voice_memo_service_play_stop();
            else                                     mp3_service_stop();
        }
        else if (strcmp(member, "Next")     == 0) mp3_service_next();
        else if (strcmp(member, "Previous") == 0) mp3_service_prev();
        reply = dbus_message_new_method_return(msg);
    }

    if (!reply && dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL)
        reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, member);

    if (reply) {
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable s_vtable = {
    .message_function = message_handler,
};

/* ── PropertiesChanged signal ──────────────────────────────────────────── */

static void emit_properties_changed(DBusConnection *conn)
{
    DBusMessage *sig = dbus_message_new_signal(PLAYER_PATH, PROPS_IFACE,
                                               "PropertiesChanged");
    if (!sig) return;

    DBusMessageIter args, arr;
    const char *iface = PLAYER_IFACE;
    dbus_message_iter_init_append(sig, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface);

    /* Changed properties: playback status + metadata. */
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &arr);
    {
        DBusMessageIter e;
        const char *k = "PlaybackStatus";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k);
        append_status_variant(&e);
        dbus_message_iter_close_container(&arr, &e);
    }
    {
        DBusMessageIter e;
        const char *k = "Metadata";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k);
        append_track_variant(&e);
        dbus_message_iter_close_container(&arr, &e);
    }
    dbus_message_iter_close_container(&args, &arr);

    /* Empty invalidated-properties array */
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_close_container(&args, &arr);

    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
}

/* ── Register with BlueZ ───────────────────────────────────────────────── */

static int register_player(DBusConnection *conn)
{
    const char *path = PLAYER_PATH;
    DBusMessage *msg = dbus_message_new_method_call(
        BLUEZ_DEST, ADAPTER_PATH, BLUEZ_MEDIA1, "RegisterPlayer");
    if (!msg) return 0;

    /* BlueZ RegisterPlayer initial dict.
     *
     * Current BlueZ RegisterPlayer consumes MPRIS Player properties. Supplying
     * the older org.bluez.MediaPlayer1 property names makes bluetoothd reject
     * registration with org.bluez.Error.InvalidArguments on modern BlueZ. */
    DBusMessageIter args, arr;
    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &arr);
    append_dict_str(&arr,  "Identity",       "BlackHand");
    append_dict_str(&arr,  "PlaybackStatus", "Stopped");
    append_dict_bool(&arr, "CanPlay",        1);
    append_dict_bool(&arr, "CanPause",       1);
    append_dict_bool(&arr, "CanGoNext",      1);
    append_dict_bool(&arr, "CanGoPrevious",  1);
    append_dict_bool(&arr, "CanControl",     1);
    append_dict_bool(&arr, "Shuffle",        0);
    append_dict_str(&arr,  "LoopStatus",     "None");
    {
        DBusMessageIter e;
        const char *k = "Metadata";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k);
        append_track_variant(&e);
        dbus_message_iter_close_container(&arr, &e);
    }
    dbus_message_iter_close_container(&args, &arr);

    /* Use non-blocking send so the dispatch loop keeps running while we wait.
     *
     * WHY: Modern BlueZ (5.65+) calls back synchronously to introspect our
     * player object inside media_player_create() during the RegisterPlayer
     * handling.  If we use dbus_connection_send_with_reply_and_block() here,
     * this thread is frozen and can't call dbus_connection_read_write_dispatch(),
     * so the incoming Introspect request from BlueZ sits in the queue unread.
     * BlueZ's synchronous introspect call times out → media_player_create()
     * returns NULL → RegisterPlayer returns InvalidArguments.
     *
     * By using an async pending call and pumping dispatch while waiting, we
     * allow BlueZ's Introspect to be answered mid-flight, breaking the deadlock. */
    DBusPendingCall *pending = NULL;
    if (!dbus_connection_send_with_reply(conn, msg, &pending, 5000)) {
        dbus_message_unref(msg);
        return 0;
    }
    dbus_message_unref(msg);
    if (!pending) return 0;

    /* Pump the connection so incoming method calls (Introspect, Get, GetAll)
     * from BlueZ are dispatched while we wait for the RegisterPlayer reply. */
    while (!dbus_pending_call_get_completed(pending))
        dbus_connection_read_write_dispatch(conn, 50);

    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    if (!reply) return 0;

    int ok = 0;
    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        fprintf(stderr, "avrcp: MediaPlayer registered at %s\n", PLAYER_PATH);
        ok = 1;
    } else {
        DBusError err;
        dbus_error_init(&err);
        dbus_set_error_from_message(&err, reply);
        fprintf(stderr, "avrcp: RegisterPlayer failed: %s: %s\n",
                err.name, err.message);
        dbus_error_free(&err);
    }
    dbus_message_unref(reply);
    return ok;
}

/* ── Dispatch thread — all D-Bus work runs here ────────────────────────── */

static void *dispatch_thread(void *arg)
{
    (void)arg;

    DBusError err;
    dbus_error_init(&err);
    /* Private connection so this thread owns dispatch exclusively */
    DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "avrcp: D-Bus connect failed: %s\n", err.message);
        dbus_error_free(&err);
        s_running = 0;
        return NULL;
    }
    if (!conn) { s_running = 0; return NULL; }

    dbus_connection_set_exit_on_disconnect(conn, 0);

    if (!dbus_connection_register_object_path(conn, PLAYER_PATH, &s_vtable, NULL)) {
        fprintf(stderr, "avrcp: register_object_path failed\n");
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        s_running = 0;
        return NULL;
    }

    /* Initial registration: up to 5 attempts, 2 s apart.
     * bluetoothd may not have finished loading the audio plugin yet.
     * Use dispatch-pumped sleep so incoming messages are handled even between
     * attempts — a second introspect might arrive during the wait. */
    int registered = 0;
    for (int attempt = 0; attempt < 5 && s_running; attempt++) {
        if (attempt > 0) {
            /* 2-second wait using 50 ms dispatch slices instead of sleep(2).
             * This keeps the connection alive for any arriving messages. */
            for (int w = 0; w < 40 && s_running; w++)
                dbus_connection_read_write_dispatch(conn, 50);
        }
        if (register_player(conn)) { registered = 1; break; }
    }

    /* Track previous state to detect changes that need PropertiesChanged */
    char last_status[32]  = "stopped";
    char last_track[256]  = "";
    int  retry_ticks      = 0;

    while (s_running && dbus_connection_get_is_connected(conn)) {
        dbus_connection_read_write_dispatch(conn, 200);

        /* If not yet registered, retry every ~10 s (50 × 200 ms ticks).
         * Covers the case where bluetoothd restarted after initial boot. */
        if (!registered) {
            if (++retry_ticks >= 50) {
                retry_ticks = 0;
                if (register_player(conn)) registered = 1;
            }
            continue;
        }

        /* Emit PropertiesChanged when playback status or track title changes. */
        const char *cur_status = player_status();
        const char *cur_track  = mp3_service_current_track_name();
        if (!cur_track) cur_track = "";

        if (strcmp(cur_status, last_status) != 0 ||
            strcmp(cur_track,  last_track)  != 0) {
            strncpy(last_status, cur_status, sizeof(last_status) - 1);
            strncpy(last_track,  cur_track,  sizeof(last_track)  - 1);
            last_status[sizeof(last_status) - 1] = '\0';
            last_track[sizeof(last_track)   - 1] = '\0';
            emit_properties_changed(conn);
        }
    }

    /* Unregister cleanly */
    const char *path = PLAYER_PATH;
    DBusMessage *unreg = dbus_message_new_method_call(
        BLUEZ_DEST, ADAPTER_PATH, BLUEZ_MEDIA1, "UnregisterPlayer");
    if (unreg) {
        dbus_message_append_args(unreg,
            DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, unreg, NULL);
        dbus_connection_flush(conn);
        dbus_message_unref(unreg);
    }

    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return NULL;
}

/* ── Public API ────────────────────────────────────────────────────────── */

void avrcp_service_init(void)
{
    dbus_threads_init_default();
    s_running        = 1;
    s_thread_created = 0;
    if (pthread_create(&s_thread, NULL, dispatch_thread, NULL) == 0)
        s_thread_created = 1;
    else
        s_running = 0;
}

void avrcp_service_shutdown(void)
{
    s_running = 0;
    if (s_thread_created) {
        pthread_join(s_thread, NULL);
        s_thread_created = 0;
    }
}
