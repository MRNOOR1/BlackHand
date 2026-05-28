#include "comm_service.h"
#include "ui-ipcs/storage_ipc.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define COMM_MAX_ITEMS 128
#define LIST_BUF_SIZE  32768

static CommCall    s_calls[COMM_MAX_ITEMS];
static size_t      s_call_count   = 0;

static CommMessage s_messages[COMM_MAX_ITEMS];
static size_t      s_message_count = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void shift_down_calls(size_t index)
{
    for (size_t i = index; i + 1 < s_call_count; i++)
        s_calls[i] = s_calls[i + 1];
}

static void shift_down_messages(size_t index)
{
    for (size_t i = index; i + 1 < s_message_count; i++)
        s_messages[i] = s_messages[i + 1];
}

static const char *call_icon(const char *type)
{
    if (type && strcmp(type, "incoming") == 0) return "↙";
    if (type && strcmp(type, "missed")   == 0) return "✗";
    return "↗";
}

static void format_time_now(char *buf, size_t sz)
{
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm))
        strftime(buf, sz, "%H:%M", &tm);
    else
        snprintf(buf, sz, "now");
}

/* ── init / shutdown ─────────────────────────────────────────────────────── */

void comm_service_init(void)
{
    s_call_count    = 0;
    s_message_count = 0;
    comm_service_sync();
}

void comm_service_shutdown(void) {}

/* ── sync from storage ────────────────────────────────────────────────────── */

void comm_service_sync(void)
{
    static char buf[LIST_BUF_SIZE];

    /* Load calls */
    s_call_count = 0;
    if (storage_ipc_calls_list(buf, sizeof(buf)) == 0) {
        cJSON *arr = cJSON_Parse(buf);
        if (arr && cJSON_IsArray(arr)) {
            cJSON *item;
            cJSON_ArrayForEach(item, arr) {
                if (s_call_count >= COMM_MAX_ITEMS) break;
                CommCall *c = &s_calls[s_call_count];
                memset(c, 0, sizeof(*c));

                cJSON *n  = cJSON_GetObjectItem(item, "caller_name");
                cJSON *ph = cJSON_GetObjectItem(item, "phone");
                cJSON *ty = cJSON_GetObjectItem(item, "call_type");
                cJSON *ts = cJSON_GetObjectItem(item, "timestamp");
                cJSON *du = cJSON_GetObjectItem(item, "duration_sec");
                cJSON *id = cJSON_GetObjectItem(item, "id");

                snprintf(c->name, sizeof(c->name), "%s",
                         cJSON_IsString(n)  ? n->valuestring  : "Unknown");
                snprintf(c->phone, sizeof(c->phone), "%s",
                         cJSON_IsString(ph) ? ph->valuestring : "");
                snprintf(c->type, sizeof(c->type), "%s",
                         cJSON_IsString(ty) ? ty->valuestring : "outgoing");
                snprintf(c->time, sizeof(c->time), "%s",
                         cJSON_IsString(ts) ? ts->valuestring : "");
                snprintf(c->icon, sizeof(c->icon), "%s", call_icon(c->type));
                c->duration_sec = cJSON_IsNumber(du) ? du->valueint : 0;
                c->storage_id   = cJSON_IsNumber(id) ? id->valueint : 0;
                s_call_count++;
            }
        }
        cJSON_Delete(arr);
    }

    /* Load messages */
    s_message_count = 0;
    if (storage_ipc_messages_list(buf, sizeof(buf)) == 0) {
        cJSON *arr = cJSON_Parse(buf);
        if (arr && cJSON_IsArray(arr)) {
            cJSON *item;
            cJSON_ArrayForEach(item, arr) {
                if (s_message_count >= COMM_MAX_ITEMS) break;
                CommMessage *m = &s_messages[s_message_count];
                memset(m, 0, sizeof(*m));

                cJSON *sn = cJSON_GetObjectItem(item, "sender");
                cJSON *ph = cJSON_GetObjectItem(item, "phone");
                cJSON *bo = cJSON_GetObjectItem(item, "body");
                cJSON *ts = cJSON_GetObjectItem(item, "timestamp");
                cJSON *og = cJSON_GetObjectItem(item, "is_outgoing");
                cJSON *rd = cJSON_GetObjectItem(item, "read");
                cJSON *id = cJSON_GetObjectItem(item, "id");

                snprintf(m->sender, sizeof(m->sender), "%s",
                         cJSON_IsString(sn) ? sn->valuestring : "Unknown");
                snprintf(m->phone, sizeof(m->phone), "%s",
                         cJSON_IsString(ph) ? ph->valuestring : "");
                snprintf(m->body, sizeof(m->body), "%s",
                         cJSON_IsString(bo) ? bo->valuestring : "");
                snprintf(m->stamp, sizeof(m->stamp), "%s",
                         cJSON_IsString(ts) ? ts->valuestring : "");
                m->is_outgoing = cJSON_IsNumber(og) ? og->valueint : 0;
                m->read        = cJSON_IsNumber(rd) ? rd->valueint : 0;
                m->storage_id  = cJSON_IsNumber(id) ? id->valueint : 0;
                s_message_count++;
            }
        }
        cJSON_Delete(arr);
    }
}

/* ── calls ────────────────────────────────────────────────────────────────── */

size_t comm_service_call_count(void) { return s_call_count; }

const CommCall *comm_service_call_at(size_t index)
{
    return (index < s_call_count) ? &s_calls[index] : NULL;
}

int comm_service_call_add(const char *name, const char *type)
{
    return comm_service_call_add_full(name, "", type, 0);
}

int comm_service_call_add_full(const char *name, const char *phone,
                                const char *type, int duration_sec)
{
    if (s_call_count >= COMM_MAX_ITEMS) return -1;

    /* Shift existing entries right to put newest at index 0 */
    for (size_t i = s_call_count; i > 0; i--)
        s_calls[i] = s_calls[i - 1];

    CommCall *c = &s_calls[0];
    memset(c, 0, sizeof(*c));
    snprintf(c->name,  sizeof(c->name),  "%s", name  ? name  : "Unknown");
    snprintf(c->phone, sizeof(c->phone), "%s", phone ? phone : "");
    snprintf(c->type,  sizeof(c->type),  "%s", type  ? type  : "outgoing");
    snprintf(c->icon,  sizeof(c->icon),  "%s", call_icon(type));
    format_time_now(c->time, sizeof(c->time));
    c->duration_sec = duration_sec;

    /* Persist to storage */
    c->storage_id = storage_ipc_calls_add(
        c->phone, c->name, c->type, duration_sec);

    s_call_count++;
    return 0;
}

int comm_service_call_delete(size_t index)
{
    if (index >= s_call_count) return -1;
    if (s_calls[index].storage_id > 0)
        storage_ipc_calls_delete(s_calls[index].storage_id);
    shift_down_calls(index);
    s_call_count--;
    return 0;
}

/* ── messages ─────────────────────────────────────────────────────────────── */

size_t comm_service_message_count(void) { return s_message_count; }

const CommMessage *comm_service_message_at(size_t index)
{
    return (index < s_message_count) ? &s_messages[index] : NULL;
}

int comm_service_message_add(const char *sender, const char *body)
{
    return comm_service_message_add_full(sender, "", body, 0);
}

int comm_service_message_add_full(const char *sender, const char *phone,
                                   const char *body, int is_outgoing)
{
    if (s_message_count >= COMM_MAX_ITEMS) return -1;

    for (size_t i = s_message_count; i > 0; i--)
        s_messages[i] = s_messages[i - 1];

    CommMessage *m = &s_messages[0];
    memset(m, 0, sizeof(*m));
    snprintf(m->sender, sizeof(m->sender), "%s", sender ? sender : "Unknown");
    snprintf(m->phone,  sizeof(m->phone),  "%s", phone  ? phone  : "");
    snprintf(m->body,   sizeof(m->body),   "%s", body   ? body   : "");
    format_time_now(m->stamp, sizeof(m->stamp));
    m->is_outgoing = is_outgoing;
    m->read        = is_outgoing ? 1 : 0;

    m->storage_id = storage_ipc_messages_add(
        m->phone, m->sender, m->body, is_outgoing);

    s_message_count++;
    return 0;
}

int comm_service_message_delete(size_t index)
{
    if (index >= s_message_count) return -1;
    if (s_messages[index].storage_id > 0)
        storage_ipc_messages_delete(s_messages[index].storage_id);
    shift_down_messages(index);
    s_message_count--;
    return 0;
}

void comm_service_reset(void)
{
    s_call_count    = 0;
    s_message_count = 0;
}
