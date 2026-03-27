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
 * The PIN is stored in settings.conf as "pin=NNNN"
 * Default PIN is "0000"
 */

void pin_service_init(void);
bool pin_service_verify(const char *pin);
int  pin_service_update(const char *old_pin, const char *new_pin);
const char *pin_service_get(void); /* for internal use only */
void pin_service_reset(void);      /* reset to default "0000" */

#endif
