#include "settings_service.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key;
    const char *label;
    bool enabled;
    bool default_enabled;
} setting_item_t;

static setting_item_t g_items[] = {
    { "night_mode", "Night Mode", false, false },
    { "bluetooth",  "Bluetooth",  false, false },
    { "hand_white", "Hand White", false, false },
};

static const char *g_themes[] = {
    "Desert Storm",
    "Neon Grid",
    "Ocean Steel",
    "Solar Ember",
    "Monochrome Ops",
};

static const int g_item_count = (int)(sizeof(g_items) / sizeof(g_items[0]));
static const int g_theme_count = (int)(sizeof(g_themes) / sizeof(g_themes[0]));
static const char *SETTINGS_FILE = "settings.conf";
static int g_light_theme = 0;

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

        if (strcmp(key, "light_theme") == 0) {
            if (enabled >= 0 && enabled < g_theme_count) {
                g_light_theme = enabled;
            }
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
    fprintf(f, "light_theme=%d\n", g_light_theme);

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
    g_light_theme = 0;
    settings_service_save();
}
