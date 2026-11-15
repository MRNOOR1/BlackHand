#include "gps_service.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
const char *at_cmd(int fd, const char *cmd, int timeout) { (void)fd; (void)cmd; (void)timeout; return "ERROR"; }
int main(void) {
    ModemGpsFix fix;
    assert(gps_parse_info("+CGPSINFO:3748.000,S,14457.000,E,090926,120000,10.2,2.0,0\r\nOK", &fix) == 0);
    assert(fabs(fix.latitude + 37.8) < 0.0001 && fabs(fix.longitude - 144.95) < 0.0001);
    assert(fabs(fix.speed - 3.704) < 0.0001);
    assert(gps_parse_info("+CGPSINFO:,,,,,,,,\r\nOK", &fix) < 0);
    assert(gps_parse_info("+CGPSINFO:9061,N,18100,E,090926,120000,0,0,0", &fix) < 0);
    assert(gps_parse_info("+CGPSINFO:nan,N,14400,E,090926,120000,0,0,0", &fix) < 0);
    assert(gps_set_enabled(1, 1) < 0 && gps_get_enabled(1) < 0);
    puts("PASS: GPS hemisphere/minutes conversion, no-fix, malformed data, command failure");
}
