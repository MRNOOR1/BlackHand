/*
 * history_cap_selftest.c — self-test for the history write-time cap and the
 * history.calls_recent limit clamp. NOT part of the build (not referenced by
 * Makefile or blackhand-storage.mk — never ends up on the target).
 *
 * Exercises the exact algorithm used in storage_service.c's h_add_sms /
 * h_add_call ("while over cap, delete index 0") and h_calls_recent's limit
 * clamp, against the real cJSON, and checks the byte-size arithmetic from
 * the comment above SMS_HISTORY_MAX / CALLS_HISTORY_MAX / CALLS_RECENT_MAX
 * against worst-case entries.
 *
 * Run it (from Phase-2/blackhand-external/):
 *
 *   gcc -Wall -Wextra \
 *       -I package/blackhand-ipc/src \
 *       package/blackhand-ipc/src/cJSON.c \
 *       package/blackhand-storage/src/history_cap_selftest.c \
 *       -o /tmp/history_cap_selftest -lm \
 *   && /tmp/history_cap_selftest
 */

#include "cJSON.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mirror the constants and worst-case byte estimates from storage_service.c
 * (kept in sync manually — this file intentionally does not #include
 * storage_service.c, which owns main() and the socket server). */
#define SMS_HISTORY_MAX    60
#define CALLS_HISTORY_MAX  200
#define CALLS_RECENT_MAX   150
#define IPC_BUFFER_SIZE    32768

static int failures;
#define CHECK(cond) do {                                                  \
    if (cond) { printf("  ok   %s\n", #cond); }                           \
    else { printf("  FAIL %s  (line %d)\n", #cond, __LINE__); failures++; } \
} while (0)

/* Same trim algorithm as h_add_sms / h_add_call: drop oldest (index 0)
 * until at or under cap. */
static void apply_cap(cJSON *arr, int cap)
{
    while (cJSON_GetArraySize(arr) > cap)
        cJSON_DeleteItemFromArray(arr, 0);
}

static cJSON *make_sms_entry(int i, const char *worst_case_body)
{
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "direction", "out");
    cJSON_AddStringToObject(e, "body", worst_case_body);
    cJSON_AddNumberToObject(e, "ts", 1700000000 + i);
    cJSON_AddNumberToObject(e, "read", 1);
    return e;
}

static cJSON *make_call_entry(int i)
{
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "direction", "out");
    cJSON_AddStringToObject(e, "outcome", "no_answer");   /* longest outcome string */
    cJSON_AddNumberToObject(e, "ts", 1700000000 + i);
    cJSON_AddNumberToObject(e, "duration", 99999);        /* worst-case 5 digits */
    return e;
}

int main(void)
{
    /* ── 1. SMS cap keeps exactly SMS_HISTORY_MAX, drops the OLDEST ────── */
    {
        char body[256];
        memset(body, 'x', sizeof(body) - 1);
        body[sizeof(body) - 1] = '\0';   /* 255-byte worst case, see comment */

        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < SMS_HISTORY_MAX + 40; i++) {
            cJSON_AddItemToArray(arr, make_sms_entry(i, body));
            apply_cap(arr, SMS_HISTORY_MAX);   /* mirrors: cap after each append */
        }
        CHECK(cJSON_GetArraySize(arr) == SMS_HISTORY_MAX);

        /* oldest surviving entry must be the one added last-minus-(MAX-1),
         * i.e. entries 0..39 were dropped */
        cJSON *first = cJSON_GetArrayItem(arr, 0);
        cJSON *ts0 = cJSON_GetObjectItem(first, "ts");
        CHECK(cJSON_IsNumber(ts0) && ts0->valuedouble == 1700000000 + 40);

        cJSON *last = cJSON_GetArrayItem(arr, SMS_HISTORY_MAX - 1);
        cJSON *tsN = cJSON_GetObjectItem(last, "ts");
        CHECK(cJSON_IsNumber(tsN) &&
              tsN->valuedouble == 1700000000 + SMS_HISTORY_MAX + 40 - 1);

        /* the arithmetic in the comment: a full, capped file must fit
         * comfortably (well under) the 32 KB IPC ceiling */
        char *str = cJSON_PrintUnformatted(arr);
        size_t len = strlen(str);
        printf("  info: %d-entry worst-case SMS history = %zu bytes"
               " (%.0f%% of %d)\n",
               SMS_HISTORY_MAX, len, 100.0 * (double)len / IPC_BUFFER_SIZE,
               IPC_BUFFER_SIZE);
        CHECK(len < IPC_BUFFER_SIZE);
        CHECK(len < IPC_BUFFER_SIZE * 3 / 4);   /* "comfortable margin" */
        free(str);
        cJSON_Delete(arr);
    }

    /* ── 2. Call history cap: same algorithm, worst-case entries ────────── */
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < CALLS_HISTORY_MAX + 40; i++) {
            cJSON_AddItemToArray(arr, make_call_entry(i));
            apply_cap(arr, CALLS_HISTORY_MAX);
        }
        CHECK(cJSON_GetArraySize(arr) == CALLS_HISTORY_MAX);

        char *str = cJSON_PrintUnformatted(arr);
        size_t len = strlen(str);
        printf("  info: %d-entry worst-case call history = %zu bytes"
               " (%.0f%% of %d)\n",
               CALLS_HISTORY_MAX, len, 100.0 * (double)len / IPC_BUFFER_SIZE,
               IPC_BUFFER_SIZE);
        CHECK(len < IPC_BUFFER_SIZE);
        CHECK(len < IPC_BUFFER_SIZE * 3 / 4);
        free(str);
        cJSON_Delete(arr);
    }

    /* ── 3. calls_recent: CALLS_RECENT_MAX worst-case entries + "number" ── */
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < CALLS_RECENT_MAX; i++) {
            cJSON *e = make_call_entry(i);
            cJSON_AddStringToObject(e, "number", "+15551234567890123");  /* 20-char cap */
            cJSON_AddItemToArray(arr, e);
        }
        char *str = cJSON_PrintUnformatted(arr);
        size_t len = strlen(str);
        printf("  info: %d-entry worst-case calls_recent = %zu bytes"
               " (%.0f%% of %d)\n",
               CALLS_RECENT_MAX, len, 100.0 * (double)len / IPC_BUFFER_SIZE,
               IPC_BUFFER_SIZE);
        CHECK(len < IPC_BUFFER_SIZE);
        CHECK(len < IPC_BUFFER_SIZE * 3 / 4);
        free(str);
        cJSON_Delete(arr);
    }

    /* ── 4. limit clamp: mirror h_calls_recent's logic exactly ──────────── */
    {
        int cases[]    = { -5, 0, 1, 150, 151, 500, 100000 };
        int expected[] = { 50, 50, 1, 150, 150, 150, 150 };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            int limit = cases[i];
            if (limit <= 0) limit = 50;
            if (limit > CALLS_RECENT_MAX) limit = CALLS_RECENT_MAX;
            CHECK(limit == expected[i]);
        }
        /* the old ceiling (500) must no longer be reachable */
        CHECK(CALLS_RECENT_MAX < 500);
    }

    printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
