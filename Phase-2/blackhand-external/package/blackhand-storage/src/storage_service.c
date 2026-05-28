/*
 * blackhand-storage — persistent data service using JSON files.
 *
 * All data lives in /data/:
 *   contacts.json   — array of contact objects
 *   messages.json   — array of SMS/call message objects
 *   calls.json      — array of call-log objects
 *   locations.json  — array of GPS location objects
 *   settings.json   — key/value object
 *
 * Data is loaded into memory at startup.  Every mutation immediately writes
 * the affected file back to disk so no data is lost on crash.
 *
 * JSON-RPC methods:
 *   contacts.list / .add / .delete / .update / .search
 *   messages.list / .add / .delete / .mark_read
 *   calls.list / .add / .delete
 *   locations.add / .list
 *   settings.get / .set
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "ipc.h"
#include "ipc_framing.h"
#include "cJSON.h"

#define STORAGE_SOCK  "/run/bh-storage.sock"
#define DATA_DIR      "/data"
#define BUFFER_SIZE   32768

/* ── file paths ─────────────────────────────────────────────────────────── */

static const char *PATH_CONTACTS  = DATA_DIR "/contacts.json";
static const char *PATH_MESSAGES  = DATA_DIR "/messages.json";
static const char *PATH_CALLS     = DATA_DIR "/calls.json";
static const char *PATH_LOCATIONS = DATA_DIR "/locations.json";
static const char *PATH_SETTINGS  = DATA_DIR "/settings.json";

/* ── in-memory state ────────────────────────────────────────────────────── */

static cJSON *g_contacts  = NULL;
static cJSON *g_messages  = NULL;
static cJSON *g_calls     = NULL;
static cJSON *g_locations = NULL;
static cJSON *g_settings  = NULL;

/* ── JSON file I/O ──────────────────────────────────────────────────────── */

static cJSON *load_json_array(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return cJSON_CreateArray();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return cJSON_CreateArray(); }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return cJSON_CreateArray(); }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return cJSON_CreateArray();
    }
    return root;
}

static cJSON *load_json_object(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return cJSON_CreateObject();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return cJSON_CreateObject(); }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return cJSON_CreateObject(); }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return cJSON_CreateObject();
    }
    return root;
}

static void save_json(const char *path, cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    if (!str) return;

    /* Write to a temp file then rename for atomicity */
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (f) {
        fputs(str, f);
        fclose(f);
        rename(tmp, path);
    }
    free(str);
}

/* ── ID helpers ─────────────────────────────────────────────────────────── */

static int next_id(cJSON *arr)
{
    int max_id = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        cJSON *id = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(id) && id->valueint > max_id)
            max_id = id->valueint;
    }
    return max_id + 1;
}

/* ── IPC helpers ────────────────────────────────────────────────────────── */

static void send_error(int fd, const char *msg)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "error", msg);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

static void send_result_json(int fd, cJSON *req, cJSON *result)
{
    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (!id || !cJSON_IsNumber(id)) {
        send_error(fd, "invalid id");
        cJSON_Delete(result);
        return;
    }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddItemToObject(res, "result", result);
    cJSON_AddNumberToObject(res, "id", id->valueint);
    char *str = cJSON_PrintUnformatted(res);
    send_msg(fd, str);
    free(str);
    cJSON_Delete(res);
}

static void send_result_str(int fd, cJSON *req, const char *s)
{
    cJSON *res_val = cJSON_CreateString(s);
    send_result_json(fd, req, res_val);
}

static void send_result_num(int fd, cJSON *req, int n)
{
    send_result_json(fd, req, cJSON_CreateNumber((double)n));
}

/* ── contacts ────────────────────────────────────────────────────────────── */

static void handle_contacts_list(int fd, cJSON *req)
{
    send_result_json(fd, req, cJSON_Duplicate(g_contacts, 1));
}

static void handle_contacts_add(int fd, cJSON *req)
{
    cJSON *p = cJSON_GetObjectItem(req, "params");
    if (!p) { send_error(fd, "missing params"); return; }
    cJSON *name  = cJSON_GetObjectItem(p, "name");
    cJSON *phone = cJSON_GetObjectItem(p, "phone");
    if (!cJSON_IsString(name) || !cJSON_IsString(phone)) {
        send_error(fd, "name and phone required");
        return;
    }
    cJSON *entry = cJSON_CreateObject();
    int id = next_id(g_contacts);
    cJSON_AddNumberToObject(entry, "id",    (double)id);
    cJSON_AddStringToObject(entry, "name",  name->valuestring);
    cJSON_AddStringToObject(entry, "phone", phone->valuestring);
    cJSON *email = cJSON_GetObjectItem(p, "email");
    cJSON_AddStringToObject(entry, "email",
                            cJSON_IsString(email) ? email->valuestring : "");
    cJSON *notes = cJSON_GetObjectItem(p, "notes");
    cJSON_AddStringToObject(entry, "notes",
                            cJSON_IsString(notes) ? notes->valuestring : "");
    cJSON_AddItemToArray(g_contacts, entry);
    save_json(PATH_CONTACTS, g_contacts);
    send_result_num(fd, req, id);
}

static void handle_contacts_delete(int fd, cJSON *req)
{
    cJSON *p  = cJSON_GetObjectItem(req, "params");
    cJSON *id = cJSON_GetObjectItem(p, "id");
    if (!cJSON_IsNumber(id)) { send_error(fd, "missing id"); return; }
    int target = id->valueint;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, g_contacts) {
        cJSON *cid = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(cid) && cid->valueint == target) {
            cJSON_DeleteItemFromArray(g_contacts, idx);
            save_json(PATH_CONTACTS, g_contacts);
            send_result_str(fd, req, "ok");
            return;
        }
        idx++;
    }
    send_error(fd, "not found");
}

static void handle_contacts_update(int fd, cJSON *req)
{
    cJSON *p  = cJSON_GetObjectItem(req, "params");
    cJSON *id = cJSON_GetObjectItem(p, "id");
    if (!cJSON_IsNumber(id)) { send_error(fd, "missing id"); return; }
    int target = id->valueint;
    cJSON *item;
    cJSON_ArrayForEach(item, g_contacts) {
        cJSON *cid = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(cid) && cid->valueint == target) {
            cJSON *name  = cJSON_GetObjectItem(p, "name");
            cJSON *phone = cJSON_GetObjectItem(p, "phone");
            cJSON *email = cJSON_GetObjectItem(p, "email");
            cJSON *notes = cJSON_GetObjectItem(p, "notes");
            if (cJSON_IsString(name))
                cJSON_ReplaceItemInObject(item, "name",  cJSON_CreateString(name->valuestring));
            if (cJSON_IsString(phone))
                cJSON_ReplaceItemInObject(item, "phone", cJSON_CreateString(phone->valuestring));
            if (cJSON_IsString(email))
                cJSON_ReplaceItemInObject(item, "email", cJSON_CreateString(email->valuestring));
            if (cJSON_IsString(notes))
                cJSON_ReplaceItemInObject(item, "notes", cJSON_CreateString(notes->valuestring));
            save_json(PATH_CONTACTS, g_contacts);
            send_result_str(fd, req, "ok");
            return;
        }
    }
    send_error(fd, "not found");
}

static void handle_contacts_search(int fd, cJSON *req)
{
    cJSON *p     = cJSON_GetObjectItem(req, "params");
    cJSON *query = cJSON_GetObjectItem(p, "query");
    if (!cJSON_IsString(query)) { send_error(fd, "missing query"); return; }
    const char *q = query->valuestring;
    cJSON *results = cJSON_CreateArray();
    cJSON *item;
    cJSON_ArrayForEach(item, g_contacts) {
        cJSON *name  = cJSON_GetObjectItem(item, "name");
        cJSON *phone = cJSON_GetObjectItem(item, "phone");
        int match = 0;
        if (cJSON_IsString(name)  && strstr(name->valuestring, q))  match = 1;
        if (cJSON_IsString(phone) && strstr(phone->valuestring, q)) match = 1;
        if (match) cJSON_AddItemToArray(results, cJSON_Duplicate(item, 1));
    }
    send_result_json(fd, req, results);
}

/* ── messages ────────────────────────────────────────────────────────────── */

static void handle_messages_list(int fd, cJSON *req)
{
    send_result_json(fd, req, cJSON_Duplicate(g_messages, 1));
}

static void handle_messages_add(int fd, cJSON *req)
{
    cJSON *p     = cJSON_GetObjectItem(req, "params");
    cJSON *phone = cJSON_GetObjectItem(p, "phone");
    cJSON *body  = cJSON_GetObjectItem(p, "body");
    if (!cJSON_IsString(phone) || !cJSON_IsString(body)) {
        send_error(fd, "phone and body required");
        return;
    }
    cJSON *entry = cJSON_CreateObject();
    int id = next_id(g_messages);
    cJSON_AddNumberToObject(entry, "id",          (double)id);
    cJSON_AddStringToObject(entry, "phone",        phone->valuestring);
    cJSON *sender = cJSON_GetObjectItem(p, "sender");
    cJSON_AddStringToObject(entry, "sender",
                            cJSON_IsString(sender) ? sender->valuestring : "");
    cJSON_AddStringToObject(entry, "body",         body->valuestring);
    cJSON *is_out = cJSON_GetObjectItem(p, "is_outgoing");
    cJSON_AddNumberToObject(entry, "is_outgoing",
                            cJSON_IsNumber(is_out) ? is_out->valuedouble : 0);
    cJSON *ts = cJSON_GetObjectItem(p, "timestamp");
    cJSON_AddStringToObject(entry, "timestamp",
                            cJSON_IsString(ts) ? ts->valuestring : "");
    cJSON_AddNumberToObject(entry, "read", 0);
    cJSON_AddItemToArray(g_messages, entry);
    save_json(PATH_MESSAGES, g_messages);
    send_result_num(fd, req, id);
}

static void handle_messages_delete(int fd, cJSON *req)
{
    cJSON *p  = cJSON_GetObjectItem(req, "params");
    cJSON *id = cJSON_GetObjectItem(p, "id");
    if (!cJSON_IsNumber(id)) { send_error(fd, "missing id"); return; }
    int target = id->valueint;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, g_messages) {
        cJSON *mid = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(mid) && mid->valueint == target) {
            cJSON_DeleteItemFromArray(g_messages, idx);
            save_json(PATH_MESSAGES, g_messages);
            send_result_str(fd, req, "ok");
            return;
        }
        idx++;
    }
    send_error(fd, "not found");
}

static void handle_messages_mark_read(int fd, cJSON *req)
{
    cJSON *p  = cJSON_GetObjectItem(req, "params");
    cJSON *id = cJSON_GetObjectItem(p, "id");
    if (!cJSON_IsNumber(id)) { send_error(fd, "missing id"); return; }
    int target = id->valueint;
    cJSON *item;
    cJSON_ArrayForEach(item, g_messages) {
        cJSON *mid = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(mid) && mid->valueint == target) {
            cJSON_ReplaceItemInObject(item, "read", cJSON_CreateNumber(1));
            save_json(PATH_MESSAGES, g_messages);
            send_result_str(fd, req, "ok");
            return;
        }
    }
    send_error(fd, "not found");
}

/* ── calls ───────────────────────────────────────────────────────────────── */

static void handle_calls_list(int fd, cJSON *req)
{
    send_result_json(fd, req, cJSON_Duplicate(g_calls, 1));
}

static void handle_calls_add(int fd, cJSON *req)
{
    cJSON *p     = cJSON_GetObjectItem(req, "params");
    cJSON *phone = cJSON_GetObjectItem(p, "phone");
    cJSON *ctype = cJSON_GetObjectItem(p, "call_type");
    if (!cJSON_IsString(phone)) { send_error(fd, "phone required"); return; }

    cJSON *entry = cJSON_CreateObject();
    int id = next_id(g_calls);
    cJSON_AddNumberToObject(entry, "id",          (double)id);
    cJSON_AddStringToObject(entry, "phone",        phone->valuestring);
    cJSON *name = cJSON_GetObjectItem(p, "caller_name");
    cJSON_AddStringToObject(entry, "caller_name",
                            cJSON_IsString(name) ? name->valuestring : "");
    cJSON_AddStringToObject(entry, "call_type",
                            cJSON_IsString(ctype) ? ctype->valuestring : "outgoing");
    cJSON *dur = cJSON_GetObjectItem(p, "duration_sec");
    cJSON_AddNumberToObject(entry, "duration_sec",
                            cJSON_IsNumber(dur) ? dur->valuedouble : 0);
    cJSON *ts = cJSON_GetObjectItem(p, "timestamp");
    cJSON_AddStringToObject(entry, "timestamp",
                            cJSON_IsString(ts) ? ts->valuestring : "");
    cJSON_AddItemToArray(g_calls, entry);
    save_json(PATH_CALLS, g_calls);
    send_result_num(fd, req, id);
}

static void handle_calls_delete(int fd, cJSON *req)
{
    cJSON *p  = cJSON_GetObjectItem(req, "params");
    cJSON *id = cJSON_GetObjectItem(p, "id");
    if (!cJSON_IsNumber(id)) { send_error(fd, "missing id"); return; }
    int target = id->valueint;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, g_calls) {
        cJSON *cid = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(cid) && cid->valueint == target) {
            cJSON_DeleteItemFromArray(g_calls, idx);
            save_json(PATH_CALLS, g_calls);
            send_result_str(fd, req, "ok");
            return;
        }
        idx++;
    }
    send_error(fd, "not found");
}

/* ── locations ───────────────────────────────────────────────────────────── */

static void handle_locations_add(int fd, cJSON *req)
{
    cJSON *p   = cJSON_GetObjectItem(req, "params");
    cJSON *lat = cJSON_GetObjectItem(p, "latitude");
    cJSON *lon = cJSON_GetObjectItem(p, "longitude");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
        send_error(fd, "latitude and longitude required");
        return;
    }
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "id",        (double)next_id(g_locations));
    cJSON_AddNumberToObject(entry, "latitude",  lat->valuedouble);
    cJSON_AddNumberToObject(entry, "longitude", lon->valuedouble);
    cJSON *alt = cJSON_GetObjectItem(p, "altitude");
    cJSON_AddNumberToObject(entry, "altitude",
                            cJSON_IsNumber(alt) ? alt->valuedouble : 0.0);
    cJSON *spd = cJSON_GetObjectItem(p, "speed");
    cJSON_AddNumberToObject(entry, "speed",
                            cJSON_IsNumber(spd) ? spd->valuedouble : 0.0);
    cJSON *ts = cJSON_GetObjectItem(p, "timestamp");
    cJSON_AddStringToObject(entry, "timestamp",
                            cJSON_IsString(ts) ? ts->valuestring : "");
    cJSON_AddItemToArray(g_locations, entry);
    save_json(PATH_LOCATIONS, g_locations);
    send_result_str(fd, req, "ok");
}

static void handle_locations_list(int fd, cJSON *req)
{
    send_result_json(fd, req, cJSON_Duplicate(g_locations, 1));
}

/* ── settings ────────────────────────────────────────────────────────────── */

static void handle_settings_get(int fd, cJSON *req)
{
    cJSON *p   = cJSON_GetObjectItem(req, "params");
    cJSON *key = cJSON_GetObjectItem(p, "key");
    if (!cJSON_IsString(key)) { send_error(fd, "missing key"); return; }
    cJSON *val = cJSON_GetObjectItem(g_settings, key->valuestring);
    if (val) {
        send_result_json(fd, req, cJSON_Duplicate(val, 1));
    } else {
        send_result_str(fd, req, "");
    }
}

static void handle_settings_set(int fd, cJSON *req)
{
    cJSON *p     = cJSON_GetObjectItem(req, "params");
    cJSON *key   = cJSON_GetObjectItem(p, "key");
    cJSON *value = cJSON_GetObjectItem(p, "value");
    if (!cJSON_IsString(key)) { send_error(fd, "missing key"); return; }
    if (cJSON_GetObjectItem(g_settings, key->valuestring))
        cJSON_ReplaceItemInObject(g_settings, key->valuestring, cJSON_Duplicate(value, 1));
    else
        cJSON_AddItemToObject(g_settings, key->valuestring, cJSON_Duplicate(value, 1));
    save_json(PATH_SETTINGS, g_settings);
    send_result_str(fd, req, "ok");
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

struct handler { const char *method; void (*fn)(int, cJSON *); };

static struct handler table[] = {
    {"contacts.list",     handle_contacts_list},
    {"contacts.add",      handle_contacts_add},
    {"contacts.delete",   handle_contacts_delete},
    {"contacts.update",   handle_contacts_update},
    {"contacts.search",   handle_contacts_search},
    {"messages.list",     handle_messages_list},
    {"messages.add",      handle_messages_add},
    {"messages.delete",   handle_messages_delete},
    {"messages.mark_read",handle_messages_mark_read},
    {"calls.list",        handle_calls_list},
    {"calls.add",         handle_calls_add},
    {"calls.delete",      handle_calls_delete},
    {"locations.add",     handle_locations_add},
    {"locations.list",    handle_locations_list},
    {"settings.get",      handle_settings_get},
    {"settings.set",      handle_settings_set},
    {NULL, NULL}
};

static void dispatch(int fd, cJSON *req)
{
    cJSON *ver  = cJSON_GetObjectItem(req, "jsonrpc");
    if (!ver || !cJSON_IsString(ver) || strcmp(ver->valuestring, "2.0") != 0) {
        send_error(fd, "invalid jsonrpc version");
        return;
    }
    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (!id || !cJSON_IsNumber(id)) { send_error(fd, "missing id"); return; }
    cJSON *meth = cJSON_GetObjectItem(req, "method");
    if (!meth || !cJSON_IsString(meth)) { send_error(fd, "missing method"); return; }

    const char *method = meth->valuestring;
    for (int i = 0; table[i].method; i++) {
        if (strcmp(table[i].method, method) == 0) {
            table[i].fn(fd, req);
            return;
        }
    }
    send_error(fd, "unknown method");
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Ensure data directory exists */
    mkdir(DATA_DIR, 0755);

    /* Load all JSON data files */
    g_contacts  = load_json_array(PATH_CONTACTS);
    g_messages  = load_json_array(PATH_MESSAGES);
    g_calls     = load_json_array(PATH_CALLS);
    g_locations = load_json_array(PATH_LOCATIONS);
    g_settings  = load_json_object(PATH_SETTINGS);

    int server_fd = ipc_server_listen(STORAGE_SOCK);
    if (server_fd < 0) {
        perror("blackhand-storage: ipc_server_listen failed");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "blackhand-storage: listening on " STORAGE_SOCK "\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("blackhand-storage: accept");
            continue;
        }
        while (1) {
            char buffer[BUFFER_SIZE];
            int n = recv_msg(client_fd, buffer, BUFFER_SIZE);
            if (n <= 0) break;

            cJSON *req = cJSON_Parse(buffer);
            if (!req) break;

            dispatch(client_fd, req);
            cJSON_Delete(req);
        }
        close(client_fd);
    }

    return 0;
}
