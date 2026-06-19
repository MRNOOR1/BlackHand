#include <notcurses/notcurses.h>
#include <stdint.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
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

    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_dim(), "CENTER APPLIES INSTANTLY");

    int count = theme_count();
    int active_idx = theme_active_index();
    if (s_selected < 0) s_selected = 0;
    if (count > 0 && s_selected >= count) s_selected = count - 1;

    /* keep the selected row on screen (12 themes can overflow short panels) */
    int list_start = CONTENT_START_ROW + 4;
    int max_rows = footer - list_start;
    if (max_rows < 1) max_rows = 1;
    int scroll = 0;
    if (s_selected >= max_rows) scroll = s_selected - max_rows + 1;

    for (int i = scroll; i < count; i++) {
        int row = list_start + (i - scroll);
        if (row >= footer) break;
        int active = (active_idx == i);

        /* row label carries a marker when this theme is the live one */
        bh_list_item(phone, row, CONTENT_COL, width - 3,
                     theme_at(i)->label, active ? "◀" : "", i == s_selected, i);

        /* swatch preview in the candidate theme's own bright accent */
        const bh_theme_t *pt = theme_at(i);
        ncplane_set_bg_rgb(phone, pt->bright);
        ncplane_set_fg_rgb(phone, pt->bright);
        ncplane_putchar_yx(phone, row, CONTENT_COL + width - 2, ' ');
        ncplane_putchar_yx(phone, row, CONTENT_COL + width - 1, ' ');
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
        case KEY_SOFT_RIGHT_ACTION:
            settings_service_set_light_theme(s_selected);
            theme_service_sync_from_settings();
            return SCREEN_THEME;
        case KEY_SOFT_LEFT_ACTION:
            return SCREEN_SETTINGS;
        default:
            return SCREEN_THEME;
    }
}
