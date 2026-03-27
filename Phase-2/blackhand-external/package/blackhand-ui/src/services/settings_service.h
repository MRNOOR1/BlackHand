#ifndef SETTINGS_SERVICE_H
#define SETTINGS_SERVICE_H

#include <stdbool.h>

void settings_service_init(void);
void settings_service_shutdown(void);

int settings_service_count(void);
const char *settings_service_label(int index);
bool settings_service_enabled(int index);
void settings_service_toggle(int index);

bool settings_service_get_bool(const char *key);
void settings_service_set_bool(const char *key, bool enabled);
void settings_service_toggle_by_key(const char *key);

int settings_service_theme_count(void);
const char *settings_service_theme_label(int index);
int settings_service_get_light_theme(void);
void settings_service_set_light_theme(int index);
void settings_service_reset_defaults(void);
int settings_service_get_volume(void);
void settings_service_set_volume(int value);
int settings_service_get_brightness(void);
void settings_service_set_brightness(int value);
int settings_service_get_timeout_sec(void);
void settings_service_set_timeout_sec(int sec);

#endif
