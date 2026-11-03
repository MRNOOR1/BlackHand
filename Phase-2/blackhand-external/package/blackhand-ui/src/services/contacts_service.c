/*
 * contacts_service.c — name⇄number lookup table backed by blackhand-storage.
 *
 * Contacts are ONE file (/data/contacts/contacts.json) owned by the storage
 * service; this module is a thin cache + mutation client. A contact's id IS
 * its E.164 number — the number is the primary key across the whole system
 * (history folders under /data/numbers/ are keyed by it too).
 *
 * The public API (contact_service_*) is unchanged from the old file-per-
 * contact implementation, so screens compile untouched.
 */
#include "contacts_service.h"
#include "ui-ipcs/storage_ipc.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_CONTACTS 256
#define LIST_BUF     32768

static Contact  s_contacts[MAX_CONTACTS];
static Contact *s_ptrs[MAX_CONTACTS];
static size_t   s_count = 0;

/* 1 = the last sync found contacts.json present but unparseable. The cache
 * is left untouched (whatever it held before), matching comm_service's
 * storage_ok precedent: "no data" and "can't read the data" must look
 * different to the user, not both collapse to an empty list. */
static int s_corrupt = 0;

/* ── cache management ────────────────────────────────────────────────────── */

static void free_cache(void)
{
    for (size_t i = 0; i < s_count; i++) {
        free(s_contacts[i].id);
        free(s_contacts[i].name);
        free(s_contacts[i].phone_number);
    }
    s_count = 0;
}

static int by_name(const void *a, const void *b)
{
    const Contact *ca = *(const Contact * const *)a;
    const Contact *cb = *(const Contact * const *)b;
    return strcasecmp(ca->name ? ca->name : "", cb->name ? cb->name : "");
}

static void sync_from_storage(void)
{
    free_cache();

    static char buf[LIST_BUF];
    int rc = storage_ipc_contacts_list(buf, sizeof(buf));
    s_corrupt = (rc == -2);
    if (rc != 0) return;

    cJSON *arr = cJSON_Parse(buf);
    if (!arr || !cJSON_IsArray(arr)) { cJSON_Delete(arr); return; }

    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (s_count >= MAX_CONTACTS) break;
        cJSON *n   = cJSON_GetObjectItem(it, "name");
        cJSON *num = cJSON_GetObjectItem(it, "number");
        if (!cJSON_IsString(num)) continue;

        Contact *c = &s_contacts[s_count];
        c->id           = strdup(num->valuestring);   /* number IS the id */
        c->name         = strdup(cJSON_IsString(n) ? n->valuestring : "Unknown");
        c->phone_number = strdup(num->valuestring);
        if (!c->id || !c->name || !c->phone_number) {
            free(c->id); free(c->name); free(c->phone_number);
            break;
        }
        s_ptrs[s_count] = c;
        s_count++;
    }
    cJSON_Delete(arr);

    qsort(s_ptrs, s_count, sizeof(s_ptrs[0]), by_name);
}

/* ── public API ──────────────────────────────────────────────────────────── */

void contact_service_init(void)
{
    sync_from_storage();
}

void contact_service_shutdown(void)
{
    free_cache();
}

/* 1 = contacts.json exists but the daemon could not parse it. Screens use
 * this the same way comm_service_storage_ok() is used for history. */
int contact_service_data_corrupt(void)
{
    return s_corrupt;
}

/* White Wipe only: unconditionally empties contacts.json, including a
 * corrupt one contact_service_delete() can never reach (it works off the
 * cache, which contacts.list — correctly — refuses to fake data for).
 * Returns -1 on failure, 0 clean, 1 if corrupt data was found and erased. */
int contact_service_wipe_all(void)
{
    int rc = storage_ipc_contacts_clear();
    if (rc < 0) return -1;
    free_cache();
    s_corrupt = 0;
    return rc;
}

Contact *contact_service_create(const char *name, const char *number)
{
    if (!number || !number[0]) return NULL;
    if (storage_ipc_contacts_save(name && name[0] ? name : "Unknown",
                                  number) != 0)
        return NULL;
    sync_from_storage();
    /* return the cached entry so callers can use it immediately */
    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_contacts[i].phone_number, number) == 0)
            return &s_contacts[i];
    return NULL;
}

int contact_service_update(Contact *c, const char *new_name,
                           const char *new_number)
{
    if (!c || !c->phone_number) return -1;
    const char *name   = (new_name   && new_name[0])   ? new_name   : c->name;
    const char *number = (new_number && new_number[0]) ? new_number : c->phone_number;

    /* Renumber and rename in one storage transaction, with collision checks. */
    if (storage_ipc_contacts_update(c->phone_number, name, number) != 0) return -1;
    sync_from_storage();
    return 0;
}

int contact_service_delete(const char *id)
{
    if (!id) return -1;
    if (storage_ipc_contacts_delete(id) != 0) return -1;
    sync_from_storage();
    return 0;
}

const Contact **contact_service_list_all(size_t *count)
{
    if (count) *count = s_count;
    return (const Contact **)s_ptrs;
}

const Contact *contact_service_get_by_id(const char *id)
{
    if (!id) return NULL;
    for (size_t i = 0; i < s_count; i++)
        if (s_contacts[i].id && strcmp(s_contacts[i].id, id) == 0)
            return &s_contacts[i];
    return NULL;
}
