/*
 * screen_theme.c — SYSTEM > THEME picker.
 *
 * Lists the 8 spec themes by id (theme_service owns the table; settings owns
 * the persisted index). Selecting applies instantly — the whole UI re-renders
 * in the new tokens on the next frame, and the choice persists across boots
 * via settings_service.
 */
#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

    ghost_text(phone, CONTENT_START_ROW, CONTENT_COL,
               theme_text_primary(), "THEME");
    ghost_text(phone, CONTENT_START_ROW, CONTENT_COL + 6,
               theme_text_muted(), theme_active()->id);

    ncplane_set_fg_rgb(phone, theme_border());
    ncplane_set_bg_rgb(phone, theme_bg());
    const char *rule = theme_rule_glyph();
    int width = INNER_WIDTH(cols);
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x,
                          (rule && rule[0]) ? rule : "-");

    int count = theme_count();
    if (s_selected < 0) s_selected = 0;
    if (s_selected >= count) s_selected = count - 1;

    int active = settings_service_get_light_theme();

    for (int i = 0; i < count; i++) {
        int row = CONTENT_START_ROW + 3 + i;
        if (row >= footer - 1) break;

        /* Row renders in the CURRENT theme's selection style — previewing a
         * theme means applying it; this list is the live preview. */
        char label[24];
        snprintf(label, sizeof(label), "%s%s",
                 (i == active) ? "* " : "  ", theme_id_at(i));
        ghost_list_row(phone, row, (int)cols, i, (i == s_selected), label);
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(),
               "Enter applies instantly · * = saved");
    ghost_softkeys(phone, "[Back]", "[Apply]");
}

screen_id screen_theme_input(uint32_t key) {
    int count = theme_count();
    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_THEME;
        case NCKEY_DOWN:
            if (s_selected < count - 1) s_selected++;
            return SCREEN_THEME;
        case NCKEY_LEFT:
        case KEY_SOFT_LEFT_ACTION:
            return SCREEN_SETTINGS;
        case NCKEY_RIGHT:
        case NCKEY_ENTER:
        case '\n':
        case KEY_SOFT_RIGHT_ACTION:
            settings_service_set_light_theme(s_selected);   /* persists */
            theme_service_sync_from_settings();             /* applies  */
            return SCREEN_THEME;
        default:
            return SCREEN_THEME;
    }
}
