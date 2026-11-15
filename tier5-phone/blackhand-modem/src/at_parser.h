#pragma once

#include <stddef.h>
#include "cJSON.h"  /* from $(STAGING_DIR)/usr/include/blackhand/ */

/* Parse signal strength from AT+CSQ response text.
 * Returns RSSI (0–31) or -1 on no-signal / parse error. */
int at_parse_csq(const char *response);

/* Parse AT+CMGL="ALL" multi-line response into a cJSON array.
 * Each element: { "id":N, "status":"...", "sender":"...",
 *                 "timestamp":"...", "body":"..." }
 * Caller owns the returned object and must cJSON_Delete it. */
cJSON *at_parse_cmgl(const char *response);

/* Parse AT+CLCC response into a cJSON array.
 * Each element: { "idx":N, "dir":N, "stat":N, "number":"..." }
 * Caller owns the returned object and must cJSON_Delete it. */
cJSON *at_parse_clcc(const char *response);

/* Parse AT+CGPSINFO response.
 * Returns 0 on a valid fix, -1 on no-fix / parse error.
 * lat/lon: decimal degrees (negative = S/W)
 * alt: metres above sea level (0 if not available)
 * speed: km/h (0 if not available) */
int at_parse_cgpsinfo(const char *response,
                      double *lat, double *lon,
                      double *alt, double *speed);

/* Parse a +CLIP URC line.
 * Copies the caller number string into buf (max buf_size chars).
 * Returns 0 on success, -1 on parse error. */
int at_parse_clip(const char *urc, char *buf, size_t buf_size);
