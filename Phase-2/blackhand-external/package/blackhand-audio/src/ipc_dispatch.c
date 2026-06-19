/*
 * ipc_dispatch.c — blackhand-audio service dispatch table.
 *
 * JSON-RPC methods exposed by this service:
 *   ping            — returns "pong"
 *   echo            — returns a test string
 *   play_start      — start playing a WAV file ({"file": "..."})
 *   play_pause      — pause current playback (real pause, byte-offset held)
 *   play_resume     — resume from where pause held
 *   play_stop       — stop playback entirely
 *   play_seek       — seek to absolute ms ({"position_ms": int})
 *   record_start    — start recording ({"file": "..."})
 *   record_pause    — pause capture
 *   record_resume   — resume capture
 *   record_stop     — stop recording and finalise the WAV header
 *   volume          — set master volume ({"volume": 0–100})
 *   status          — returns
 *     { "playing": bool, "paused": bool, "recording": bool,
 *       "elapsed_ms": int, "volume": int }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include "ipc_dispatch.h"
#include "ipc_framing.h"
#include "cJSON.h"
#include "audio_alsa.h"

/* Mirror of audio_alsa.c's dlog. Defined separately here so dispatch can log
   every incoming IPC call to /boot/logs/audio.log without needing to expose
   the static log handle. Different FILE* than the engine — they're both
   appending, and FAT32 writes are line-buffered with fflush, so interleaving
   is fine. */
static FILE *g_dispatch_log = NULL;
static pthread_mutex_t g_dispatch_lock = PTHREAD_MUTEX_INITIALIZER;
static void ipclog(const char *line) {
    pthread_mutex_lock(&g_dispatch_lock);
    if (!g_dispatch_log) {
        mkdir("/boot/logs", 0755);
        g_dispatch_log = fopen("/boot/logs/audio.log", "a");
    }
    if (g_dispatch_log) {
        time_t now = time(NULL);
        struct tm tmv; localtime_r(&now, &tmv);
        char ts[32]; strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
        fprintf(g_dispatch_log, "[%s] IPC %s\n", ts, line);
        fflush(g_dispatch_log);
    }
    pthread_mutex_unlock(&g_dispatch_lock);
}

static void handle_ping(int fd, cJSON *req);
static void handle_echo(int fd, cJSON *req);
static void handle_play_start(int fd, cJSON *req);
static void handle_play_pause(int fd, cJSON *req);
static void handle_play_resume(int fd, cJSON *req);
static void handle_play_stop(int fd, cJSON *req);
static void handle_play_seek(int fd, cJSON *req);
static void handle_record_start(int fd, cJSON *req);
static void handle_record_pause(int fd, cJSON *req);
static void handle_record_resume(int fd, cJSON *req);
static void handle_record_stop(int fd, cJSON *req);
static void handle_volume(int fd, cJSON *req);
static void handle_status(int fd, cJSON *req);

struct handler {
    const char *method;
    void (*fn)(int, cJSON *);
};

static struct handler table[] = {
    {"ping",          handle_ping},
    {"echo",          handle_echo},
    {"play_start",    handle_play_start},
    {"play_pause",    handle_play_pause},
    {"play_resume",   handle_play_resume},
    {"play_stop",     handle_play_stop},
    {"play_seek",     handle_play_seek},
    {"record_start",  handle_record_start},
    {"record_pause",  handle_record_pause},
    {"record_resume", handle_record_resume},
    {"record_stop",   handle_record_stop},
    {"volume",        handle_volume},
    {"status",        handle_status},
    {NULL, NULL}
};

static void send_result(int fd, cJSON *req, const char *result_str)
{
    cJSON *id_item = cJSON_GetObjectItem(req, "id");
    if (!id_item || !cJSON_IsNumber(id_item)) {
        send_error(fd, "invalid id");
        return;
    }
    int id = id_item->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", result_str);
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    if (str) { send_msg(fd, str); free(str); }
    cJSON_Delete(res);
}

static void send_result_json(int fd, cJSON *req, cJSON *result_obj)
{
    cJSON *id_item = cJSON_GetObjectItem(req, "id");
    if (!id_item || !cJSON_IsNumber(id_item)) {
        send_error(fd, "invalid id");
        return;
    }
    int id = id_item->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddItemToObject(res, "result", result_obj);
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    if (str) { send_msg(fd, str); free(str); }
    cJSON_Delete(res);
}

void dispatch(int fd, cJSON *req)
{
    cJSON *version_item = cJSON_GetObjectItem(req, "jsonrpc");
    if (!version_item || !cJSON_IsString(version_item) ||
        strcmp(version_item->valuestring, "2.0") != 0) {
        send_error(fd, "invalid or missing jsonrpc version");
        return;
    }

    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (!id || !cJSON_IsNumber(id)) {
        send_error(fd, "missing or invalid id");
        return;
    }

    cJSON *method_item = cJSON_GetObjectItem(req, "method");
    if (!method_item || !cJSON_IsString(method_item)) {
        send_error(fd, "missing method");
        return;
    }

    const char *method = method_item->valuestring;

    /* Log every method except status (which fires at 30 Hz from the UI tick
       and would drown out everything else). For methods with a string
       param, include it so the log line is meaningful on its own. */
    if (strcmp(method, "status") != 0) {
        cJSON *params = cJSON_GetObjectItem(req, "params");
        cJSON *file_item = params ? cJSON_GetObjectItem(params, "file") : NULL;
        cJSON *pos_item  = params ? cJSON_GetObjectItem(params, "position_ms") : NULL;
        cJSON *vol_item  = params ? cJSON_GetObjectItem(params, "volume") : NULL;
        char line[256];
        if (file_item && cJSON_IsString(file_item)) {
            snprintf(line, sizeof(line), "%s file=%s", method, file_item->valuestring);
        } else if (pos_item && cJSON_IsNumber(pos_item)) {
            snprintf(line, sizeof(line), "%s position_ms=%d", method, pos_item->valueint);
        } else if (vol_item && cJSON_IsNumber(vol_item)) {
            snprintf(line, sizeof(line), "%s volume=%d", method, vol_item->valueint);
        } else {
            snprintf(line, sizeof(line), "%s", method);
        }
        ipclog(line);
    }

    for (int i = 0; table[i].method != NULL; i++) {
        if (strcmp(table[i].method, method) == 0) {
            table[i].fn(fd, req);
            return;
        }
    }
    ipclog("UNKNOWN method");
    send_error(fd, "unknown method");
}

void send_error(int fd, const char *message)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "error", message);
    char *str = cJSON_PrintUnformatted(res);
    if (str) { send_msg(fd, str); free(str); }
    cJSON_Delete(res);
}

/* ── handlers ── */

static void handle_ping(int fd, cJSON *req) {
    send_result(fd, req, "pong");
}

static void handle_echo(int fd, cJSON *req) {
    send_result(fd, req, "this is a test echo");
}

static const char *param_string(cJSON *req, const char *key) {
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) return NULL;
    cJSON *item = cJSON_GetObjectItem(params, key);
    if (!item || !cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

static int param_int(cJSON *req, const char *key, int *out) {
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) return -1;
    cJSON *item = cJSON_GetObjectItem(params, key);
    if (!item || !cJSON_IsNumber(item)) return -1;
    *out = item->valueint;
    return 0;
}

static void handle_play_start(int fd, cJSON *req) {
    const char *file = param_string(req, "file");
    if (!file) { send_error(fd, "missing 'file' param"); return; }
    if (audio_play_start(file) < 0) {
        send_error(fd, "play_start failed");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_play_pause(int fd, cJSON *req) {
    if (audio_play_pause() < 0) {
        send_error(fd, "not playing");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_play_resume(int fd, cJSON *req) {
    if (audio_play_resume() < 0) {
        send_error(fd, "not paused");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_play_stop(int fd, cJSON *req) {
    audio_play_stop();
    send_result(fd, req, "ok");
}

static void handle_play_seek(int fd, cJSON *req) {
    int pos;
    if (param_int(req, "position_ms", &pos) != 0) {
        send_error(fd, "missing 'position_ms' param");
        return;
    }
    if (audio_play_seek(pos) < 0) {
        send_error(fd, "seek failed");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_record_start(int fd, cJSON *req) {
    const char *file = param_string(req, "file");
    if (!file) { send_error(fd, "missing 'file' param"); return; }
    if (audio_record_start(file) < 0) {
        send_error(fd, "record_start failed");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_record_pause(int fd, cJSON *req) {
    if (audio_record_pause() < 0) {
        send_error(fd, "not recording");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_record_resume(int fd, cJSON *req) {
    if (audio_record_resume() < 0) {
        send_error(fd, "not paused");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_record_stop(int fd, cJSON *req) {
    audio_record_stop();
    send_result(fd, req, "ok");
}

static void handle_volume(int fd, cJSON *req) {
    int v;
    if (param_int(req, "volume", &v) != 0) {
        send_error(fd, "missing 'volume' param");
        return;
    }
    if (v < 0 || v > 100) {
        send_error(fd, "volume must be 0-100");
        return;
    }
    audio_set_volume(v);
    send_result(fd, req, "ok");
}

static void handle_status(int fd, cJSON *req) {
    cJSON *s = cJSON_CreateObject();
    cJSON_AddBoolToObject  (s, "playing",    audio_is_playing());
    cJSON_AddBoolToObject  (s, "paused",     audio_is_paused());
    cJSON_AddBoolToObject  (s, "recording",  audio_is_recording());
    cJSON_AddNumberToObject(s, "elapsed_ms", audio_play_elapsed_ms());
    cJSON_AddNumberToObject(s, "volume",     audio_get_volume());
    send_result_json(fd, req, s);
}
