#ifndef BLACKHAND_GPS_SERVICE_H
#define BLACKHAND_GPS_SERVICE_H
/* SIMCom CGPSINFO coordinates are degrees+minutes; speed is knots. */
typedef struct { double latitude, longitude, altitude, speed; } ModemGpsFix;
int gps_parse_info(const char *response, ModemGpsFix *fix);
int gps_set_enabled(int fd, int enabled);
int gps_get_enabled(int fd);
int gps_get_fix(int fd, ModemGpsFix *fix);
#endif

void gps_reset_state(void);
