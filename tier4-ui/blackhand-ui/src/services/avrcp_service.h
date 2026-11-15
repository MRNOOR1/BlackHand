#ifndef AVRCP_SERVICE_H
#define AVRCP_SERVICE_H

/* Registers a BlueZ MediaPlayer1 D-Bus object so AVRCP pass-through commands
 * from connected headsets (Play, Pause, Next, Previous, Stop) are dispatched
 * to mp3_service.  Call once at startup, after bluetooth_service_init(). */
void avrcp_service_init(void);
void avrcp_service_shutdown(void);

#endif
