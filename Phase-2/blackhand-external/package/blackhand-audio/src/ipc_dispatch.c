/*
 * ipc_dispatch.c — blackhand-audio service dispatch table.
 *
 * This file is compiled as part of the blackhand-audio binary, NOT as part
 * of libblackhand-ipc.a. It defines the dispatch table for this specific
 * service and includes audio_alsa.h so handlers can call the ALSA layer.
 *
 * JSON-RPC methods exposed by this service:
 *   ping         — returns "pong"
 *   echo         — returns a test string
 *   play         — starts playing a file (params: { "file": "/path/to.wav" })
 *   pause        — stops current playback
 *   stop         — stops current playback
 *   volume       — sets master volume (params: { "volume": 0-100 })
 *   status       — returns { "playing": bool, "recording": bool, "volume": int }
 *   record_start — starts recording (params: { "file": "/path/to/out.wav" })
 *   record_stop  — stops recording and finalises the WAV file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_dispatch.h"
#include "ipc_framing.h"
#include "cJSON.h"
#include "audio_alsa.h"

/* ── private handler forward declarations ── */
static void handle_ping(int fd, cJSON *req);
static void handle_echo(int fd, cJSON *req);
static void handle_play(int fd, cJSON *req);
static void handle_pause(int fd, cJSON *req);
static void handle_stop(int fd, cJSON *req);
static void handle_volume(int fd, cJSON *req);
static void handle_status(int fd, cJSON *req);
static void handle_record_start(int fd, cJSON *req);
static void handle_record_stop(int fd, cJSON *req);

/* ── dispatch table ── */
struct handler {
    const char *method;
    void (*fn)(int, cJSON *);
};

static struct handler table[] = {
    {"ping",         handle_ping},
    {"echo",         handle_echo},
    {"play",         handle_play},
    {"pause",        handle_pause},
    {"stop",         handle_stop},
    {"volume",       handle_volume},
    {"status",       handle_status},
    {"record_start", handle_record_start},
    {"record_stop",  handle_record_stop},
    {NULL, NULL}
};

/* ── helpers ── */

/* Send a JSON-RPC 2.0 success response where the result is a plain string.
   Extracts the id from the request so the caller can match the response. */
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
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* Send a JSON-RPC 2.0 success response where the result is a JSON object.
   result_obj ownership is transferred — do not use it after this call. */
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
    cJSON_AddItemToObject(res, "result", result_obj); /* transfers ownership */
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* ── dispatch() — called from main.c for every incoming request ── */
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

    for (int i = 0; table[i].method != NULL; i++) {
        if (strcmp(table[i].method, method) == 0) {
            table[i].fn(fd, req);
            return;
        }
    }

    send_error(fd, "unknown method");
}

/* ── send_error() ── */
void send_error(int fd, const char *message)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "error", message);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

/* ── handler implementations ── */

static void handle_ping(int fd, cJSON *req)
{
    int id = cJSON_GetObjectItem(req, "id")->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", "pong");
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

static void handle_echo(int fd, cJSON *req)
{
    int id = cJSON_GetObjectItem(req, "id")->valueint;
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "result", "this is a test echo");
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

static void handle_play(int fd, cJSON *req)
{
    /* Expected: { "method":"play", "params":{ "file":"/path/to/file.wav" }, "id":n } */
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) {
        send_error(fd, "missing or invalid params");
        return;
    }
    cJSON *file_item = cJSON_GetObjectItem(params, "file");
    if (!file_item || !cJSON_IsString(file_item)) {
        send_error(fd, "missing or invalid 'file' field");
        return;
    }
    audio_play(file_item->valuestring);
    send_result(fd, req, "ok");
}

static void handle_pause(int fd, cJSON *req)
{
    audio_stop();
    send_result(fd, req, "ok");
}

static void handle_stop(int fd, cJSON *req)
{
    audio_stop();
    send_result(fd, req, "ok");
}

static void handle_volume(int fd, cJSON *req)
{
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) {
        send_error(fd, "missing or invalid params");
        return;
    }
    cJSON *vol_item = cJSON_GetObjectItem(params, "volume");
    if (!vol_item || !cJSON_IsNumber(vol_item)) {
        send_error(fd, "missing or invalid 'volume' field");
        return;
    }
    int volume = vol_item->valueint;
    if (volume < 0 || volume > 100) {
        send_error(fd, "volume must be between 0 and 100");
        return;
    }
    audio_set_volume(volume);
    send_result(fd, req, "ok");
}

static void handle_status(int fd, cJSON *req)
{
    cJSON *status_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(status_obj, "playing",   audio_is_playing());
    cJSON_AddBoolToObject(status_obj, "recording", audio_is_recording());
    cJSON_AddNumberToObject(status_obj, "volume",  audio_get_volume());
    send_result_json(fd, req, status_obj);
}

static void handle_record_start(int fd, cJSON *req)
{
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!params || !cJSON_IsObject(params)) {
        send_error(fd, "missing or invalid params");
        return;
    }
    cJSON *file_item = cJSON_GetObjectItem(params, "file");
    if (!file_item || !cJSON_IsString(file_item)) {
        send_error(fd, "missing or invalid 'file' field");
        return;
    }
    if (audio_record_start(file_item->valuestring) < 0) {
        send_error(fd, "record_start failed — already recording or device unavailable");
        return;
    }
    send_result(fd, req, "ok");
}

static void handle_record_stop(int fd, cJSON *req)
{
    audio_record_stop();
    send_result(fd, req, "ok");
}
