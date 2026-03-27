#include <notcurses/notcurses.h>
#include <stdint.h>

#include "config.h"
#include "draw_utils.h"
#include "services/theme_service.h"
#include "ui.h"

void screen_bluetooth_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "BLUETOOTH");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++) {
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");
    }

    ghost_text(phone, CONTENT_START_ROW + 3, CONTENT_COL, theme_text_muted(), "Bluetooth backend disabled");
    ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_muted(), "Implement when ready");
    ghost_text(phone, CONTENT_START_ROW + 6, CONTENT_COL, theme_text_muted(), "service: services/bluetooth_service.c");
    ghost_softkeys(phone, "[Back]", "");
}

screen_id screen_bluetooth_input(uint32_t key) {
    switch (key) {
        case NCKEY_LEFT:
        case 'q':
        case 'Q':
            return SCREEN_SETTINGS;
        case 'e':
        case 'E':
            return SCREEN_HOME;
        default:
            return SCREEN_BLUETOOTH;
    }
}
