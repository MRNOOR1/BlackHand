/*
 * storage_ipc.h — UI-side client for blackhand-storage (per-number layout).
 *
 * The UI is a READER of history (/data/numbers/...) and the OWNER of contact
 * edits (/data/contacts/contacts.json). It never writes call/SMS history —
 * the modem service does that on URC events.
 *
 * List-returning calls fill buf with the JSON array string from the service.
 * All return 0 on success, -1 on IPC/parse failure.
 */
#ifndef STORAGE_IPC_H
#define STORAGE_IPC_H

int storage_ipc_ping(void);

/* contacts.json — the lookup table */
int storage_ipc_contacts_list(char *buf, int buf_size);
int storage_ipc_contacts_save(const char *name, const char *number);
int storage_ipc_contacts_delete(const char *number);

/* SMS history */
int storage_ipc_sms_threads(char *buf, int buf_size);
int storage_ipc_sms_list(const char *number, char *buf, int buf_size);
int storage_ipc_mark_read(const char *number);

/* call history (merged across numbers, newest first) */
int storage_ipc_calls_recent(int limit, char *buf, int buf_size);

/* settings.json */
int storage_ipc_settings_get_str(const char *key, char *buf, int buf_size);
int storage_ipc_settings_set_str(const char *key, const char *value);

#endif
