#ifndef PIN_SERVICE_H
#define PIN_SERVICE_H

#include <stdbool.h>

/*
 * pin_service.h
 *
 * Manages the 4-digit PIN used for:
 *   - Exiting HAND WHITE mode
 *   - WHITE WIPE confirmation
 *   - PIN update
 *
 * Salted PBKDF2-SHA256 verifier is stored in /data/pin.conf.
 * Default PIN is "0000"
 */

void pin_service_init(void);
bool pin_service_verify(const char *pin);
int  pin_service_update(const char *old_pin, const char *new_pin);
int pin_service_reset(void);      /* reset to default "0000" */

/* Seconds remaining in the current failed-attempt lockout, 0 if none.
 * Callers should show this instead of "WRONG PIN" while locked out —
 * the guess may well have been right; verify() rejects it unchecked. */
int  pin_service_lockout_remaining_sec(void);

#endif
