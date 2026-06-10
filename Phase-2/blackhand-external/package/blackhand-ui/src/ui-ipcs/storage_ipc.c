#include "storage_ipc.h"
#include "ipc.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define STORAGE_SOCK  "/run/bh-storage.sock"
#define RESP_SIZE     32768

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int fetch_array(const char *method_json, char *buf, int buf_size)
{
    char resp[RESP_SIZE];
    if (ipc_request(STORAGE_SOCK, method_json, resp, sizeof(resp)) != IPC_OK)
        return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) { cJSON_Delete(root); return -1; }
    char *str = cJSON_PrintUnformatted(result);
    if (str && buf && buf_size > 0) {
        strncpy(buf, str, (size_t)(buf_size - 1));
        buf[buf_size - 1] = '\0';
    }
    free(str);
    cJSON_Delete(root);
    return 0;
}

static int is_ok_response(const char *resp)
{
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    int ok = cJSON_IsString(result) && strcmp(result->valuestring, "ok") == 0;
    cJSON_Delete(root);
    return ok ? 0 : -1;
}

static int get_num_result(const char *resp)
{
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    int id = cJSON_IsNumber(result) ? result->valueint : -1;
    cJSON_Delete(root);
    return id;
}

/* ── contacts ─────────────────────────────────────────────────────────────── */

int storage_ipc_contacts_list(char *buf, int buf_size)
{
    return fetch_array(
        "{\"jsonrpc\":\"2.0\",\"method\":\"contacts.list\",\"id\":1}",
        buf, buf_size);
}

int storage_ipc_contacts_add(const char *name, const char *phone,
                              const char *email, const char *notes)
{
    char req[512], resp[RESP_SIZE];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"contacts.add\","
        "\"params\":{\"name\":\"%s\",\"phone\":\"%s\","
        "\"email\":\"%s\",\"notes\":\"%s\"},\"id\":1}",
        name ? name : "", phone ? phone : "",
        email ? email : "", notes ? notes : "");
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return get_num_result(resp);
}

int storage_ipc_contacts_delete(int id)
{
    char req[128], resp[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"contacts.delete\","
        "\"params\":{\"id\":%d},\"id\":1}", id);
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok_response(resp);
}

int storage_ipc_contacts_update(int id, const char *name, const char *phone)
{
    char req[512], resp[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"contacts.update\","
        "\"params\":{\"id\":%d,\"name\":\"%s\",\"phone\":\"%s\"},\"id\":1}",
        id, name ? name : "", phone ? phone : "");
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok_response(resp);
}

int storage_ipc_contacts_search(const char *query, char *buf, int buf_size)
{
    char req[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"contacts.search\","
        "\"params\":{\"query\":\"%s\"},\"id\":1}",
        query ? query : "");
    return fetch_array(req, buf, buf_size);
}

/* ── messages ─────────────────────────────────────────────────────────────── */

int storage_ipc_messages_list(char *buf, int buf_size)
{
    return fetch_array(
        "{\"jsonrpc\":\"2.0\",\"method\":\"messages.list\",\"id\":1}",
        buf, buf_size);
}

int storage_ipc_messages_add(const char *phone, const char *sender,
                              const char *body, int is_outgoing)
{
    char req[1024], resp[RESP_SIZE];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"messages.add\","
        "\"params\":{\"phone\":\"%s\",\"sender\":\"%s\","
        "\"body\":\"%s\",\"is_outgoing\":%d},\"id\":1}",
        phone ? phone : "", sender ? sender : "",
        body  ? body  : "", is_outgoing ? 1 : 0);
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return get_num_result(resp);
}

int storage_ipc_messages_delete(int id)
{
    char req[128], resp[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"messages.delete\","
        "\"params\":{\"id\":%d},\"id\":1}", id);
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok_response(resp);
}

int storage_ipc_messages_mark_read(int id)
{
    char req[128], resp[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"messages.mark_read\","
        "\"params\":{\"id\":%d},\"id\":1}", id);
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok_response(resp);
}

/* ── calls ────────────────────────────────────────────────────────────────── */

int storage_ipc_calls_list(char *buf, int buf_size)
{
    return fetch_array(
        "{\"jsonrpc\":\"2.0\",\"method\":\"calls.list\",\"id\":1}",
        buf, buf_size);
}

int storage_ipc_calls_add(const char *phone, const char *caller_name,
                           const char *call_type, int duration_sec)
{
    char req[512], resp[RESP_SIZE];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"calls.add\","
        "\"params\":{\"phone\":\"%s\",\"caller_name\":\"%s\","
        "\"call_type\":\"%s\",\"duration_sec\":%d},\"id\":1}",
        phone ? phone : "", caller_name ? caller_name : "",
        call_type ? call_type : "outgoing", duration_sec);
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return get_num_result(resp);
}

int storage_ipc_calls_delete(int id)
{
    char req[128], resp[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"calls.delete\","
        "\"params\":{\"id\":%d},\"id\":1}", id);
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok_response(resp);
}

/* ── locations ────────────────────────────────────────────────────────────── */

int storage_ipc_locations_add(double lat, double lon,
                               double alt, double speed)
{
    char req[256], resp[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"locations.add\","
        "\"params\":{\"latitude\":%.6f,\"longitude\":%.6f,"
        "\"altitude\":%.1f,\"speed\":%.1f},\"id\":1}",
        lat, lon, alt, speed);
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok_response(resp);
}

int storage_ipc_locations_list(char *buf, int buf_size)
{
    return fetch_array(
        "{\"jsonrpc\":\"2.0\",\"method\":\"locations.list\",\"id\":1}",
        buf, buf_size);
}

/* ── settings ─────────────────────────────────────────────────────────────── */

int storage_ipc_settings_get(const char *key, char *val_buf, int val_size)
{
    char req[256], resp[RESP_SIZE];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"settings.get\","
        "\"params\":{\"key\":\"%s\"},\"id\":1}",
        key ? key : "");
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsString(result) && val_buf && val_size > 0) {
        strncpy(val_buf, result->valuestring, (size_t)(val_size - 1));
        val_buf[val_size - 1] = '\0';
    }
    cJSON_Delete(root);
    return 0;
}

int storage_ipc_settings_set(const char *key, const char *value)
{
    char req[512], resp[256];
    snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"settings.set\","
        "\"params\":{\"key\":\"%s\",\"value\":\"%s\"},\"id\":1}",
        key ? key : "", value ? value : "");
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    return is_ok_response(resp);
}
