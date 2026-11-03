#include "gps_service.h"
#include "serial_utility.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
static atomic_int enabled_cache = -1;

int gps_parse_info(const char *response, ModemGpsFix *fix) {
    if (!response || !fix) return -1;
    memset(fix, 0, sizeof(*fix));
    const char *p = strstr(response, "+CGPSINFO:");
    if (!p) return -1;
    double lat, lon, alt, speed; char ns, ew, date[16], utc[24];
    if (sscanf(p, "+CGPSINFO: %lf,%c,%lf,%c,%15[^,],%23[^,],%lf,%lf",
               &lat, &ns, &lon, &ew, date, utc, &alt, &speed) != 8) return -1;
    if (!isfinite(lat) || !isfinite(lon) || !isfinite(alt) || !isfinite(speed) ||
        lat < 0 || lon < 0 || lat > 9000 || lon > 18000 || speed < 0 ||
        (ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W')) return -1;
    int ld = (int)(lat / 100), od = (int)(lon / 100);
    double lm = lat - ld * 100, om = lon - od * 100;
    if (lm >= 60 || om >= 60 || (ld == 90 && lm > 0) || (od == 180 && om > 0)) return -1;
    fix->latitude = (ld + lm / 60) * (ns == 'S' ? -1 : 1);
    fix->longitude = (od + om / 60) * (ew == 'W' ? -1 : 1);
    fix->altitude = alt; fix->speed = speed * 1.852;
    return 0;
}
int gps_set_enabled(int fd, int enabled) {
    const char *response = at_cmd(fd, enabled ? "AT+CGPS=1" : "AT+CGPS=0", 3000);
    if (!response || !strstr(response, "OK")) return -1;
    atomic_store(&enabled_cache, !!enabled);
    return 0;
}
int gps_get_enabled(int fd) { (void)fd; return atomic_load(&enabled_cache); }
void gps_reset_state(void) { atomic_store(&enabled_cache, -1); }
int gps_get_fix(int fd, ModemGpsFix *fix) {
    return gps_parse_info(at_cmd(fd, "AT+CGPSINFO", 2000), fix);
}
