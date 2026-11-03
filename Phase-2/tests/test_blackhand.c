/*
 * test_blackhand.c — host unit tests for Black Hand OS pure-logic units.
 *
 * Covers the logic that needs no target hardware:
 *   - cJSON  : parse/serialize round-trip
 *   - country_codes : table lookups + E.164 normalisation
 *   - ipc    : send_msg/recv_msg framing over a socketpair, including the
 *              oversize-length rejection path and a short/partial read.
 *
 * No test framework: a tiny assert macro keeps this self-contained and
 * lightweight, per project rules. Run with `make check` in this directory.
 * The existing sources are compiled/linked directly (see Makefile) — they
 * are never copied here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "cJSON.h"
#include "country_codes.h"
#include "ipc.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);              \
        }                                                                    \
    } while (0)

/* ── cJSON: parse/serialize round-trip ───────────────────────────────────*/

static void test_cjson_roundtrip(void)
{
    printf("cJSON round-trip:\n");

    const char *in = "{\"jsonrpc\":\"2.0\",\"method\":\"play\",\"id\":7}";
    cJSON *root = cJSON_Parse(in);
    CHECK(root != NULL);
    if (!root) return;

    cJSON *method = cJSON_GetObjectItem(root, "method");
    CHECK(cJSON_IsString(method));
    CHECK(method && strcmp(method->valuestring, "play") == 0);

    cJSON *id = cJSON_GetObjectItem(root, "id");
    CHECK(cJSON_IsNumber(id));
    CHECK(id && id->valuedouble == 7);

    /* Serialize then re-parse: values must survive the round trip. */
    char *out = cJSON_PrintUnformatted(root);
    CHECK(out != NULL);
    cJSON *root2 = out ? cJSON_Parse(out) : NULL;
    CHECK(root2 != NULL);
    if (root2) {
        cJSON *m2 = cJSON_GetObjectItem(root2, "method");
        CHECK(m2 && cJSON_IsString(m2) && strcmp(m2->valuestring, "play") == 0);
        cJSON *i2 = cJSON_GetObjectItem(root2, "id");
        CHECK(i2 && cJSON_IsNumber(i2) && i2->valuedouble == 7);
        cJSON_Delete(root2);
    }

    /* Malformed input must be rejected, not crash. */
    cJSON *bad = cJSON_Parse("{not valid json");
    CHECK(bad == NULL);
    if (bad) cJSON_Delete(bad);

    if (out) free(out);
    cJSON_Delete(root);
}

/* ── country_codes: lookups + normalisation ──────────────────────────────*/

static void test_country_codes(void)
{
    printf("country_codes:\n");

    /* Table is non-empty and NULL-terminated count is sane. */
    size_t count = 0;
    const CountryCode *tbl = country_table(&count);
    CHECK(tbl != NULL);
    CHECK(count > 0);
    CHECK(tbl[count].name == NULL);   /* sentinel terminator */

    /* Known lookups. */
    const CountryCode *au = country_by_iso("AU");
    CHECK(au != NULL);
    CHECK(au && strcmp(au->code, "+61") == 0);
    CHECK(au && strcmp(au->trunk, "0") == 0);

    const CountryCode *us = country_by_iso("US");
    CHECK(us != NULL);
    CHECK(us && strcmp(us->code, "+1") == 0);
    CHECK(us && strcmp(us->trunk, "") == 0);

    /* Unknown / NULL lookups return NULL. */
    CHECK(country_by_iso("ZZ") == NULL);
    CHECK(country_by_iso(NULL) == NULL);

    /* E.164 normalisation — cases from the header contract. */
    char out[32];

    CHECK(phone_normalize("0413 805 252", au, out, sizeof(out)) == 0);
    CHECK(strcmp(out, "+61413805252") == 0);

    const CountryCode *gb = country_by_iso("GB");
    CHECK(phone_normalize("07700900001", gb, out, sizeof(out)) == 0);
    CHECK(strcmp(out, "+447700900001") == 0);

    CHECK(phone_normalize("2025551234", us, out, sizeof(out)) == 0);
    CHECK(strcmp(out, "+12025551234") == 0);

    /* Already-international input passes through. */
    CHECK(phone_normalize("+61413805252", au, out, sizeof(out)) == 0);
    CHECK(strcmp(out, "+61413805252") == 0);

    /* "00" international prefix becomes "+". */
    CHECK(phone_normalize("0061413805252", NULL, out, sizeof(out)) == 0);
    CHECK(strcmp(out, "+61413805252") == 0);

    /* No digits → error. */
    CHECK(phone_normalize("abc", au, out, sizeof(out)) == -1);

    /* phone_is_e164 predicate. */
    CHECK(phone_is_e164("+61413805252") == 1);
    CHECK(phone_is_e164("0413805252") == 0);   /* no '+' */
    CHECK(phone_is_e164("+123") == 0);          /* too short */
    CHECK(phone_is_e164(NULL) == 0);
}

/* ── ipc: framing over a socketpair ──────────────────────────────────────*/

static void test_ipc_roundtrip(void)
{
    printf("ipc send_msg/recv_msg round-trip:\n");

    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    const char *msg = "{\"jsonrpc\":\"2.0\",\"result\":\"ok\",\"id\":1}";
    CHECK(send_msg(sv[0], msg) == 0);

    char buf[256];
    int n = recv_msg(sv[1], buf, (int)sizeof(buf));
    CHECK(n == (int)strlen(msg));
    CHECK(strcmp(buf, msg) == 0);

    ipc_close(sv[0]);
    ipc_close(sv[1]);
}

/* Oversize-length rejection: recv_msg must return -1 when the framed length
 * is >= buffer_size (ipc.c:153) rather than overflowing the buffer. */
static void test_ipc_oversize_reject(void)
{
    printf("ipc oversize-length rejection:\n");

    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    /* Hand-craft a frame claiming a payload larger than the receive buffer. */
    char smallbuf[16];
    uint32_t declared = 100;                 /* >> sizeof(smallbuf) */
    uint32_t net_len = htonl(declared);
    CHECK(write(sv[0], &net_len, sizeof(net_len)) == (ssize_t)sizeof(net_len));

    int n = recv_msg(sv[1], smallbuf, (int)sizeof(smallbuf));
    CHECK(n == -1);   /* rejected before any payload read */

    ipc_close(sv[0]);
    ipc_close(sv[1]);
}

/* Short/partial read: sender declares a length but closes before sending the
 * full payload. recv_all sees EOF mid-body and recv_msg must return -1. */
static void test_ipc_short_read(void)
{
    printf("ipc short/partial read:\n");

    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    uint32_t declared = 20;                  /* promise 20 bytes ... */
    uint32_t net_len = htonl(declared);
    CHECK(write(sv[0], &net_len, sizeof(net_len)) == (ssize_t)sizeof(net_len));
    CHECK(write(sv[0], "short", 5) == 5);    /* ... but only send 5 */
    ipc_close(sv[0]);                        /* then close → EOF */

    char buf[64];
    int n = recv_msg(sv[1], buf, (int)sizeof(buf));
    CHECK(n == -1);

    ipc_close(sv[1]);
}

int main(void)
{
    printf("== Black Hand OS host unit tests ==\n\n");

    test_cjson_roundtrip();
    test_country_codes();
    test_ipc_roundtrip();
    test_ipc_oversize_reject();
    test_ipc_short_read();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
