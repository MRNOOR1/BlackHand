#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "services/bluetooth_service.h"
#include "services/settings_service.h"
#include "services/theme_service.h"
#include "ui.h"

static int s_selected = 0;
static int s_refreshed = 0;

void screen_bluetooth_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "BLUETOOTH");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++) {
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");
    }

    if (!bluetooth_service_is_available()) {
        ghost_text(phone, CONTENT_START_ROW + 3, CONTENT_COL, theme_text_muted(), "No BT adapter found");
        ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_muted(), "Plug in USB BT dongle");
        ghost_softkeys(phone, "[Back]", "[Menu]");
        return;
    }

    int enabled = settings_service_get_bool(SETTINGS_KEY_BLUETOOTH) ? 1 : 0;
    char state_line[64];
    snprintf(state_line, sizeof(state_line), "STATE: %s",
             enabled ? "ON" : "OFF");
    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), state_line);

    if (!enabled) {
        ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_muted(), "Press center to enable");
        ghost_softkeys(phone, "[Back]", "[Menu]");
        return;
    }

    if (!s_refreshed) {
        ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_muted(), "Press right arrow to scan");
        ghost_softkeys(phone, "[Back]", "[Scan]");
        return;
    }

    size_t count = bluetooth_service_device_count();
    if (count == 0) {
        ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_muted(), "No audio devices found");
        ghost_text(phone, CONTENT_START_ROW + 5, CONTENT_COL, theme_text_muted(), "Press right arrow to rescan");
        ghost_softkeys(phone, "[Back]", "[Menu]");
        return;
    }

    if (s_selected < 0) s_selected = 0;
    if (s_selected >= (int)count) s_selected = (int)count - 1;

    for (size_t i = 0; i < count; i++) {
        const BtDevice *d = bluetooth_service_device_at(i);
        if (!d) continue;
        int row = CONTENT_START_ROW + 4 + (int)i;
        if (row >= footer) break;

        char status[24];
        if (d->connected) snprintf(status, sizeof(status), "CONNECTED");
        else if (d->paired) snprintf(status, sizeof(status), "PAIRED");
        else snprintf(status, sizeof(status), "READY");

        char nm[20]; snprintf(nm, sizeof(nm), "%.14s", d->name[0] ? d->name : "?");
        bh_list_item(phone, row, CONTENT_COL, INNER_WIDTH(cols), nm, status,
                     (int)i == s_selected, (int)i);
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "OK:Connect  \u25B6:Rescan  \u25C0:Back");
    ghost_softkeys(phone, "[Back]", "[Menu]");
}

screen_id screen_bluetooth_input(uint32_t key) {
    size_t count = bluetooth_service_device_count();
    int enabled = settings_service_get_bool(SETTINGS_KEY_BLUETOOTH) ? 1 : 0;

    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_BLUETOOTH;
        case NCKEY_DOWN:
            if (s_selected < (int)count - 1) s_selected++;
            return SCREEN_BLUETOOTH;
        case NCKEY_LEFT:
        case KEY_SOFT_LEFT_ACTION:
            s_refreshed = 0;
            return SCREEN_SETTINGS;
        case KEY_SOFT_RIGHT_ACTION:
            s_refreshed = 0;
            return SCREEN_HOME;
        case NCKEY_RIGHT:
            if (enabled) {
                bluetooth_service_refresh_devices();
                s_refreshed = 1;
            }
            return SCREEN_BLUETOOTH;
        case NCKEY_ENTER:
        case '\n':
            if (!enabled) {
                settings_service_set_bool(SETTINGS_KEY_BLUETOOTH, true);
                s_refreshed = 0;
                return SCREEN_BLUETOOTH;
            }

            if (count > 0) {
                const BtDevice *d = bluetooth_service_device_at((size_t)s_selected);
                if (d) {
                    if (d->connected) bluetooth_service_disconnect(d->mac);
                    else bluetooth_service_connect(d->mac);
                    bluetooth_service_refresh_devices();
                }
            }
            return SCREEN_BLUETOOTH;
        default:
            return SCREEN_BLUETOOTH;
    }
}
