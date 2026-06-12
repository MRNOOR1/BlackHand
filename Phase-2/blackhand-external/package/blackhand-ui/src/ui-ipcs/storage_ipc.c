#include "storage_ipc.h"
#include "ipc.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define STORAGE_SOCK  "/run/bh-storage.sock"
#define RESP_SIZE     32768

/* ── internals ───────────────────────────────────────────────────────────── */

/* Build {"jsonrpc":"2.0","method":M,"params":P,"id":1} into req_out; P owned. */
static int rpc(const char *method, cJSON *params /* may be NULL */,
               char *req_out, size_t req_sz)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", method);
    if (params) cJSON_AddItemToObject(req, "params", params);
    cJSON_AddNumberToObject(req, "id", 1);
    char *str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!str) return -1;
    int rc = ((size_t)snprintf(req_out, req_sz, "%s", str) < req_sz) ? 0 : -1;
    free(str);
    return rc;
}

/* Send req; expect {"result": <array>}; copy array JSON into buf. */
static int request_array(const char *req, char *buf, int buf_size)
{
    char resp[RESP_SIZE];
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) { cJSON_Delete(root); return -1; }
    char *str = cJSON_PrintUnformatted(result);
    int rc = -1;
    if (str && buf && buf_size > 0) {
        snprintf(buf, (size_t)buf_size, "%s", str);
        rc = 0;
    }
    free(str);
    cJSON_Delete(root);
    return rc;
}

/* Send req; success = any "result", no "error". */
static int request_ok(const char *req)
{
    char resp[1024];
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    int ok = (cJSON_GetObjectItem(root, "result") != NULL) &&
             (cJSON_GetObjectItem(root, "error")  == NULL);
    cJSON_Delete(root);
    return ok ? 0 : -1;
}

/* ── public API ──────────────────────────────────────────────────────────── */

int storage_ipc_ping(void)
{
    char req[128];
    if (rpc("ping", NULL, req, sizeof(req)) != 0) return -1;
    return request_ok(req);
}

int storage_ipc_contacts_list(char *buf, int buf_size)
{
    char req[128];
    if (rpc("contacts.list", NULL, req, sizeof(req)) != 0) return -1;
    return request_array(req, buf, buf_size);
}

int storage_ipc_contacts_save(const char *name, const char *number)
{
    if (!name || !number) return -1;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "name", name);
    cJSON_AddStringToObject(p, "number", number);
    char req[512];
    if (rpc("contacts.save", p, req, sizeof(req)) != 0) return -1;
    return request_ok(req);
}

int storage_ipc_contacts_delete(const char *number)
{
    if (!number) return -1;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "number", number);
    char req[256];
    if (rpc("contacts.delete", p, req, sizeof(req)) != 0) return -1;
    return request_ok(req);
}

int storage_ipc_sms_threads(char *buf, int buf_size)
{
    char req[128];
    if (rpc("history.sms_threads", NULL, req, sizeof(req)) != 0) return -1;
    return request_array(req, buf, buf_size);
}

int storage_ipc_sms_list(const char *number, char *buf, int buf_size)
{
    if (!number) return -1;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "number", number);
    char req[256];
    if (rpc("history.sms_list", p, req, sizeof(req)) != 0) return -1;
    return request_array(req, buf, buf_size);
}

int storage_ipc_mark_read(const char *number)
{
    if (!number) return -1;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "number", number);
    char req[256];
    if (rpc("history.mark_read", p, req, sizeof(req)) != 0) return -1;
    return request_ok(req);
}

int storage_ipc_calls_recent(int limit, char *buf, int buf_size)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "limit", limit > 0 ? limit : 50);
    char req[256];
    if (rpc("history.calls_recent", p, req, sizeof(req)) != 0) return -1;
    return request_array(req, buf, buf_size);
}

int storage_ipc_settings_get_str(const char *key, char *buf, int buf_size)
{
    if (!key || !buf || buf_size <= 0) return -1;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "key", key);
    char req[256];
    if (rpc("settings.get", p, req, sizeof(req)) != 0) return -1;

    char resp[2048];
    if (ipc_request(STORAGE_SOCK, req, resp, sizeof(resp)) != IPC_OK) return -1;
    cJSON *root = cJSON_Parse(resp);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    int rc = -1;
    if (cJSON_IsString(result)) {
        snprintf(buf, (size_t)buf_size, "%s", result->valuestring);
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}

int storage_ipc_settings_set_str(const char *key, const char *value)
{
    if (!key || !value) return -1;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "key", key);
    cJSON_AddStringToObject(p, "value", value);
    char req[512];
    if (rpc("settings.set", p, req, sizeof(req)) != 0) return -1;
    return request_ok(req);
}
