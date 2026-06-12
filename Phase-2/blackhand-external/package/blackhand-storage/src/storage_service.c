/*
 * blackhand-storage — persistent data service, per-number layout.
 *
 *   /data/contacts/contacts.json          one lookup table: [{name,number}]
 *   /data/numbers/<E.164>/sms.json        all SMS with that number
 *   /data/numbers/<E.164>/calls.json      all calls with that number
 *
 * Design rules (see CODE-REVIEW.md / project notes):
 *   - Contacts is ONLY a name lookup. It never grows with usage.
 *   - Every number that ever calls/texts/is dialled gets a folder. Saved vs
 *     unknown makes no difference to history storage — saving a contact just
 *     adds a name on top. No migration ever.
 *   - Writers (modem service) and readers (UI) both talk JSON-RPC to this
 *     daemon, which serialises all file access — no cross-process locking.
 *   - Every mutation is written immediately, atomically (tmp + rename).
 *
 * JSON-RPC methods:
 *   ping
 *   contacts.list                          → [{name,number}]
 *   contacts.save    {name,number}           upsert keyed by number
 *   contacts.delete  {number}
 *   history.add_sms  {number,direction:"in"|"out",body,ts?,read?}
 *   history.sms_list {number}              → [{direction,body,ts,read}]
 *   history.sms_threads                    → [{number,last_body,last_ts,unread,count}]
 *   history.mark_read {number}
 *   history.add_call {number,direction,outcome,ts?,duration?}
 *        outcome: "answered"|"missed"|"busy"|"rejected"|"no_answer"|"failed"
 *   history.calls_list   {number}          → [{direction,outcome,ts,duration}]
 *   history.calls_recent {limit?}          → [{number,direction,outcome,ts,duration}]
 *   settings.get {key} / settings.set {key,value}
 *
 * SMS entry:  { "direction":"in", "body":"...", "ts":1718000000, "read":0 }
 * Call entry: { "direction":"out", "outcome":"answered", "ts":..., "duration":62 }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "ipc.h"
#include "cJSON.h"

#define STORAGE_SOCK   "/run/bh-storage.sock"
#define CONTACTS_DIR   "/data/contacts"
#define CONTACTS_PATH  CONTACTS_DIR "/contacts.json"
#define NUMBERS_DIR    "/data/numbers"
#define SETTINGS_PATH  "/data/settings.json"
#define BUFFER_SIZE    32768

/* ── JSON file I/O (atomic) ─────────────────────────────────────────────── */

static cJSON *load_json(const char *path, int want_array)
{
    FILE *f = fopen(path, "r");
    if (!f) return want_array ? cJSON_CreateArray() : cJSON_CreateObject();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return want_array ? cJSON_CreateArray() : cJSON_CreateObject(); }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return want_array ? cJSON_CreateArray() : cJSON_CreateObject(); }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || (want_array ? !cJSON_IsArray(root) : !cJSON_IsObject(root))) {
        cJSON_Delete(root);
        return want_array ? cJSON_CreateArray() : cJSON_CreateObject();
    }
    return root;
}

static void save_json(const char *path, cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    if (!str) return;
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (f) {
        fputs(str, f);
        fflush(f);
        fclose(f);
        rename(tmp, path);   /* atomic replace */
    }
    free(str);
}

/* ── number helpers ─────────────────────────────────────────────────────── */

/* Sanitise a number into a safe directory name: optional leading '+', digits
 * only, length-capped. Returns 0 on success. Callers (UI/modem) normalise to
 * E.164 before sending; this is defence, not normalisation. */
static int number_dirname(const char *number, char *out, size_t out_sz)
{
    if (!number || !out || out_sz < 8) return -1;
    size_t w = 0;
    for (size_t r = 0; number[r] && w + 1 < out_sz && w < 20; r++) {
        char c = number[r];
        if ((c == '+' && w == 0) || (c >= '0' && c <= '9'))
            out[w++] = c;
    }
    out[w] = '\0';
    return (w >= 3) ? 0 : -1;   /* at least 3 digits to be a number */
}

static void ensure_dirs(void)
{
    mkdir("/data", 0755);
    mkdir(CONTACTS_DIR, 0755);
    mkdir(NUMBERS_DIR, 0755);
}

/* Path to a number's sms.json/calls.json, creating the folder on demand. */
static int number_file(const char *number, const char *fname,
                       char *out, size_t out_sz, int create)
{
    char dir[32];
    if (number_dirname(number, dir, sizeof(dir)) != 0) return -1;
    char dpath[128];
    snprintf(dpath, sizeof(dpath), NUMBERS_DIR "/%s", dir);
    if (create) mkdir(dpath, 0755);
    snprintf(out, out_sz, "%s/%s", dpath, fname);
    return 0;
}

/* ── response helpers ───────────────────────────────────────────────────── */

static void send_result(int fd, int id, cJSON *result /* owned */)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddItemToObject(res, "result", result);
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    if (str) { send_msg(fd, str); free(str); }
    cJSON_Delete(res);
}

static void send_ok(int fd, int id)
{
    send_result(fd, id, cJSON_CreateString("ok"));
}

static void send_err(int fd, int id, const char *message)
{
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    cJSON_AddStringToObject(res, "error", message);
    cJSON_AddNumberToObject(res, "id", id);
    char *str = cJSON_PrintUnformatted(res);
    if (str) { send_msg(fd, str); free(str); }
    cJSON_Delete(res);
}

static const char *param_str(cJSON *params, const char *key)
{
    cJSON *it = params ? cJSON_GetObjectItem(params, key) : NULL;
    return cJSON_IsString(it) ? it->valuestring : NULL;
}

static double param_num(cJSON *params, const char *key, double dflt)
{
    cJSON *it = params ? cJSON_GetObjectItem(params, key) : NULL;
    return cJSON_IsNumber(it) ? it->valuedouble : dflt;
}

/* ── contacts ───────────────────────────────────────────────────────────── */

static void h_contacts_list(int fd, int id, cJSON *params)
{
    (void)params;
    send_result(fd, id, load_json(CONTACTS_PATH, 1));
}

static void h_contacts_save(int fd, int id, cJSON *params)
{
    const char *name   = param_str(params, "name");
    const char *number = param_str(params, "number");
    if (!name || !number) { send_err(fd, id, "missing name or number"); return; }

    cJSON *arr = load_json(CONTACTS_PATH, 1);
    int replaced = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        cJSON *n = cJSON_GetObjectItem(it, "number");
        if (cJSON_IsString(n) && strcmp(n->valuestring, number) == 0) {
            cJSON_ReplaceItemInObject(it, "name", cJSON_CreateString(name));
            replaced = 1;
            break;
        }
    }
    if (!replaced) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "name",   name);
        cJSON_AddStringToObject(c, "number", number);
        cJSON_AddItemToArray(arr, c);
    }
    save_json(CONTACTS_PATH, arr);
    cJSON_Delete(arr);
    send_ok(fd, id);
}

static void h_contacts_delete(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    if (!number) { send_err(fd, id, "missing number"); return; }

    cJSON *arr = load_json(CONTACTS_PATH, 1);
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        cJSON *num = cJSON_GetObjectItem(it, "number");
        if (cJSON_IsString(num) && strcmp(num->valuestring, number) == 0) {
            cJSON_DeleteItemFromArray(arr, i);
            break;
        }
    }
    save_json(CONTACTS_PATH, arr);
    cJSON_Delete(arr);
    send_ok(fd, id);
}

/* ── history: SMS ───────────────────────────────────────────────────────── */

static void h_add_sms(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    const char *dir    = param_str(params, "direction");
    const char *body   = param_str(params, "body");
    if (!number || !dir || !body) {
        send_err(fd, id, "missing number/direction/body");
        return;
    }
    double ts = param_num(params, "ts", (double)time(NULL));
    /* outgoing messages are born read; incoming default unread */
    int read_dflt = (strcmp(dir, "out") == 0) ? 1 : 0;
    int read = (int)param_num(params, "read", read_dflt);

    char path[160];
    if (number_file(number, "sms.json", path, sizeof(path), 1) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    cJSON *arr = load_json(path, 1);
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "direction", dir);
    cJSON_AddStringToObject(e, "body", body);
    cJSON_AddNumberToObject(e, "ts", ts);
    cJSON_AddNumberToObject(e, "read", read);
    cJSON_AddItemToArray(arr, e);
    save_json(path, arr);
    cJSON_Delete(arr);
    send_ok(fd, id);
}

static void h_sms_list(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    char path[160];
    if (!number || number_file(number, "sms.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    send_result(fd, id, load_json(path, 1));
}

static void h_mark_read(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    char path[160];
    if (!number || number_file(number, "sms.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    cJSON *arr = load_json(path, 1);
    int dirty = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        cJSON *r = cJSON_GetObjectItem(it, "read");
        if (cJSON_IsNumber(r) && r->valueint == 0) {
            cJSON_SetNumberValue(r, 1);
            dirty = 1;
        }
    }
    if (dirty) save_json(path, arr);
    cJSON_Delete(arr);
    send_ok(fd, id);
}

/* Thread summary across all number folders. */
static void h_sms_threads(int fd, int id, cJSON *params)
{
    (void)params;
    cJSON *out = cJSON_CreateArray();

    DIR *d = opendir(NUMBERS_DIR);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            char path[300];
            snprintf(path, sizeof(path), NUMBERS_DIR "/%s/sms.json", de->d_name);
            cJSON *arr = load_json(path, 1);
            int count = cJSON_GetArraySize(arr);
            if (count == 0) { cJSON_Delete(arr); continue; }

            int unread = 0;
            cJSON *it;
            cJSON_ArrayForEach(it, arr) {
                cJSON *r = cJSON_GetObjectItem(it, "read");
                if (cJSON_IsNumber(r) && r->valueint == 0) unread++;
            }
            cJSON *last = cJSON_GetArrayItem(arr, count - 1);
            cJSON *lb = cJSON_GetObjectItem(last, "body");
            cJSON *lt = cJSON_GetObjectItem(last, "ts");

            cJSON *t = cJSON_CreateObject();
            cJSON_AddStringToObject(t, "number", de->d_name);
            cJSON_AddStringToObject(t, "last_body",
                                    cJSON_IsString(lb) ? lb->valuestring : "");
            cJSON_AddNumberToObject(t, "last_ts",
                                    cJSON_IsNumber(lt) ? lt->valuedouble : 0);
            cJSON_AddNumberToObject(t, "unread", unread);
            cJSON_AddNumberToObject(t, "count", count);
            cJSON_AddItemToArray(out, t);
            cJSON_Delete(arr);
        }
        closedir(d);
    }

    /* sort by last_ts desc (simple insertion into new array) */
    int n = cJSON_GetArraySize(out);
    cJSON *sorted = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *best = NULL; int best_idx = -1; double best_ts = -1;
        for (int j = 0; j < cJSON_GetArraySize(out); j++) {
            cJSON *c = cJSON_GetArrayItem(out, j);
            double ts = param_num(c, "last_ts", 0);
            if (ts > best_ts) { best_ts = ts; best = c; best_idx = j; }
        }
        (void)best;
        if (best_idx >= 0)
            cJSON_AddItemToArray(sorted, cJSON_DetachItemFromArray(out, best_idx));
    }
    cJSON_Delete(out);
    send_result(fd, id, sorted);
}

/* ── history: calls ─────────────────────────────────────────────────────── */

static void h_add_call(int fd, int id, cJSON *params)
{
    const char *number  = param_str(params, "number");
    const char *dir     = param_str(params, "direction");
    const char *outcome = param_str(params, "outcome");
    if (!number || !dir || !outcome) {
        send_err(fd, id, "missing number/direction/outcome");
        return;
    }
    double ts  = param_num(params, "ts", (double)time(NULL));
    int    dur = (int)param_num(params, "duration", 0);

    char path[160];
    if (number_file(number, "calls.json", path, sizeof(path), 1) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    cJSON *arr = load_json(path, 1);
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "direction", dir);
    cJSON_AddStringToObject(e, "outcome", outcome);
    cJSON_AddNumberToObject(e, "ts", ts);
    cJSON_AddNumberToObject(e, "duration", dur);
    cJSON_AddItemToArray(arr, e);
    save_json(path, arr);
    cJSON_Delete(arr);
    send_ok(fd, id);
}

static void h_calls_list(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    char path[160];
    if (!number || number_file(number, "calls.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    send_result(fd, id, load_json(path, 1));
}

/* Merged recent calls across every number folder, newest first. */
static void h_calls_recent(int fd, int id, cJSON *params)
{
    int limit = (int)param_num(params, "limit", 50);
    if (limit <= 0 || limit > 500) limit = 50;

    cJSON *all = cJSON_CreateArray();
    DIR *d = opendir(NUMBERS_DIR);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            char path[300];
            snprintf(path, sizeof(path), NUMBERS_DIR "/%s/calls.json", de->d_name);
            cJSON *arr = load_json(path, 1);
            cJSON *it;
            cJSON_ArrayForEach(it, arr) {
                cJSON *e = cJSON_Duplicate(it, 1);
                cJSON_AddStringToObject(e, "number", de->d_name);
                cJSON_AddItemToArray(all, e);
            }
            cJSON_Delete(arr);
        }
        closedir(d);
    }

    /* selection-sort the newest `limit` entries into the result */
    cJSON *sorted = cJSON_CreateArray();
    for (int i = 0; i < limit && cJSON_GetArraySize(all) > 0; i++) {
        int best_idx = -1; double best_ts = -1;
        for (int j = 0; j < cJSON_GetArraySize(all); j++) {
            double ts = param_num(cJSON_GetArrayItem(all, j), "ts", 0);
            if (ts > best_ts) { best_ts = ts; best_idx = j; }
        }
        if (best_idx < 0) break;
        cJSON_AddItemToArray(sorted, cJSON_DetachItemFromArray(all, best_idx));
    }
    cJSON_Delete(all);
    send_result(fd, id, sorted);
}

/* ── settings ───────────────────────────────────────────────────────────── */

static void h_settings_get(int fd, int id, cJSON *params)
{
    const char *key = param_str(params, "key");
    cJSON *obj = load_json(SETTINGS_PATH, 0);
    if (key) {
        cJSON *v = cJSON_GetObjectItem(obj, key);
        send_result(fd, id, v ? cJSON_Duplicate(v, 1) : cJSON_CreateNull());
        cJSON_Delete(obj);
    } else {
        send_result(fd, id, obj);   /* whole object */
    }
}

static void h_settings_set(int fd, int id, cJSON *params)
{
    const char *key = param_str(params, "key");
    cJSON *val = params ? cJSON_GetObjectItem(params, "value") : NULL;
    if (!key || !val) { send_err(fd, id, "missing key or value"); return; }

    cJSON *obj = load_json(SETTINGS_PATH, 0);
    if (cJSON_GetObjectItem(obj, key))
        cJSON_ReplaceItemInObject(obj, key, cJSON_Duplicate(val, 1));
    else
        cJSON_AddItemToObject(obj, key, cJSON_Duplicate(val, 1));
    save_json(SETTINGS_PATH, obj);
    cJSON_Delete(obj);
    send_ok(fd, id);
}

static void h_ping(int fd, int id, cJSON *params)
{
    (void)params;
    send_result(fd, id, cJSON_CreateString("pong"));
}

/* ── dispatch ───────────────────────────────────────────────────────────── */

struct handler { const char *method; void (*fn)(int, int, cJSON *); };

static const struct handler TABLE[] = {
    { "ping",                 h_ping            },
    { "contacts.list",        h_contacts_list   },
    { "contacts.save",        h_contacts_save   },
    { "contacts.delete",      h_contacts_delete },
    { "history.add_sms",      h_add_sms         },
    { "history.sms_list",     h_sms_list        },
    { "history.sms_threads",  h_sms_threads     },
    { "history.mark_read",    h_mark_read       },
    { "history.add_call",     h_add_call        },
    { "history.calls_list",   h_calls_list      },
    { "history.calls_recent", h_calls_recent    },
    { "settings.get",         h_settings_get    },
    { "settings.set",         h_settings_set    },
    { NULL, NULL }
};

static void dispatch(int fd, const char *json)
{
    cJSON *req = cJSON_Parse(json);
    if (!req) { send_err(fd, 0, "invalid json"); return; }

    cJSON *id_item = cJSON_GetObjectItem(req, "id");
    int id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;

    cJSON *meth   = cJSON_GetObjectItem(req, "method");
    cJSON *params = cJSON_GetObjectItem(req, "params");
    if (!cJSON_IsString(meth)) {
        send_err(fd, id, "missing method");
        cJSON_Delete(req);
        return;
    }

    for (int i = 0; TABLE[i].method; i++) {
        if (strcmp(TABLE[i].method, meth->valuestring) == 0) {
            TABLE[i].fn(fd, id, params);
            cJSON_Delete(req);
            return;
        }
    }
    send_err(fd, id, "unknown method");
    cJSON_Delete(req);
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    ensure_dirs();

    int server_fd = ipc_server_listen(STORAGE_SOCK);
    if (server_fd < 0) {
        fprintf(stderr, "blackhand-storage: ipc_server_listen(%s) failed\n",
                STORAGE_SOCK);
        return 1;
    }
    fprintf(stderr, "blackhand-storage: listening on " STORAGE_SOCK "\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        while (1) {
            char buffer[BUFFER_SIZE];
            int n = recv_msg(client_fd, buffer, BUFFER_SIZE);
            if (n <= 0) break;
            dispatch(client_fd, buffer);
        }
        close(client_fd);
    }
    return 0;
}
