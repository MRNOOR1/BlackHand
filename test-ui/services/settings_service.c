#include "settings_service.h"
#include "../config.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key;
    const char *label;
    bool enabled;
    bool default_enabled;
} setting_item_t;

static setting_item_t g_items[] = {
    { SETTINGS_KEY_NIGHT_MODE, SETTINGS_LABEL_NIGHT_MODE, SETTINGS_DEFAULT_NIGHT_MODE, SETTINGS_DEFAULT_NIGHT_MODE },
    { SETTINGS_KEY_BLUETOOTH,  SETTINGS_LABEL_BLUETOOTH,  SETTINGS_DEFAULT_BLUETOOTH,  SETTINGS_DEFAULT_BLUETOOTH  },
    { SETTINGS_KEY_HAND_WHITE, SETTINGS_LABEL_HAND_WHITE, SETTINGS_DEFAULT_HAND_WHITE, SETTINGS_DEFAULT_HAND_WHITE },
    { SETTINGS_KEY_AUX_INPUT,  SETTINGS_LABEL_AUX_INPUT,  SETTINGS_DEFAULT_AUX_INPUT,  SETTINGS_DEFAULT_AUX_INPUT  },
};

static const char *g_themes[] = {
    UI_THEME_LABEL_0,  UI_THEME_LABEL_1,  UI_THEME_LABEL_2,  UI_THEME_LABEL_3,
    UI_THEME_LABEL_4,  UI_THEME_LABEL_5,  UI_THEME_LABEL_6,  UI_THEME_LABEL_7,
    UI_THEME_LABEL_8,  UI_THEME_LABEL_9,  UI_THEME_LABEL_10, UI_THEME_LABEL_11,
};

static const int g_item_count = (int)(sizeof(g_items) / sizeof(g_items[0]));
static const int g_theme_count = (int)(sizeof(g_themes) / sizeof(g_themes[0]));
static const char *SETTINGS_FILE = APP_PATH_SETTINGS_FILE;
static int g_light_theme = SETTINGS_DEFAULT_THEME_INDEX;
static int g_volume = SETTINGS_DEFAULT_VOLUME;
static int g_brightness = SETTINGS_DEFAULT_BRIGHTNESS;
static int g_timeout_sec = SETTINGS_DEFAULT_TIMEOUT_SEC;

static int find_index_by_key(const char *key) {
    for (int i = 0; i < g_item_count; i++) {
        if (strcmp(g_items[i].key, key) == 0) return i;
    }
    return -1;
}

static void settings_service_load(void) {
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        int enabled;
        if (sscanf(line, "%63[^=]=%d", key, &enabled) != 2) continue;

        if (strcmp(key, SETTINGS_KEY_LIGHT_THEME) == 0) {
            if (enabled >= 0 && enabled < g_theme_count) {
                g_light_theme = enabled;
            }
            continue;
        }
        if (strcmp(key, SETTINGS_KEY_VOLUME) == 0) {
            if (enabled < SETTINGS_MIN_VOLUME) enabled = SETTINGS_MIN_VOLUME;
            if (enabled > SETTINGS_MAX_VOLUME) enabled = SETTINGS_MAX_VOLUME;
            g_volume = enabled;
            continue;
        }
        if (strcmp(key, SETTINGS_KEY_BRIGHTNESS) == 0) {
            if (enabled < SETTINGS_MIN_BRIGHTNESS) enabled = SETTINGS_MIN_BRIGHTNESS;
            if (enabled > SETTINGS_MAX_BRIGHTNESS) enabled = SETTINGS_MAX_BRIGHTNESS;
            g_brightness = enabled;
            continue;
        }
        if (strcmp(key, SETTINGS_KEY_TIMEOUT_SEC) == 0) {
            if (enabled < SETTINGS_MIN_TIMEOUT_SEC) enabled = SETTINGS_MIN_TIMEOUT_SEC;
            if (enabled > SETTINGS_MAX_TIMEOUT_SEC) enabled = SETTINGS_MAX_TIMEOUT_SEC;
            g_timeout_sec = enabled;
            continue;
        }

        int index = find_index_by_key(key);
        if (index >= 0) g_items[index].enabled = (enabled != 0);
    }

    fclose(f);
}

static void settings_service_save(void) {
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return;

    for (int i = 0; i < g_item_count; i++) {
        fprintf(f, "%s=%d\n", g_items[i].key, g_items[i].enabled ? 1 : 0);
    }
    fprintf(f, "%s=%d\n", SETTINGS_KEY_LIGHT_THEME, g_light_theme);
    fprintf(f, "%s=%d\n", SETTINGS_KEY_VOLUME, g_volume);
    fprintf(f, "%s=%d\n", SETTINGS_KEY_BRIGHTNESS, g_brightness);
    fprintf(f, "%s=%d\n", SETTINGS_KEY_TIMEOUT_SEC, g_timeout_sec);

    fclose(f);
}

void settings_service_init(void) {
    settings_service_load();
}

void settings_service_shutdown(void) {
    settings_service_save();
}

int settings_service_count(void) {
    return g_item_count;
}

const char *settings_service_label(int index) {
    if (index < 0 || index >= g_item_count) return "";
    return g_items[index].label;
}

bool settings_service_enabled(int index) {
    if (index < 0 || index >= g_item_count) return false;
    return g_items[index].enabled;
}

void settings_service_toggle(int index) {
    if (index < 0 || index >= g_item_count) return;
    g_items[index].enabled = !g_items[index].enabled;
    settings_service_save();
}
void settings_service_toggle_by_key(const char *key) {
    int index = find_index_by_key(key);
    if (index < 0) return;
    g_items[index].enabled = !g_items[index].enabled;
    settings_service_save();
}

bool settings_service_get_bool(const char *key){
    int index = find_index_by_key(key);
    if (index < 0) return false;
    return g_items[index].enabled;
}

void settings_service_set_bool(const char *key, bool enabled) {
    int index = find_index_by_key(key);
    if (index < 0) return;
    g_items[index].enabled = enabled;
    settings_service_save();
}

int settings_service_theme_count(void) {
    return g_theme_count;
}

const char *settings_service_theme_label(int index) {
    if (index < 0 || index >= g_theme_count) return "";
    return g_themes[index];
}

int settings_service_get_light_theme(void) {
    return g_light_theme;
}

void settings_service_set_light_theme(int index) {
    if (index < 0 || index >= g_theme_count) return;
    g_light_theme = index;
    settings_service_save();
}

void settings_service_reset_defaults(void) {
    for (int i = 0; i < g_item_count; i++) {
        g_items[i].enabled = g_items[i].default_enabled;
    }
    g_light_theme = SETTINGS_DEFAULT_THEME_INDEX;
    g_volume = SETTINGS_DEFAULT_VOLUME;
    g_brightness = SETTINGS_DEFAULT_BRIGHTNESS;
    g_timeout_sec = SETTINGS_DEFAULT_TIMEOUT_SEC;
    settings_service_save();
}

int settings_service_get_volume(void) { return g_volume; }
void settings_service_set_volume(int value) {
    if (value < SETTINGS_MIN_VOLUME) value = SETTINGS_MIN_VOLUME;
    if (value > SETTINGS_MAX_VOLUME) value = SETTINGS_MAX_VOLUME;
    g_volume = value;
    settings_service_save();
}

int settings_service_get_brightness(void) { return g_brightness; }
void settings_service_set_brightness(int value) {
    if (value < SETTINGS_MIN_BRIGHTNESS) value = SETTINGS_MIN_BRIGHTNESS;
    if (value > SETTINGS_MAX_BRIGHTNESS) value = SETTINGS_MAX_BRIGHTNESS;
    g_brightness = value;
    settings_service_save();
}

int settings_service_get_timeout_sec(void) { return g_timeout_sec; }
void settings_service_set_timeout_sec(int sec) {
    if (sec < SETTINGS_MIN_TIMEOUT_SEC) sec = SETTINGS_MIN_TIMEOUT_SEC;
    if (sec > SETTINGS_MAX_TIMEOUT_SEC) sec = SETTINGS_MAX_TIMEOUT_SEC;
    g_timeout_sec = sec;
    settings_service_save();
}
