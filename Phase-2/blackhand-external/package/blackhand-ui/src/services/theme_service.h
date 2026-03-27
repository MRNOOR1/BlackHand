#ifndef THEME_SERVICE_H
#define THEME_SERVICE_H
/*
theme_service.h 
instead of having each view have their own theme and colours all that will be implemented in this file
*/
#include <stdint.h>


void theme_service_init(void);
uint32_t theme_bg(void);
uint32_t theme_text_primary(void);
uint32_t theme_text_muted(void);
uint32_t theme_border(void);
uint32_t theme_selection_bg(void);
uint32_t theme_selection_text(void);
const char *theme_rule_glyph(void);
void theme_service_sync_from_settings(void);
int theme_service_is_dark(void);


#endif
