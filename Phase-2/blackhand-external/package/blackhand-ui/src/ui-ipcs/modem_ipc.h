#ifndef MODEM_IPC_H
#define MODEM_IPC_H

/*
 * modem_ipc — client-side wrappers for blackhand-modem JSON-RPC calls.
 * All functions return 0 on success, -1 on IPC failure.
 */

typedef struct {
    int    has_incoming_call;
    char   incoming_number[64];
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

int modem_ipc_ping(void);

/* SMS */
int modem_ipc_send_sms(const char *number, const char *body);
/* Fills buf with JSON array string of messages.  Returns 0 on success. */
int modem_ipc_get_messages(char *buf, int buf_size);

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
