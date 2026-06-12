/*
 * comm_service.c — read cache over blackhand-storage's per-number history.
 * See comm_service.h for the model.
 */
#include "comm_service.h"
#include "contacts_service.h"
#include "ui-ipcs/storage_ipc.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define COMM_MAX_ITEMS   128
#define THREAD_MAX_ITEMS 200
#define LIST_BUF_SIZE    32768

static CommCall    s_calls[COMM_MAX_ITEMS];
static size_t      s_call_count = 0;

static CommMessage s_threads[COMM_MAX_ITEMS];   /* thread list (inbox) */
static size_t      s_thread_count = 0;

static CommMessage s_conv[THREAD_MAX_ITEMS];    /* open conversation */
static size_t      s_conv_count = 0;
static char        s_conv_number[32] = "";

/* 1 = last sync reached the storage daemon; 0 = it is down/unreachable.
 * Screens use this to show a STORAGE OFFLINE banner instead of a silently
 * empty list — empty-because-no-data and empty-because-no-daemon must look
 * different to the user. */
static int s_storage_ok = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* type as shown in the call log; derived from direction + outcome */
static void call_type(const char *dir, const char *outcome,
                      char *out, size_t out_sz)
{
    if (outcome && (strcmp(outcome, "missed")    == 0 ||
                    strcmp(outcome, "rejected")  == 0 ||
                    strcmp(outcome, "busy")      == 0 ||
                    strcmp(outcome, "no_answer") == 0)) {
        snprintf(out, out_sz, "%s", outcome);
        return;
    }
    snprintf(out, out_sz, "%s",
             (dir && strcmp(dir, "in") == 0) ? "incoming" : "outgoing");
}

static const char *call_icon(const char *type)
{
    if (strcmp(type, "incoming")  == 0) return "↙";
    if (strcmp(type, "outgoing")  == 0) return "↗";
    return "✗";   /* missed / rejected / busy / no_answer */
}

static void format_ts(double ts, char *buf, size_t sz)
{
    time_t t = (time_t)ts;
    if (t <= 0) { snprintf(buf, sz, "-"); return; }
    struct tm tm;
    if (!localtime_r(&t, &tm)) { snprintf(buf, sz, "-"); return; }

    time_t now = time(NULL);
    struct tm tn;
    localtime_r(&now, &tn);
    if (tm.tm_year == tn.tm_year && tm.tm_yday == tn.tm_yday)
        strftime(buf, sz, "%H:%M", &tm);          /* today: time only */
    else
        strftime(buf, sz, "%d/%m %H:%M", &tm);
}

/* Two numbers refer to the same line if their last 9 digits match — covers
 * "+61413805252" vs "0413805252" vs "61413805252" (CLIP type 129 vs 145). */
static int same_line(const char *a, const char *b)
{
    char da[24], db[24];
    size_t na = 0, nb = 0;
    for (const char *p = a; *p && na < sizeof(da) - 1; p++)
        if (*p >= '0' && *p <= '9') da[na++] = *p;
    for (const char *p = b; *p && nb < sizeof(db) - 1; p++)
        if (*p >= '0' && *p <= '9') db[nb++] = *p;
    da[na] = db[nb] = '\0';
    if (na == 0 || nb == 0) return 0;
    size_t n = (na < nb) ? na : nb;
    if (n > 9) n = 9;
    return strcmp(da + na - n, db + nb - n) == 0;
}

/* Contact name for a number, or the number itself. */
static void resolve_name(const char *number, char *out, size_t out_sz)
{
    size_t count = 0;
    const Contact **all = contact_service_list_all(&count);
    for (size_t i = 0; i < count; i++) {
        if (all && all[i] && all[i]->phone_number &&
            same_line(all[i]->phone_number, number)) {
            snprintf(out, out_sz, "%s", all[i]->name ? all[i]->name : number);
            return;
        }
    }
    snprintf(out, out_sz, "%s", number);
}

static double obj_num(cJSON *o, const char *k, double dflt)
{
    cJSON *it = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(it) ? it->valuedouble : dflt;
}

static const char *obj_str(cJSON *o, const char *k, const char *dflt)
{
    cJSON *it = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(it) ? it->valuestring : dflt;
}

/* ── sync ───────────────────────────────────────────────────────────────── */

void comm_service_sync(void)
{
    static char buf[LIST_BUF_SIZE];

    /* recent calls (already merged + sorted by the storage service) */
    s_call_count = 0;
    s_storage_ok = (storage_ipc_ping() == 0);
    if (storage_ipc_calls_recent(COMM_MAX_ITEMS, buf, sizeof(buf)) == 0) {
        cJSON *arr = cJSON_Parse(buf);
        if (arr && cJSON_IsArray(arr)) {
            cJSON *item;
            cJSON_ArrayForEach(item, arr) {
                if (s_call_count >= COMM_MAX_ITEMS) break;
                CommCall *c = &s_calls[s_call_count];
                memset(c, 0, sizeof(*c));

                const char *number = obj_str(item, "number", "");
                snprintf(c->phone, sizeof(c->phone), "%s", number);
                resolve_name(number, c->name, sizeof(c->name));
                call_type(obj_str(item, "direction", "out"),
                          obj_str(item, "outcome", ""),
                          c->type, sizeof(c->type));
                snprintf(c->icon, sizeof(c->icon), "%s", call_icon(c->type));
                format_ts(obj_num(item, "ts", 0), c->time, sizeof(c->time));
                c->duration_sec = (int)obj_num(item, "duration", 0);
                s_call_count++;
            }
        }
        cJSON_Delete(arr);
    }

    /* SMS threads */
    s_thread_count = 0;
    if (storage_ipc_sms_threads(buf, sizeof(buf)) == 0) {
        cJSON *arr = cJSON_Parse(buf);
        if (arr && cJSON_IsArray(arr)) {
            cJSON *item;
            cJSON_ArrayForEach(item, arr) {
                if (s_thread_count >= COMM_MAX_ITEMS) break;
                CommMessage *m = &s_threads[s_thread_count];
                memset(m, 0, sizeof(*m));

                const char *number = obj_str(item, "number", "");
                snprintf(m->phone, sizeof(m->phone), "%s", number);
                resolve_name(number, m->sender, sizeof(m->sender));
                snprintf(m->body, sizeof(m->body), "%s",
                         obj_str(item, "last_body", ""));
                format_ts(obj_num(item, "last_ts", 0), m->stamp, sizeof(m->stamp));
                int unread     = (int)obj_num(item, "unread", 0);
                m->read        = (unread == 0);
                m->storage_id  = unread;        /* repurposed: unread count */
                m->is_outgoing = 0;
                s_thread_count++;
            }
        }
        cJSON_Delete(arr);
    }
}

int comm_service_storage_ok(void) { return s_storage_ok; }

void comm_service_init(void)     { comm_service_sync(); }
void comm_service_shutdown(void) {}
void comm_service_reset(void)    { s_call_count = 0; s_thread_count = 0; }

/* ── calls ──────────────────────────────────────────────────────────────── */

size_t comm_service_call_count(void) { return s_call_count; }

const CommCall *comm_service_call_at(size_t index)
{
    return (index < s_call_count) ? &s_calls[index] : NULL;
}

/* ── threads (inbox) ────────────────────────────────────────────────────── */

size_t comm_service_message_count(void) { return s_thread_count; }

const CommMessage *comm_service_message_at(size_t index)
{
    return (index < s_thread_count) ? &s_threads[index] : NULL;
}

/* ── open conversation ──────────────────────────────────────────────────── */

int comm_service_thread_open(const char *number)
{
    if (!number || !number[0]) return -1;

    static char buf[LIST_BUF_SIZE];
    s_conv_count = 0;
    snprintf(s_conv_number, sizeof(s_conv_number), "%s", number);

    if (storage_ipc_sms_list(number, buf, sizeof(buf)) != 0) return -1;

    char display[48];
    resolve_name(number, display, sizeof(display));

    cJSON *arr = cJSON_Parse(buf);
    if (!arr || !cJSON_IsArray(arr)) { cJSON_Delete(arr); return -1; }

    /* Keep the most recent THREAD_MAX_ITEMS, oldest→newest for display. */
    int total = cJSON_GetArraySize(arr);
    int start = (total > THREAD_MAX_ITEMS) ? total - THREAD_MAX_ITEMS : 0;
    for (int i = start; i < total; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        CommMessage *m = &s_conv[s_conv_count];
        memset(m, 0, sizeof(*m));

        const char *dir = obj_str(item, "direction", "in");
        m->is_outgoing = (strcmp(dir, "out") == 0);
        snprintf(m->sender, sizeof(m->sender), "%s",
                 m->is_outgoing ? "Me" : display);
        snprintf(m->phone, sizeof(m->phone), "%s", number);
        snprintf(m->body, sizeof(m->body), "%s", obj_str(item, "body", ""));
        format_ts(obj_num(item, "ts", 0), m->stamp, sizeof(m->stamp));
        m->read = (int)obj_num(item, "read", 1);
        s_conv_count++;
    }
    cJSON_Delete(arr);

    /* Opening a conversation reads it. */
    storage_ipc_mark_read(number);
    return 0;
}

size_t comm_service_thread_count(void) { return s_conv_count; }

const CommMessage *comm_service_thread_at(size_t index)
{
    return (index < s_conv_count) ? &s_conv[index] : NULL;
}

const char *comm_service_thread_number(void) { return s_conv_number; }

/* ── compatibility shims ────────────────────────────────────────────────── */
/* History writes happen in the modem service at the URC source. These shims
 * keep old call sites compiling; they just refresh the cache so the new
 * record (already written by the modem) becomes visible. */

int comm_service_call_add(const char *name, const char *type)
{
    (void)name; (void)type;
    comm_service_sync();
    return 0;
}

int comm_service_call_add_full(const char *name, const char *phone,
                                const char *type, int duration_sec)
{
    (void)name; (void)phone; (void)type; (void)duration_sec;
    comm_service_sync();
    return 0;
}

int comm_service_call_delete(size_t index)
{
    (void)index;
    return -1;   /* per-entry delete not supported in per-number layout */
}

int comm_service_message_add(const char *sender, const char *body)
{
    (void)sender; (void)body;
    comm_service_sync();
    return 0;
}

int comm_service_message_add_full(const char *sender, const char *phone,
                                   const char *body, int is_outgoing)
{
    (void)sender; (void)phone; (void)body; (void)is_outgoing;
    comm_service_sync();
    return 0;
}

int comm_service_message_delete(size_t index)
{
    (void)index;
    return -1;
}
