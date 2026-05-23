#include "alarm_service.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define INITIAL_ALARM_CAPACITY 16

static Alarm **s_alarms = NULL;
static size_t s_count = 0;
static size_t s_capacity = 0;
static const char *ALARMS_PATH = APP_PATH_ALARMS_DIR;
static Alarm *s_ringing = NULL;
static Alarm *s_snooze_alarm = NULL;
static time_t s_snooze_until = 0;
static time_t s_last_checked_minute = 0;

static void free_alarm(Alarm *a) {
    if (!a) return;
    free(a->id);
    free(a->label);
    free(a);
}

static int ensure_capacity(void) {
    if (s_count < s_capacity) return 0;
    size_t new_capacity = (s_capacity == 0) ? INITIAL_ALARM_CAPACITY : s_capacity * 2;
    Alarm **next = realloc(s_alarms, sizeof(Alarm *) * new_capacity);
    if (!next) return -1;
    s_alarms = next;
    s_capacity = new_capacity;
    return 0;
}

static const char *repeat_str(alarm_repeat_t r) {
    switch (r) {
        case ALARM_DAILY:    return "daily";
        case ALARM_WEEKDAYS: return "weekdays";
        default:             return "once";
    }
}

static alarm_repeat_t parse_repeat(const char *s) {
    if (!s) return ALARM_ONCE;
    if (strcmp(s, "daily") == 0) return ALARM_DAILY;
    if (strcmp(s, "weekdays") == 0) return ALARM_WEEKDAYS;
    return ALARM_ONCE;
}

static int write_alarm_file(const Alarm *a) {
    if (!a || !a->id) return -1;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", ALARMS_PATH, a->id);

    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "Time: %02d:%02d\n", a->hour, a->minute);
    fprintf(f, "Enabled: %d\n", a->enabled ? 1 : 0);
    fprintf(f, "Label: %s\n", a->label ? a->label : "Alarm");
    fprintf(f, "Repeat: %s\n", repeat_str(a->repeat));
    fclose(f);
    return 0;
}

static int generate_alarm_id(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm tm_now;
    if (!localtime_r(&now, &tm_now)) return -1;

    for (size_t i = 0; i < 1000; i++) {
        snprintf(buf, len,
                 "%04d%02d%02d%02d%02d%02d_%zu.alarm",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday,
                 tm_now.tm_hour,
                 tm_now.tm_min,
                 tm_now.tm_sec,
                 i);

        int exists = 0;
        for (size_t j = 0; j < s_count; j++) {
            if (s_alarms[j] && s_alarms[j]->id && strcmp(s_alarms[j]->id, buf) == 0) {
                exists = 1;
                break;
            }
        }
        if (!exists) return 0;
    }

    return -1;
}

void alarm_service_init(void) {
    s_alarms = malloc(sizeof(Alarm *) * INITIAL_ALARM_CAPACITY);
    if (!s_alarms) return;
    s_count = 0;
    s_capacity = INITIAL_ALARM_CAPACITY;

    struct stat st = {0};
    if (stat(ALARMS_PATH, &st) == -1) {
        if (mkdir(ALARMS_PATH, 0755) != 0) return;
    }

    DIR *dir = opendir(ALARMS_PATH);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".alarm") != 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", ALARMS_PATH, entry->d_name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        Alarm *a = malloc(sizeof(Alarm));
        if (!a) {
            fclose(f);
            continue;
        }
        a->id = strdup(entry->d_name);
        a->hour = 7;
        a->minute = 0;
        a->enabled = true;
        a->label = strdup("Alarm");
        a->repeat = ALARM_ONCE;

        char line[256];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strncmp(line, "Time: ", 6) == 0) {
                int h = 0;
                int m = 0;
                if (sscanf(line + 6, "%d:%d", &h, &m) == 2) {
                    if (h >= 0 && h < 24) a->hour = h;
                    if (m >= 0 && m < 60) a->minute = m;
                }
            } else if (strncmp(line, "Enabled: ", 9) == 0) {
                a->enabled = (line[9] == '1');
            } else if (strncmp(line, "Label: ", 7) == 0) {
                free(a->label);
                a->label = strdup(line + 7);
            } else if (strncmp(line, "Repeat: ", 8) == 0) {
                a->repeat = parse_repeat(line + 8);
            }
        }
        fclose(f);

        if (!a->id || !a->label || ensure_capacity() != 0) {
            free_alarm(a);
            continue;
        }

        for (size_t j = s_count; j > 0; j--) s_alarms[j] = s_alarms[j - 1];
        s_alarms[0] = a;
        s_count++;
    }

    closedir(dir);
}

const Alarm **alarm_service_list_all(size_t *count) {
    if (count) *count = s_count;
    return (const Alarm **)s_alarms;
}

size_t alarm_service_count(void) {
    return s_count;
}

Alarm *alarm_service_create(int hour, int minute, const char *label) {
    if (ensure_capacity() != 0) return NULL;

    char id[128];
    if (generate_alarm_id(id, sizeof(id)) != 0) return NULL;

    Alarm *a = malloc(sizeof(Alarm));
    if (!a) return NULL;
    a->id = strdup(id);
    a->hour = (hour >= 0 && hour < 24) ? hour : 7;
    a->minute = (minute >= 0 && minute < 60) ? minute : 0;
    a->enabled = true;
    a->label = strdup(label ? label : "Alarm");
    a->repeat = ALARM_ONCE;
    if (!a->id || !a->label) {
        free_alarm(a);
        return NULL;
    }

    for (size_t j = s_count; j > 0; j--) s_alarms[j] = s_alarms[j - 1];
    s_alarms[0] = a;
    s_count++;

    write_alarm_file(a);
    return a;
}

int alarm_service_toggle(const char *id) {
    if (!id) return -1;
    for (size_t i = 0; i < s_count; i++) {
        Alarm *a = s_alarms[i];
        if (a && a->id && strcmp(a->id, id) == 0) {
            a->enabled = !a->enabled;
            return write_alarm_file(a);
        }
    }
    return -1;
}

int alarm_service_set_time(const char *id, int hour, int minute) {
    if (!id) return -1;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return -1;

    for (size_t i = 0; i < s_count; i++) {
        Alarm *a = s_alarms[i];
        if (a && a->id && strcmp(a->id, id) == 0) {
            a->hour = hour;
            a->minute = minute;
            return write_alarm_file(a);
        }
    }
    return -1;
}

int alarm_service_set_label(const char *id, const char *label) {
    if (!id || !label) return -1;
    for (size_t i = 0; i < s_count; i++) {
        Alarm *a = s_alarms[i];
        if (a && a->id && strcmp(a->id, id) == 0) {
            free(a->label);
            a->label = strdup(label);
            return write_alarm_file(a);
        }
    }
    return -1;
}

int alarm_service_set_repeat(const char *id, alarm_repeat_t repeat) {
    if (!id) return -1;
    for (size_t i = 0; i < s_count; i++) {
        Alarm *a = s_alarms[i];
        if (a && a->id && strcmp(a->id, id) == 0) {
            a->repeat = repeat;
            return write_alarm_file(a);
        }
    }
    return -1;
}

int alarm_service_delete(const char *id) {
    if (!id) return -1;
    for (size_t i = 0; i < s_count; i++) {
        Alarm *a = s_alarms[i];
        if (!a || !a->id || strcmp(a->id, id) != 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", ALARMS_PATH, a->id);
        remove(path);
        if (s_ringing == a) s_ringing = NULL;
        if (s_snooze_alarm == a) {
            s_snooze_alarm = NULL;
            s_snooze_until = 0;
        }
        free_alarm(a);

        for (size_t j = i; j < s_count - 1; j++) s_alarms[j] = s_alarms[j + 1];
        s_count--;
        return 0;
    }
    return -1;
}

void alarm_service_shutdown(void) {
    if (!s_alarms) return;
    for (size_t i = 0; i < s_count; i++) free_alarm(s_alarms[i]);
    free(s_alarms);
    s_alarms = NULL;
    s_count = 0;
    s_capacity = 0;
}

static int repeat_matches(alarm_repeat_t repeat, const struct tm *now_tm) {
    if (!now_tm) return 0;
    if (repeat == ALARM_DAILY) return 1;
    if (repeat == ALARM_WEEKDAYS) {
        return (now_tm->tm_wday >= 1 && now_tm->tm_wday <= 5) ? 1 : 0;
    }
    return 1;
}

int alarm_service_tick(void) {
    time_t now = time(NULL);
    if (now <= 0) return 0;

    if (s_ringing) return 0;

    if (s_snooze_alarm && s_snooze_until > 0 && now >= s_snooze_until) {
        s_ringing = s_snooze_alarm;
        s_snooze_alarm = NULL;
        s_snooze_until = 0;
        return 1;
    }

    time_t minute_bucket = now / 60;
    if (minute_bucket == s_last_checked_minute) return 0;
    s_last_checked_minute = minute_bucket;

    struct tm now_tm;
    if (!localtime_r(&now, &now_tm)) return 0;

    for (size_t i = 0; i < s_count; i++) {
        Alarm *a = s_alarms[i];
        if (!a || !a->enabled) continue;
        if (!repeat_matches(a->repeat, &now_tm)) continue;
        if (a->hour == now_tm.tm_hour && a->minute == now_tm.tm_min) {
            s_ringing = a;
            return 1;
        }
    }

    return 0;
}

const Alarm *alarm_service_current_ringing(void) {
    return s_ringing;
}

void alarm_service_stop_ringing(void) {
    if (!s_ringing) return;
    if (s_ringing->repeat == ALARM_ONCE) {
        s_ringing->enabled = false;
        write_alarm_file(s_ringing);
    }
    s_ringing = NULL;
    s_snooze_alarm = NULL;
    s_snooze_until = 0;
}

void alarm_service_snooze_current(int minutes) {
    if (!s_ringing) return;
    if (minutes <= 0) minutes = ALARM_SNOOZE_MIN;
    s_snooze_alarm = s_ringing;
    s_snooze_until = time(NULL) + (time_t)(minutes * 60);
    s_ringing = NULL;
}

void alarm_service_dismiss_current(void) {
    alarm_service_stop_ringing();
}
