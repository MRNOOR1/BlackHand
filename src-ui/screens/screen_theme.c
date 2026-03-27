#include <notcurses/notcurses.h>
#include <stdint.h>

#include "config.h"
#include "draw_utils.h"
#include "services/settings_service.h"
#include "services/theme_service.h"
#include "ui.h"

static int s_selected = 0;

void screen_theme_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "THEMES");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++) {
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");
    }

    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), "MODE: ACCENT THEME");

    int count = settings_service_theme_count();
    if (s_selected < 0) s_selected = 0;
    if (count > 0 && s_selected >= count) s_selected = count - 1;

    for (int i = 0; i < count; i++) {
        int row = CONTENT_START_ROW + 4 + i;
        if (row >= footer) break;
        int active = (settings_service_get_light_theme() == i);
        const char *cursor = (i == s_selected) ? MENU_CURSOR : MENU_CURSOR_BLANK;
        ncplane_set_fg_rgb(phone, (i == s_selected) ? theme_selection_text() : theme_text_muted());
        ncplane_set_bg_rgb(phone, (i == s_selected) ? theme_selection_bg() : theme_bg());
        ncplane_putstr_yx(phone, row, CONTENT_COL, cursor);
        ncplane_putstr_yx(phone, row, CONTENT_COL + 2, active ? "[*]" : "[ ]");
        ncplane_putstr_yx(phone, row, CONTENT_COL + 6, settings_service_theme_label(i));
    }

    ghost_softkeys(phone, "[Back]", "[Apply]");
}

screen_id screen_theme_input(uint32_t key) {
    int count = settings_service_theme_count();
    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_THEME;
        case NCKEY_DOWN:
            if (s_selected < count - 1) s_selected++;
            return SCREEN_THEME;
        case NCKEY_LEFT:
            return SCREEN_SETTINGS;  /* Same as LSK — go back */
        case NCKEY_RIGHT:
            settings_service_set_light_theme(s_selected);
            theme_service_sync_from_settings();
            return SCREEN_THEME;     /* Same as RSK — apply */
        case NCKEY_ENTER:
        case '\n':
        case 'e':
        case 'E':
            settings_service_set_light_theme(s_selected);
            theme_service_sync_from_settings();
            return SCREEN_THEME;
        case 'q':
        case 'Q':
            return SCREEN_SETTINGS;
        default:
            return SCREEN_THEME;
    }
}
