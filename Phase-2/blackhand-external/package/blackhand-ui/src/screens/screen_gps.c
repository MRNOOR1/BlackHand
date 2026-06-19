#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "services/theme_service.h"
#include "ui-ipcs/modem_ipc.h"
#include "ui.h"

static int          s_gps_on    = 0;
static GpsLocation  s_loc       = {0};
static int          s_has_loc   = 0;
static int          s_draw_tick = 0;

/* ── draw ─────────────────────────────────────────────────────────────────── */

void screen_gps_draw(struct ncplane *phone)
{
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width  = INNER_WIDTH(cols);
    if (width < 10) return;

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "GPS");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x,
                          (rule && rule[0]) ? rule : "-");

    /* Poll location every ~10 draw calls (~2.5s at 250ms idle cadence) */
    if (s_gps_on && ++s_draw_tick >= 10) {
        s_draw_tick = 0;
        if (modem_ipc_get_location(&s_loc) == 0)
            s_has_loc = 1;
    }

    int top = CONTENT_START_ROW + 3;

    /* GPS on/off status */
    ghost_text(phone, top, CONTENT_COL, theme_text_muted(),
               s_gps_on ? "STATUS: ENABLED" : "STATUS: DISABLED");

    if (!s_gps_on) {
        ghost_text(phone, top + 2, CONTENT_COL, theme_text_muted(),
                   "GPS is off. Press right soft");
        ghost_text(phone, top + 3, CONTENT_COL, theme_text_muted(),
                   "key to enable.");
        ghost_softkeys(phone, "[Back]", "[Enable]");
        return;
    }

    if (!s_has_loc || !s_loc.has_fix) {
        ghost_text(phone, top + 2, CONTENT_COL, theme_text_muted(), "Acquiring fix...");
        ghost_text(phone, top + 3, CONTENT_COL, theme_text_muted(), "No GPS lock yet");
    } else {
        char lat_buf[32], lon_buf[32], alt_buf[32], spd_buf[32];
        snprintf(lat_buf, sizeof(lat_buf), "LAT:  %.6f", s_loc.latitude);
        snprintf(lon_buf, sizeof(lon_buf), "LON:  %.6f", s_loc.longitude);
        snprintf(alt_buf, sizeof(alt_buf), "ALT:  %.1f m", s_loc.altitude);
        snprintf(spd_buf, sizeof(spd_buf), "SPD:  %.1f km/h", s_loc.speed);

        ghost_text(phone, top + 2, CONTENT_COL, theme_text_primary(), lat_buf);
        ghost_text(phone, top + 3, CONTENT_COL, theme_text_primary(), lon_buf);
        ghost_text(phone, top + 4, CONTENT_COL, theme_text_muted(),   alt_buf);
        ghost_text(phone, top + 5, CONTENT_COL, theme_text_muted(),   spd_buf);
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(),
               "Right soft: disable GPS");
    ghost_softkeys(phone, "[Back]", "[Disable]");
    (void)footer;
}

/* ── input ────────────────────────────────────────────────────────────────── */

screen_id screen_gps_input(uint32_t key)
{
    switch (key) {
        case KEY_SOFT_LEFT_ACTION:
        case KEY_ACTION_SECONDARY:         /* D = back to home */
            return SCREEN_HOME;

        case KEY_SOFT_RIGHT_ACTION:
        case KEY_ACTION_PRIMARY:           /* A = toggle GPS */
        case NCKEY_ENTER: case '\n':
            if (s_gps_on) {
                modem_ipc_gps_disable();
                s_gps_on    = 0;
                s_has_loc   = 0;
                s_draw_tick = 0;
            } else {
                if (modem_ipc_gps_enable() == 0)
                    s_gps_on = 1;
            }
            return SCREEN_GPS;

        default:
            return SCREEN_GPS;
    }
}
