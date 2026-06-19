#ifndef ALARM_SERVICE_H
#define ALARM_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include "../config.h"

/* Repeat modes */
typedef enum {
    ALARM_ONCE,
    ALARM_DAILY,
    ALARM_WEEKDAYS,
} alarm_repeat_t;

typedef struct {
    char *id;
    int hour;
    int minute;
    bool enabled;
    char *label;
    alarm_repeat_t repeat;
} Alarm;

void alarm_service_init(void);
const Alarm **alarm_service_list_all(size_t *count);
size_t alarm_service_count(void);
Alarm *alarm_service_create(int hour, int minute, const char *label);
int alarm_service_toggle(const char *id);
int alarm_service_set_time(const char *id, int hour, int minute);
int alarm_service_set_label(const char *id, const char *label);
int alarm_service_set_repeat(const char *id, alarm_repeat_t repeat);
int alarm_service_delete(const char *id);
int alarm_service_tick(void);
const Alarm *alarm_service_current_ringing(void);
void alarm_service_stop_ringing(void);
void alarm_service_snooze_current(int minutes);
void alarm_service_dismiss_current(void);
void alarm_service_shutdown(void);

#endif
