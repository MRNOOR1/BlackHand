#include "at_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── signal strength ─────────────────────────────────────────────────────── */

int at_parse_csq(const char *response)
{
    if (!response) return -1;
    const char *p = strstr(response, "+CSQ:");
    if (!p) return -1;
    p += 5;
    while (*p == ' ') p++;
    int rssi = atoi(p);
    return (rssi == 99) ? -1 : rssi;
}

/* ── GPS ─────────────────────────────────────────────────────────────────── */

/* NMEA coordinate DDDMM.MMMM → decimal degrees */
static double nmea_to_deg(double raw)
{
    int deg = (int)(raw / 100.0);
    double minutes = raw - (double)deg * 100.0;
    return (double)deg + minutes / 60.0;
}

int at_parse_cgpsinfo(const char *response,
                      double *lat, double *lon,
                      double *alt, double *speed)
{
    if (!response || !lat || !lon) return -1;

    const char *p = strstr(response, "+CGPSINFO:");
    if (!p) p = strstr(response, "+CGPSINFO: ");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    while (*p == ' ') p++;

    /* +CGPSINFO: lat,N/S,lon,E/W,date,time,alt,speed,course */
    double rlat = 0.0, rlon = 0.0, ralt = 0.0, rspd = 0.0;
    char ns = 'N', ew = 'E';
    char date[16] = {0}, tstr[16] = {0};

    int n = sscanf(p, "%lf,%c,%lf,%c,%15[^,],%15[^,],%lf,%lf",
                   &rlat, &ns, &rlon, &ew, date, tstr, &ralt, &rspd);
    if (n < 4) return -1;
    /* No fix: modem returns empty fields */
    if (rlat == 0.0 && rlon == 0.0) return -1;

    *lat = nmea_to_deg(rlat) * (ns == 'S' ? -1.0 : 1.0);
    *lon = nmea_to_deg(rlon) * (ew == 'W' ? -1.0 : 1.0);
    if (alt)   *alt   = (n >= 7) ? ralt : 0.0;
    if (speed) *speed = (n >= 8) ? rspd : 0.0;
    return 0;
}

/* ── incoming call ───────────────────────────────────────────────────────── */

int at_parse_clip(const char *urc, char *buf, size_t buf_size)
{
    if (!urc || !buf || buf_size == 0) return -1;
    /* +CLIP: "+1234567890",145,,,,0  */
    const char *p = strchr(urc, '"');
    if (!p) {
        /* Some modems omit quotes */
        p = strchr(urc, ':');
        if (!p) return -1;
        p++;
        while (*p == ' ') p++;
        size_t len = 0;
        while (p[len] && p[len] != ',' && p[len] != '\r' && p[len] != '\n')
            len++;
        if (len >= buf_size) len = buf_size - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
        return 0;
    }
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return 0;
}

/* ── SMS list ────────────────────────────────────────────────────────────── */

/*
 * Parse AT+CMGL="ALL" response (text mode, ATE0).
 * Pairs of lines:  +CMGL header \n  message body \n
 *
 * Header format: +CMGL: <idx>,"<stat>","<sender>","<alpha>","<timestamp>"
 */
cJSON *at_parse_cmgl(const char *response)
{
    cJSON *arr = cJSON_CreateArray();
    if (!response) return arr;

    char *copy = strdup(response);
    if (!copy) return arr;

    char *saveptr = NULL;
    char *line = strtok_r(copy, "\n", &saveptr);

    while (line) {
        /* Trim \r */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
            line[--len] = '\0';

        if (strncmp(line, "+CMGL:", 6) == 0) {
            char *p = line + 6;
            while (*p == ' ') p++;

            int idx = atoi(p);
            /* Skip past idx and comma */
            p = strchr(p, ',');
            if (!p) { line = strtok_r(NULL, "\n", &saveptr); continue; }
            p++;

            /* Extract up to 4 quoted/unquoted fields */
            char fields[4][64];
            for (int fi = 0; fi < 4 && *p; fi++) {
                fields[fi][0] = '\0';
                while (*p == ' ') p++;
                if (*p == '"') {
                    p++;
                    char *end = strchr(p, '"');
                    if (!end) break;
                    size_t flen = (size_t)(end - p);
                    if (flen >= sizeof(fields[fi])) flen = sizeof(fields[fi]) - 1;
                    memcpy(fields[fi], p, flen);
                    fields[fi][flen] = '\0';
                    p = end + 1;
                    if (*p == ',') p++;
                } else {
                    char *end = strchr(p, ',');
                    if (end) {
                        size_t flen = (size_t)(end - p);
                        if (flen >= sizeof(fields[fi])) flen = sizeof(fields[fi]) - 1;
                        memcpy(fields[fi], p, flen);
                        fields[fi][flen] = '\0';
                        p = end + 1;
                    } else {
                        snprintf(fields[fi], sizeof(fields[fi]), "%s", p);
                        p += strlen(p);
                    }
                }
            }

            /* Body is on the next line */
            char *body = strtok_r(NULL, "\n", &saveptr);
            if (body) {
                size_t blen = strlen(body);
                while (blen > 0 && (body[blen - 1] == '\r' || body[blen - 1] == ' '))
                    body[--blen] = '\0';
            }

            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "id",        (double)idx);
            cJSON_AddStringToObject(obj, "status",    fields[0]);
            cJSON_AddStringToObject(obj, "sender",    fields[1]);
            cJSON_AddStringToObject(obj, "timestamp", fields[3]);
            cJSON_AddStringToObject(obj, "body",      body ? body : "");
            cJSON_AddItemToArray(arr, obj);
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(copy);
    return arr;
}

/* ── call list ───────────────────────────────────────────────────────────── */

cJSON *at_parse_clcc(const char *response)
{
    cJSON *arr = cJSON_CreateArray();
    if (!response) return arr;

    char *copy = strdup(response);
    if (!copy) return arr;

    char *saveptr = NULL;
    char *line = strtok_r(copy, "\n", &saveptr);

    while (line) {
        if (strncmp(line, "+CLCC:", 6) == 0) {
            int idx = 0, dir = 0, stat = 0;
            char number[64] = "";
            char *p = line + 6;
            while (*p == ' ') p++;
            sscanf(p, "%d,%d,%d", &idx, &dir, &stat);
            char *q = strchr(p, '"');
            if (q) {
                q++;
                char *end = strchr(q, '"');
                if (end) {
                    size_t len = (size_t)(end - q);
                    if (len >= sizeof(number)) len = sizeof(number) - 1;
                    memcpy(number, q, len);
                    number[len] = '\0';
                }
            }
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "idx",    (double)idx);
            cJSON_AddNumberToObject(obj, "dir",    (double)dir);
            cJSON_AddNumberToObject(obj, "stat",   (double)stat);
            cJSON_AddStringToObject(obj, "number", number);
            cJSON_AddItemToArray(arr, obj);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(copy);
    return arr;
}
