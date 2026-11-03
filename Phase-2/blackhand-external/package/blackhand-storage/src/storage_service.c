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
 *   contacts.list                          → [{name,number}] or {"error":...}
 *   contacts.save    {name,number}           upsert keyed by number
 *   contacts.delete  {number}
 *   contacts.clear                         → unconditionally resets
 *        contacts.json to []; used only by White Wipe. Unlike every other
 *        mutation here, this one is ALLOWED to clobber a corrupt file — an
 *        explicit user-confirmed wipe must not be defeated by corruption it
 *        cannot otherwise remove. Result is "ok", or "wiped_corrupt" if the
 *        file it just replaced could not be parsed.
 *   history.add_sms  {number,direction:"in"|"out",body,ts?,read?}
 *   history.sms_list {number}              → [{direction,body,ts,read}] or {"error":...}
 *   history.sms_threads                    → [{number,last_body,last_ts,unread,count}]
 *   history.mark_read {number}
 *   history.add_call {number,direction,outcome,ts?,duration?}
 *        outcome: "answered"|"missed"|"busy"|"rejected"|"no_answer"|"failed"
 *   history.calls_list   {number}          → [{direction,outcome,ts,duration}] or {"error":...}
 *   history.calls_recent {limit?}          → [{number,direction,outcome,ts,duration}]
 *        limit is clamped to CALLS_RECENT_MAX (below) — the IPC transport
 *        cannot deliver an unbounded response.
 *   settings.get {key} / settings.set {key,value}
 *
 * SMS entry:  { "direction":"in", "body":"...", "ts":1718000000, "read":0 }
 * Call entry: { "direction":"out", "outcome":"answered", "ts":..., "duration":62 }
 *
 * Corrupt-file reads: contacts.list / history.sms_list / history.calls_list
 * each read exactly one file the caller asked about. If that file exists but
 * fails to parse, an empty array would look identical to "no data yet" —
 * silent data loss to the user. These three reply with a JSON-RPC error
 * ("... file unreadable") instead, same as the mutation handlers already do.
 * history.sms_threads and history.calls_recent are different: they merge
 * EVERY number's file into one list. One corrupt folder there is skipped
 * (still logged by load_json) rather than turning the whole merged view into
 * an error, which would hide every other number's healthy history too.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "ipc.h"
#include "cJSON.h"

#define STORAGE_SOCK   "/run/bh-storage.sock"
#ifndef STORAGE_DATA_DIR
#define STORAGE_DATA_DIR "/data"
#endif
#define CONTACTS_DIR   STORAGE_DATA_DIR "/contacts"
#define CONTACTS_PATH  CONTACTS_DIR "/contacts.json"
#define NUMBERS_DIR    STORAGE_DATA_DIR "/numbers"
#define SETTINGS_PATH  STORAGE_DATA_DIR "/settings.json"
#define BUFFER_SIZE    32768

/*
 * History caps — the IPC transport (ipc.c recv_msg) rejects any message
 * >= BUFFER_SIZE (32768) bytes. h_sms_list / h_calls_list return a whole
 * per-number file with no limit param, so the cap has to live at write
 * time (oldest entries dropped first) or that file eventually becomes
 * undeliverable forever.
 *
 * SMS entry, minified JSON: {"direction":"out","body":"...","ts":1234567890,"read":1}
 *   fixed overhead ~54 bytes (measured: literal text minus body content)
 *   + body. blackhand-modem's own buffers cap a body at 255 bytes
 *   (Modem.c PendingSms.body[160], urc.c/modem_dispatch.c body[256]) — use
 *   that as the realistic worst case.
 *   entry <= 54 + 255 + 1 (array comma) ~= 310 bytes.
 *   SMS_HISTORY_MAX(60) * 310 ~= 18600 bytes = 57% of 32768 — comfortable
 *   margin for the JSON-RPC envelope and any entry a bit larger than assumed.
 *
 * Call entry: {"direction":"out","outcome":"answered","ts":1234567890,"duration":600}
 *   worst case (outcome "no_answer", 5-digit duration) ~= 74 bytes + 1 comma
 *   = 75 bytes/entry (this matches the reported empirical break point of
 *   ~400-437 calls = 32768/75).
 *   CALLS_HISTORY_MAX(200) * 75 = 15000 bytes = 46% of 32768.
 */
#define SMS_HISTORY_MAX    60
#define CALLS_HISTORY_MAX  200

/*
 * history.calls_recent merges every number's calls.json and returns the
 * newest `limit` entries with an extra "number" field on each:
 *   {"direction":"out","outcome":"answered","ts":1234567890,"duration":600,"number":"+15551234567"}
 *   ~= 75 (call entry, see above) + 12 ("," + "number":"") + 20 (number,
 *   see number_dirname's own cap) + 1 (closing quote) ~= 108 bytes/entry.
 *   CALLS_RECENT_MAX(150) * 108 = 16200 bytes = 49% of 32768.
 * The previous ceiling of 500 was already documented as broken by
 * construction (500*108 > 32768) before any growth at all.
 */
#define CALLS_RECENT_MAX   150

/* A finite emergency allocation for atomic communication writes when media
 * fills /data. Refill only when free space is comfortably above the reserve. */
#define RESERVE_PATH STORAGE_DATA_DIR "/.communication-reserve"
#define RESERVE_BYTES (256 * 1024)
static void replenish_reserve(void)
{
    struct statvfs st;
    if (access(RESERVE_PATH, F_OK) == 0 || statvfs(STORAGE_DATA_DIR, &st) != 0 ||
        (unsigned long long)st.f_bavail * st.f_frsize < 2 * RESERVE_BYTES) return;
    int fd = open(RESERVE_PATH, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return;
    char zeros[4096] = {0}; int ok = 1;
    for (int n = 0; n < RESERVE_BYTES; n += sizeof(zeros)) {
        if (write(fd, zeros, sizeof(zeros)) != sizeof(zeros)) { ok = 0; break; }
    }
    if (fsync(fd) != 0) ok = 0;
    close(fd);
    if (!ok) unlink(RESERVE_PATH);
}

/* ── JSON file I/O (atomic) ─────────────────────────────────────────────── */

/*
 * Load a JSON file.
 *
 *   file absent / empty  → fresh empty array|object (a legitimate "no data yet"
 *                          state; safe to mutate and save back).
 *   present but bad      → still returns an empty array|object so read handlers
 *                          keep working, BUT sets *bad = 1. Mutation handlers
 *                          MUST check *bad and refuse to save, otherwise the
 *                          empty substitute would overwrite and destroy the
 *                          recoverable (but currently unparseable) file.
 *
 * `bad` may be NULL when the caller only reads and never writes back.
 */
static cJSON *load_json(const char *path, int want_array, int *bad)
{
    if (bad) *bad = 0;

    FILE *f = fopen(path, "r");
    if (!f) return want_array ? cJSON_CreateArray() : cJSON_CreateObject();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return want_array ? cJSON_CreateArray() : cJSON_CreateObject(); }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        /* Present file we could not read: treat as unsafe to overwrite. */
        fclose(f);
        if (bad) *bad = 1;
        return want_array ? cJSON_CreateArray() : cJSON_CreateObject();
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || (want_array ? !cJSON_IsArray(root) : !cJSON_IsObject(root))) {
        cJSON_Delete(root);
        /* Corrupt data must never look like "no data yet" with no trace.
         * Read-only callers pass bad=NULL and would otherwise see a silent
         * empty result. One line, unconditional, to stderr (captured by the
         * service manager into <service>.log). */
        fprintf(stderr, "blackhand-storage: corrupt/unreadable JSON in %s\n", path);
        if (bad) *bad = 1;   /* present but unparseable/wrong-type — do NOT overwrite */
        return want_array ? cJSON_CreateArray() : cJSON_CreateObject();
    }
    return root;
}

/* fsync the directory holding `path` so a rename survives power loss. */
static int fsync_parent_dir(const char *path)
{
    char dir[300];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return -1;
    if (slash == dir) slash[1] = '\0';   /* file at root: keep "/" */
    else *slash = '\0';

    int dfd = open(dir, O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return -1;
    int rc = fsync(dfd), saved = errno;
    close(dfd);
    errno = saved;
    return rc;
}

/*
 * Atomically replace `path` with the serialised `root`.
 *
 * Returns 0 on full success, -1 on any failure. On failure the temp file is
 * removed. Failures before rename preserve the original. A directory sync
 * failure after rename means publication succeeded but durability is unknown.
 */
static int save_json_once(const char *path, cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    if (!str) { errno = ENOMEM; return -1; }

    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) { free(str); return -1; }
    fchmod(fileno(f), 0600);   /* user data — owner-only, regardless of umask */

    int saved = 0;
    if (fputs(str, f) < 0) saved = errno;
    if (!saved && fflush(f) != 0) saved = errno;
    if (!saved && fsync(fileno(f)) != 0) saved = errno;
    if (fclose(f) != 0 && !saved) saved = errno;
    free(str);
    if (saved) { unlink(tmp); errno = saved; return -1; }
    if (rename(tmp, path) != 0) {
        saved = errno; unlink(tmp); errno = saved; return -1;
    }
    return fsync_parent_dir(path);

}

static int save_json(const char *path, cJSON *root)
{
    if (save_json_once(path, root) == 0) return 0;
    if (errno != ENOSPC || unlink(RESERVE_PATH) != 0) return -1;
    fprintf(stderr, "blackhand-storage: emergency reserve consumed; free storage now\n");
    return save_json_once(path, root);
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
    /* 0700: /data/numbers holds phone numbers, /data/contacts holds names —
     * user data. Owner-only. */
    mkdir(STORAGE_DATA_DIR, 0700);
    mkdir(CONTACTS_DIR, 0700);
    mkdir(NUMBERS_DIR, 0700);
}

/* Path to a number's sms.json/calls.json, creating the folder on demand. */
static int number_file(const char *number, const char *fname,
                       char *out, size_t out_sz, int create)
{
    char dir[32];
    if (number_dirname(number, dir, sizeof(dir)) != 0) return -1;
    char dpath[128];
    snprintf(dpath, sizeof(dpath), NUMBERS_DIR "/%s", dir);
    if (create) mkdir(dpath, 0700);   /* per-number folder — owner-only, see ensure_dirs() */
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
    int bad = 0;
    cJSON *arr = load_json(CONTACTS_PATH, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "contacts file unreadable"); return; }
    send_result(fd, id, arr);
}

static void h_contacts_save(int fd, int id, cJSON *params)
{
    const char *name   = param_str(params, "name");
    const char *number = param_str(params, "number");
    if (!name || !number) { send_err(fd, id, "missing name or number"); return; }

    int bad = 0;
    cJSON *arr = load_json(CONTACTS_PATH, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "contacts file unreadable"); return; }
    const char *original = param_str(params, "original_number");
    cJSON *target = NULL, *it;
    cJSON_ArrayForEach(it, arr) {
        cJSON *n = cJSON_GetObjectItem(it, "number");
        if (!cJSON_IsString(n)) continue;
        if (original && strcmp(n->valuestring, original) == 0) target = it;
        if (strcmp(n->valuestring, number) == 0 &&
            (!original || strcmp(original, number) != 0)) {
            cJSON_Delete(arr); send_err(fd, id, "number already belongs to a contact"); return;
        }
    }
    if (original && !target) {
        cJSON_Delete(arr); send_err(fd, id, "contact no longer exists"); return;
    }
    if (target) {
        cJSON_ReplaceItemInObject(target, "name", cJSON_CreateString(name));
        cJSON_ReplaceItemInObject(target, "number", cJSON_CreateString(number));
    } else {
        target = cJSON_CreateObject();
        cJSON_AddStringToObject(target, "name", name);
        cJSON_AddStringToObject(target, "number", number);
        cJSON_AddItemToArray(arr, target);
    }
    int rc = save_json(CONTACTS_PATH, arr);
    cJSON_Delete(arr);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
}

static void h_contacts_delete(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    if (!number) { send_err(fd, id, "missing number"); return; }

    int bad = 0;
    cJSON *arr = load_json(CONTACTS_PATH, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "contacts file unreadable"); return; }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        cJSON *num = cJSON_GetObjectItem(it, "number");
        if (cJSON_IsString(num) && strcmp(num->valuestring, number) == 0) {
            cJSON_DeleteItemFromArray(arr, i);
            break;
        }
    }
    int rc = save_json(CONTACTS_PATH, arr);
    cJSON_Delete(arr);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
}

/*
 * White Wipe's only way to empty contacts.json. Every other write handler
 * above refuses to touch a corrupt file (bad → send_err, no save) so an
 * unreadable file is never silently destroyed by an ordinary edit. A wipe is
 * different: it is an explicit, user-confirmed request to erase everything,
 * so a corrupt file must not survive it just because save_contact()/delete()
 * cannot enumerate its contents. Report "wiped_corrupt" so the caller can
 * tell the user the wipe found unreadable data, without treating the wipe
 * itself as failed — the file IS gone.
 */
static void h_contacts_clear(int fd, int id, cJSON *params)
{
    (void)params;
    int bad = 0;
    cJSON *old = load_json(CONTACTS_PATH, 1, &bad);
    cJSON_Delete(old);
    cJSON *empty = cJSON_CreateArray();
    int rc = save_json(CONTACTS_PATH, empty);
    cJSON_Delete(empty);
    if (rc != 0) { send_err(fd, id, "write failed"); return; }
    send_result(fd, id, cJSON_CreateString(bad ? "wiped_corrupt" : "ok"));
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
    int bad = 0;
    cJSON *arr = load_json(path, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "sms file unreadable"); return; }
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "direction", dir);
    cJSON_AddStringToObject(e, "body", body);
    cJSON_AddNumberToObject(e, "ts", ts);
    cJSON_AddNumberToObject(e, "read", read);
    cJSON_AddItemToArray(arr, e);
    /* Drop oldest first (index 0 = oldest, entries are appended). Loop, not
     * a single delete, so a file left over-cap by an older build self-heals
     * on its next write. */
    while (cJSON_GetArraySize(arr) > SMS_HISTORY_MAX)
        cJSON_DeleteItemFromArray(arr, 0);
    int rc = save_json(path, arr);
    cJSON_Delete(arr);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
}

static void h_sms_list(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    char path[160];
    if (!number || number_file(number, "sms.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    int bad = 0;
    cJSON *arr = load_json(path, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "sms file unreadable"); return; }
    send_result(fd, id, arr);
}

static void h_mark_read(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    char path[160];
    if (!number || number_file(number, "sms.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    int bad = 0;
    cJSON *arr = load_json(path, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "sms file unreadable"); return; }
    int dirty = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        cJSON *r = cJSON_GetObjectItem(it, "read");
        if (cJSON_IsNumber(r) && r->valueint == 0) {
            cJSON_SetNumberValue(r, 1);
            dirty = 1;
        }
    }
    int rc = dirty ? save_json(path, arr) : 0;
    cJSON_Delete(arr);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
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
            /* bad=NULL is intentional here, not the read/write asymmetry bug:
             * this merges every number's file, so a corrupt one just drops
             * that thread from the inbox (load_json already logged it to
             * stderr) instead of hiding every other number's messages too. */
            cJSON *arr = load_json(path, 1, NULL);
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
    int bad = 0;
    cJSON *arr = load_json(path, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "calls file unreadable"); return; }
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "direction", dir);
    cJSON_AddStringToObject(e, "outcome", outcome);
    cJSON_AddNumberToObject(e, "ts", ts);
    cJSON_AddNumberToObject(e, "duration", dur);
    cJSON_AddItemToArray(arr, e);
    /* Drop oldest first — see SMS_HISTORY_MAX comment above. */
    while (cJSON_GetArraySize(arr) > CALLS_HISTORY_MAX)
        cJSON_DeleteItemFromArray(arr, 0);
    int rc = save_json(path, arr);
    cJSON_Delete(arr);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
}

static void h_calls_delete_entry(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    double ts = param_num(params, "ts", -1.0);
    if (!number || ts < 0) { send_err(fd, id, "missing number or ts"); return; }

    char path[160];
    if (number_file(number, "calls.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number"); return;
    }
    int bad = 0;
    cJSON *arr = load_json(path, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "calls file unreadable"); return; }
    int n = cJSON_GetArraySize(arr);
    int removed = 0;
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        double entry_ts = param_num(it, "ts", -1.0);
        if (entry_ts >= 0 && entry_ts == ts) {
            cJSON_DeleteItemFromArray(arr, i);
            removed = 1;
            break;
        }
    }
    int rc = removed ? save_json(path, arr) : 0;
    cJSON_Delete(arr);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
}

static void h_messages_delete_thread(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    if (!number) { send_err(fd, id, "missing number"); return; }

    char path[160];
    if (number_file(number, "sms.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number"); return;
    }
    cJSON *empty = cJSON_CreateArray();
    int rc = save_json(path, empty);
    cJSON_Delete(empty);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
}

static void h_calls_list(int fd, int id, cJSON *params)
{
    const char *number = param_str(params, "number");
    char path[160];
    if (!number || number_file(number, "calls.json", path, sizeof(path), 0) != 0) {
        send_err(fd, id, "bad number");
        return;
    }
    int bad = 0;
    cJSON *arr = load_json(path, 1, &bad);
    if (bad) { cJSON_Delete(arr); send_err(fd, id, "calls file unreadable"); return; }
    send_result(fd, id, arr);
}

/* Merged recent calls across every number folder, newest first. */
static void h_calls_recent(int fd, int id, cJSON *params)
{
    int limit = (int)param_num(params, "limit", 50);
    if (limit <= 0) limit = 50;
    /* Clamp, don't silently substitute an unrelated default — see
     * CALLS_RECENT_MAX comment above for why 150 is the ceiling. */
    if (limit > CALLS_RECENT_MAX) limit = CALLS_RECENT_MAX;

    cJSON *all = cJSON_CreateArray();
    DIR *d = opendir(NUMBERS_DIR);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            char path[300];
            snprintf(path, sizeof(path), NUMBERS_DIR "/%s/calls.json", de->d_name);
            /* bad=NULL intentional — see the matching comment in
             * h_sms_threads above; this is a merge across every number. */
            cJSON *arr = load_json(path, 1, NULL);
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
    cJSON *obj = load_json(SETTINGS_PATH, 0, NULL);
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

    int bad = 0;
    cJSON *obj = load_json(SETTINGS_PATH, 0, &bad);
    if (bad) { cJSON_Delete(obj); send_err(fd, id, "settings file unreadable"); return; }
    if (cJSON_GetObjectItem(obj, key))
        cJSON_ReplaceItemInObject(obj, key, cJSON_Duplicate(val, 1));
    else
        cJSON_AddItemToObject(obj, key, cJSON_Duplicate(val, 1));
    int rc = save_json(SETTINGS_PATH, obj);
    cJSON_Delete(obj);
    if (rc != 0) send_err(fd, id, "write failed");
    else send_ok(fd, id);
}

/* Only storage owns communication files. Do not emulate a wipe in UI caches.
 * Refuse unexpected directories; never follow a symlink outside this tree. */
static int remove_history(void)
{
    DIR *numbers = opendir(NUMBERS_DIR);
    if (!numbers) return errno == ENOENT ? 0 : -1;
    int ok = 1;
    struct dirent *number;
    while ((number = readdir(numbers))) {
        if (!strcmp(number->d_name, ".") || !strcmp(number->d_name, "..")) continue;
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/%s", NUMBERS_DIR, number->d_name);
        struct stat st;
        if (lstat(dir, &st) != 0) { ok = 0; continue; }
        if (!S_ISDIR(st.st_mode)) {
            if (unlink(dir) != 0) ok = 0;
            continue;
        }
        DIR *entries = opendir(dir);
        if (!entries) { ok = 0; continue; }
        struct dirent *entry;
        while ((entry = readdir(entries))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
            if (unlink(path) != 0) ok = 0;
        }
        closedir(entries);
        if (rmdir(dir) != 0) ok = 0;
    }
    closedir(numbers);
    if (fsync_parent_dir(NUMBERS_DIR "/entry") != 0) ok = 0;
    return ok ? 0 : -1;
}

static void h_history_clear(int fd, int id, cJSON *params)
{
    (void)params;
    if (remove_history() != 0) send_err(fd, id, "history wipe incomplete");
    else send_ok(fd, id);
}

static void h_ping(int fd, int id, cJSON *params)
{
    (void)params;
    send_result(fd, id, cJSON_CreateString("pong"));
}

/* ── dispatch ───────────────────────────────────────────────────────────── */

struct handler { const char *method; void (*fn)(int, int, cJSON *); };

static const struct handler TABLE[] = {
    { "history.clear",                 h_history_clear         },
    { "ping",                          h_ping                  },
    { "contacts.list",                 h_contacts_list         },
    { "contacts.save",                 h_contacts_save         },
    { "contacts.delete",               h_contacts_delete       },
    { "contacts.clear",                h_contacts_clear        },
    { "history.add_sms",               h_add_sms              },
    { "history.sms_list",              h_sms_list             },
    { "history.sms_threads",           h_sms_threads          },
    { "history.mark_read",             h_mark_read            },
    { "history.add_call",              h_add_call             },
    { "history.calls_list",            h_calls_list           },
    { "history.calls_recent",          h_calls_recent         },
    { "history.calls_delete_entry",    h_calls_delete_entry   },
    { "history.messages_delete_thread",h_messages_delete_thread},
    { "settings.get",                  h_settings_get         },
    { "settings.set",                  h_settings_set         },
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
    /* Everything this process creates (dirs and JSON files) holds user data.
     * An inherited umask could otherwise defeat the explicit 0700/0600 modes
     * below (e.g. a 0000 umask would leave fopen()'d files at their default
     * 0666). 0077 makes owner-only the floor regardless of what was inherited. */
    umask(0077);

    ensure_dirs();
    replenish_reserve();

    int server_fd = ipc_server_listen(STORAGE_SOCK);
    if (server_fd < 0) {
        fprintf(stderr, "blackhand-storage: ipc_server_listen(%s) failed\n",
                STORAGE_SOCK);
        return 1;
    }
    fprintf(stderr, "blackhand-storage: listening on " STORAGE_SOCK "\n");

    while (1) {
        int client_fd = ipc_server_accept(server_fd);
        if (client_fd < 0) continue;
        while (1) {
            char buffer[BUFFER_SIZE];
            int n = recv_msg(client_fd, buffer, BUFFER_SIZE);
            if (n <= 0) break;
            dispatch(client_fd, buffer);
        }
        close(client_fd);
        replenish_reserve();
    }
    return 0;
}
