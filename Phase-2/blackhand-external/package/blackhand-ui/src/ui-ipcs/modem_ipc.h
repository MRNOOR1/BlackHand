#ifndef MODEM_IPC_H
#define MODEM_IPC_H

/*
 * modem_ipc — client-side wrappers for blackhand-modem JSON-RPC calls.
 * All functions return 0 on success, -1 on IPC failure.
 */

typedef struct {
    int    has_incoming_call;
    char   incoming_number[64];
    char   call_state[16];   /* "idle" | "dialling" | "ringing" | "active" */
    int    pending_sms;
    int    signal;
    int    gps_enabled;
} ModemStatus;

typedef struct {
    double latitude;
    double longitude;
    double altitude;
    double speed;
    int    has_fix;
} GpsLocation;

typedef struct {
    int  present;          /* 1 if AT port is open and modem responded to init */
    char port[64];         /* device path the service is using                 */
    char error[128];       /* human-readable reason if !present                */
} ModemHealth;

int modem_ipc_ping(void);
/* Always-safe reachability + hardware check. Service responds even when the
 * modem is offline, so this distinguishes "modem service not running" (return
 * -1) from "modem service is up but hardware is missing" (returns 0 with
 * out->present == 0). */
int modem_ipc_health(ModemHealth *out);

/* Cached health, refreshed by the main loop. Cheap to call every frame —
 * just reads a static. Screens use these to show an OFFLINE banner.        */
void        modem_ipc_refresh_health(void);
int         modem_ipc_is_online(void);
const char *modem_ipc_health_error(void);
/* Device path the modem service is currently using (cached from health). */
const char *modem_ipc_health_port(void);

/* Pin the modem service to a specific AT port ("/dev/ttyUSB0".."4"), or
 * "auto" to resume the normal sweep. Synchronous: the service tears down,
 * probes the new port and configures it before replying (1-3s typically).
 * Returns 0 if the modem answered on that port. */
int modem_ipc_set_port(const char *port);

/* SMS */
int modem_ipc_send_sms(const char *number, const char *body);
/* Fills buf with JSON array string of messages.  Returns 0 on success. */
int modem_ipc_get_messages(char *buf, int buf_size);
/* Drain pending incoming SMS from the modem service.
 * buf is filled with a JSON array of {"sender":"...","body":"..."} objects.
 * Returns 0 on success, -1 on IPC failure. Empty array on no pending.        */
int modem_ipc_pop_pending_sms(char *buf, int buf_size);

/* Calls */
int modem_ipc_dial(const char *number);
int modem_ipc_hangup(void);
int modem_ipc_answer(void);
int modem_ipc_reject(void);
int modem_ipc_get_calls(char *buf, int buf_size);

/* GPS */
int modem_ipc_gps_enable(void);
int modem_ipc_gps_disable(void);
int modem_ipc_get_location(GpsLocation *out);

/* Status (signal, incoming call, pending SMS) */
int modem_ipc_get_status(ModemStatus *out);

#endif
