#include "modem_core.h"
#include "modem_uart.h"
#include "at_parser.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define AT_TIMEOUT_MS   5000
#define AT_INIT_TIMEOUT 10000

int modem_init(void)
{
    char resp[256];

    /* Basic health check — try twice in case modem needs wake-up */
    if (at_command("AT", NULL, 0, 2000) < 0) {
        usleep(500000);
        if (at_command("AT", NULL, 0, 2000) < 0) return -1;
    }

    at_command("ATE0",           NULL,  0,     AT_TIMEOUT_MS); /* echo off */
    at_command("AT+CMGF=1",      NULL,  0,     AT_TIMEOUT_MS); /* SMS text mode */
    at_command("AT+CNMI=2,1,0,1,0", NULL, 0,  AT_TIMEOUT_MS); /* new-SMS URC */
    at_command("AT+CLIP=1",      NULL,  0,     AT_TIMEOUT_MS); /* caller-ID URC */
    at_command("AT+CREG=1",      NULL,  0,     AT_TIMEOUT_MS); /* reg change URC */

    /* Verify SMS text mode stuck */
    if (at_command("AT+CMGF?", resp, sizeof(resp), AT_TIMEOUT_MS) < 0) return -1;

    return 0;
}

/* ── SMS ─────────────────────────────────────────────────────────────────── */

int modem_sms_send(const char *number, const char *body)
{
    return modem_uart_sms_send(number, body);
}

cJSON *modem_sms_list(void)
{
    char resp[4096];
    if (at_command("AT+CMGL=\"ALL\"", resp, sizeof(resp), AT_TIMEOUT_MS) < 0)
        return cJSON_CreateArray();
    return at_parse_cmgl(resp);
}

int modem_sms_delete(int msg_id)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", msg_id);
    return at_command(cmd, NULL, 0, AT_TIMEOUT_MS);
}

/* ── Calls ───────────────────────────────────────────────────────────────── */

int modem_call_dial(const char *number)
{
    if (!number) return -1;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);
    return at_command(cmd, NULL, 0, AT_TIMEOUT_MS);
}

int modem_call_hangup(void)
{
    return at_command("ATH", NULL, 0, AT_TIMEOUT_MS);
}

int modem_call_answer(void)
{
    return at_command("ATA", NULL, 0, AT_TIMEOUT_MS);
}

int modem_call_reject(void)
{
    /* AT+CHUP is more reliable than ATH for rejection before answering */
    if (at_command("AT+CHUP", NULL, 0, AT_TIMEOUT_MS) == 0) return 0;
    return at_command("ATH", NULL, 0, AT_TIMEOUT_MS);
}

cJSON *modem_call_get_active(void)
{
    char resp[512];
    if (at_command("AT+CLCC", resp, sizeof(resp), AT_TIMEOUT_MS) < 0)
        return cJSON_CreateArray();
    return at_parse_clcc(resp);
}

/* ── GPS ─────────────────────────────────────────────────────────────────── */

int modem_gps_enable(void)
{
    /* AT+CGPS=1,1 : enable GPS in stand-alone mode */
    at_command("AT+CGPS=1,1", NULL, 0, AT_TIMEOUT_MS);
    return 0;
}

int modem_gps_disable(void)
{
    return at_command("AT+CGPS=0", NULL, 0, AT_TIMEOUT_MS);
}

int modem_gps_get_location(double *lat, double *lon,
                            double *alt, double *speed)
{
    if (!lat || !lon) return -1;
    char resp[256];
    if (at_command("AT+CGPSINFO", resp, sizeof(resp), AT_TIMEOUT_MS) < 0)
        return -1;
    double a = 0.0, s = 0.0;
    int rc = at_parse_cgpsinfo(resp, lat, lon, &a, &s);
    if (alt)   *alt   = a;
    if (speed) *speed = s;
    return rc;
}

/* ── System ─────────────────────────────────────────────────────────────── */

int modem_get_signal_strength(int *rssi)
{
    char resp[64];
    if (at_command("AT+CSQ", resp, sizeof(resp), AT_TIMEOUT_MS) < 0)
        return -1;
    int val = at_parse_csq(resp);
    if (val < 0) { if (rssi) *rssi = 0; return -1; }
    if (rssi) *rssi = val;
    return 0;
}

int modem_get_imei(char *buf, size_t len)
{
    return at_command("AT+CGSN", buf, len, AT_TIMEOUT_MS);
}

int modem_get_operator(char *buf, size_t len)
{
    return at_command("AT+COPS?", buf, len, AT_TIMEOUT_MS);
}
