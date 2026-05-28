#ifndef STORAGE_IPC_H
#define STORAGE_IPC_H

/*
 * storage_ipc — client-side wrappers for blackhand-storage JSON-RPC calls.
 * All functions return 0 on success, -1 on IPC failure.
 *
 * Array results are written as JSON strings into the caller's buffer
 * (same pattern as audio_ipc).
 */

/* ── Contacts ────────────────────────────────────────────────────────────── */
int storage_ipc_contacts_list(char *buf, int buf_size);
/* params: { "name":"Alice", "phone":"+1234", "email":"", "notes":"" }
 * Returns the new contact id on success (>= 1), -1 on failure. */
int storage_ipc_contacts_add(const char *name, const char *phone,
                              const char *email, const char *notes);
int storage_ipc_contacts_delete(int id);
int storage_ipc_contacts_update(int id, const char *name, const char *phone);
int storage_ipc_contacts_search(const char *query, char *buf, int buf_size);

/* ── Messages ────────────────────────────────────────────────────────────── */
int storage_ipc_messages_list(char *buf, int buf_size);
int storage_ipc_messages_add(const char *phone, const char *sender,
                              const char *body, int is_outgoing);
int storage_ipc_messages_delete(int id);
int storage_ipc_messages_mark_read(int id);

/* ── Calls ───────────────────────────────────────────────────────────────── */
int storage_ipc_calls_list(char *buf, int buf_size);
int storage_ipc_calls_add(const char *phone, const char *caller_name,
                           const char *call_type, int duration_sec);
int storage_ipc_calls_delete(int id);

/* ── Locations ───────────────────────────────────────────────────────────── */
int storage_ipc_locations_add(double lat, double lon,
                               double alt, double speed);
int storage_ipc_locations_list(char *buf, int buf_size);

/* ── Settings ────────────────────────────────────────────────────────────── */
int storage_ipc_settings_get(const char *key, char *val_buf, int val_size);
int storage_ipc_settings_set(const char *key, const char *value);

#endif
