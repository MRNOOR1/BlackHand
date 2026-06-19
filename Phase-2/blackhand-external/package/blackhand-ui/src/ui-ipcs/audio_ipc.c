#include "audio_ipc.h"
#include "ipc.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <stdarg.h>

#define AUDIO_SOCKET_PATH  "/run/bh-audio.sock"
#define AUDIO_RESPONSE_SIZE 4096

/* Client-side log → /boot/logs/ui-audio.log on the FAT32 partition.
   Pair with daemon's /boot/logs/audio.log to see both sides of every
   IPC round-trip. Skips status() spam (30 Hz from the tick). */
static FILE *g_uilog = NULL;
static pthread_mutex_t g_uilog_lock = PTHREAD_MUTEX_INITIALIZER;

static void uilog(const char *fmt, ...) {
    pthread_mutex_lock(&g_uilog_lock);
    if (!g_uilog) {
        mkdir("/boot/logs", 0755);
        g_uilog = fopen("/boot/logs/ui-audio.log", "a");
    }
    if (g_uilog) {
        time_t now = time(NULL);
        struct tm tmv; localtime_r(&now, &tmv);
        char ts[32]; strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
        fprintf(g_uilog, "[%s] ", ts);
        va_list ap; va_start(ap, fmt);
        vfprintf(g_uilog, fmt, ap);
        va_end(ap);
        fputc('\n', g_uilog);
        fflush(g_uilog);
    }
    pthread_mutex_unlock(&g_uilog_lock);
}

static int response_is_ok(const char *response) {
    cJSON *root = cJSON_Parse(response);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    int ok = cJSON_IsString(result) && strcmp(result->valuestring, "ok") == 0;
    cJSON_Delete(root);
    return ok ? 0 : -1;
}

/* Send a no-param request like {"jsonrpc":"2.0","method":"<m>","id":1}.
   Used for ping/play_pause/play_resume/play_stop/record_pause/record_resume/record_stop. */
static int send_simple(const char *method) {
    char request[128];
    int n = snprintf(request, sizeof(request),
                     "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"id\":1}", method);
    if (n < 0 || (size_t)n >= sizeof(request)) return -1;

    char response[AUDIO_RESPONSE_SIZE];
    if (ipc_request(AUDIO_SOCKET_PATH, request, response, sizeof(response)) != IPC_OK)
        return -1;
    return response_is_ok(response);
}

/* Send a request with a single string param: {"file": "..."} */
static int send_with_file(const char *method, const char *filepath) {
    if (!filepath) return -1;
    cJSON *req = cJSON_CreateObject();
    if (!req) return -1;
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", method);
    cJSON *params = cJSON_CreateObject();
    if (!params) { cJSON_Delete(req); return -1; }
    cJSON_AddStringToObject(params, "file", filepath);
    cJSON_AddItemToObject(req, "params", params);
    cJSON_AddNumberToObject(req, "id", 1);
    char *json = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!json) return -1;

    char response[AUDIO_RESPONSE_SIZE];
    int rc = ipc_request(AUDIO_SOCKET_PATH, json, response, sizeof(response));
    free(json);
    if (rc != IPC_OK) return -1;
    return response_is_ok(response);
}

int audio_ipc_ping(void) {
    char response[AUDIO_RESPONSE_SIZE];
    if (ipc_request(AUDIO_SOCKET_PATH,
                    "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":1}",
                    response, sizeof(response)) != IPC_OK)
        return -1;

    cJSON *root = cJSON_Parse(response);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    int ok = cJSON_IsString(result) && strcmp(result->valuestring, "pong") == 0;
    cJSON_Delete(root);
    return ok ? 0 : -1;
}

int audio_ipc_play_start(const char *filepath) {
    int rc = send_with_file("play_start", filepath);
    uilog("→ play_start file=%s  rc=%d", filepath ? filepath : "(null)", rc);
    return rc;
}
int audio_ipc_play_pause(void) {
    int rc = send_simple("play_pause");
    uilog("→ play_pause  rc=%d", rc);
    return rc;
}
int audio_ipc_play_resume(void) {
    int rc = send_simple("play_resume");
    uilog("→ play_resume  rc=%d", rc);
    return rc;
}
int audio_ipc_play_stop(void) {
    int rc = send_simple("play_stop");
    uilog("→ play_stop  rc=%d", rc);
    return rc;
}

int audio_ipc_play_seek(int position_ms) {
    char request[128];
    int n = snprintf(request, sizeof(request),
                     "{\"jsonrpc\":\"2.0\",\"method\":\"play_seek\","
                     "\"params\":{\"position_ms\":%d},\"id\":1}", position_ms);
    if (n < 0 || (size_t)n >= sizeof(request)) return -1;

    char response[AUDIO_RESPONSE_SIZE];
    int rc;
    if (ipc_request(AUDIO_SOCKET_PATH, request, response, sizeof(response)) != IPC_OK) {
        rc = -1;
    } else {
        rc = response_is_ok(response);
    }
    uilog("→ play_seek position_ms=%d  rc=%d", position_ms, rc);
    return rc;
}

int audio_ipc_record_start(const char *filepath) {
    int rc = send_with_file("record_start", filepath);
    uilog("→ record_start file=%s  rc=%d", filepath ? filepath : "(null)", rc);
    return rc;
}
int audio_ipc_record_pause(void) {
    int rc = send_simple("record_pause");
    uilog("→ record_pause  rc=%d", rc);
    return rc;
}
int audio_ipc_record_resume(void) {
    int rc = send_simple("record_resume");
    uilog("→ record_resume  rc=%d", rc);
    return rc;
}
int audio_ipc_record_stop(void) {
    int rc = send_simple("record_stop");
    uilog("→ record_stop  rc=%d", rc);
    return rc;
}

int audio_ipc_volume(int percent) {
    if (percent < 0 || percent > 100) return -1;
    char request[128];
    int n = snprintf(request, sizeof(request),
                     "{\"jsonrpc\":\"2.0\",\"method\":\"volume\","
                     "\"params\":{\"volume\":%d},\"id\":1}", percent);
    if (n < 0 || (size_t)n >= sizeof(request)) return -1;

    char response[AUDIO_RESPONSE_SIZE];
    if (ipc_request(AUDIO_SOCKET_PATH, request, response, sizeof(response)) != IPC_OK)
        return -1;
    return response_is_ok(response);
}

int audio_ipc_status_full(int *out_playing,
                          int *out_paused,
                          int *out_recording,
                          int *out_elapsed_ms,
                          int *out_volume) {
    char response[AUDIO_RESPONSE_SIZE];
    if (ipc_request(AUDIO_SOCKET_PATH,
                    "{\"jsonrpc\":\"2.0\",\"method\":\"status\",\"id\":1}",
                    response, sizeof(response)) != IPC_OK)
        return -1;

    cJSON *root = cJSON_Parse(response);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result)) { cJSON_Delete(root); return -1; }

    cJSON *playing    = cJSON_GetObjectItem(result, "playing");
    cJSON *paused     = cJSON_GetObjectItem(result, "paused");
    cJSON *recording  = cJSON_GetObjectItem(result, "recording");
    cJSON *elapsed_ms = cJSON_GetObjectItem(result, "elapsed_ms");
    cJSON *volume     = cJSON_GetObjectItem(result, "volume");

    if (out_playing    && cJSON_IsBool(playing))     *out_playing    = cJSON_IsTrue(playing) ? 1 : 0;
    if (out_paused     && cJSON_IsBool(paused))      *out_paused     = cJSON_IsTrue(paused)  ? 1 : 0;
    if (out_recording  && cJSON_IsBool(recording))   *out_recording  = cJSON_IsTrue(recording) ? 1 : 0;
    if (out_elapsed_ms && cJSON_IsNumber(elapsed_ms)) *out_elapsed_ms = elapsed_ms->valueint;
    if (out_volume     && cJSON_IsNumber(volume))    *out_volume     = volume->valueint;

    cJSON_Delete(root);
    return 0;
}

int audio_ipc_status(int *out_playing, int *out_volume) {
    if (!out_playing || !out_volume) return -1;
    return audio_ipc_status_full(out_playing, NULL, NULL, NULL, out_volume);
}

int audio_ipc_get_volume(void) {
    int v = -1;
    if (audio_ipc_status_full(NULL, NULL, NULL, NULL, &v) != 0) return -1;
    return v;
}
