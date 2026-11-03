/*
 * modem_storage.h — thin write-only client for blackhand-storage.
 *
 * The modem service is THE writer of communication history (the flow is
 * URC event → here → /data/numbers/<number>/...). The UI only reads.
 * Failures are logged, never fatal —
 * a missing storage daemon must not break call handling.
 */
#ifndef MODEM_STORAGE_H
#define MODEM_STORAGE_H

/* direction: "in" | "out"
 * outcome:   "answered" | "missed" | "busy" | "rejected" | "no_answer" | "failed"
 * ts:        call start (epoch seconds); duration: talk time in seconds. */
void storage_record_call(const char *number, const char *direction,
                         const char *outcome, long ts, int duration);

/* 0 only after storage acknowledges persistence; otherwise -1. */
int storage_record_sms(const char *number, const char *direction,
                        const char *body);

#endif

/* Latched until process restart; a missed write needs user intervention. */
int storage_write_failed(void);
