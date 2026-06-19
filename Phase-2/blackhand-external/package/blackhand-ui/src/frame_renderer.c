#include "frame_renderer.h"
#include "draw_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "bh_skin.h"
#include "platform/hardware.h"
#include "services/theme_service.h"


void draw_battery(struct ncplane *phone, int percent,
                         bool charging, int tick) {

    int segs = (percent + 24) / 25;
    if (segs > 4) segs = 4;
    if (segs < 0) segs = 0;

    if (percent < 15 && !charging) {
        if ((tick % 10) >= 5) {
            ghost_set(phone, theme_bg());
            ncplane_putstr_yx(phone, STATUS_ROW, STATUS_BATTERY_COL,
                              "               ");
            return;
        }
    }

    char label[24];
    static const char *bars[] = { "░░░░", "█░░░", "██░░", "███░", "████" };

    if (charging) {
        ghost_set(phone, theme_text_primary());
        snprintf(label, sizeof(label), "BAT %s %3d%% CHG", bars[segs], percent);
    } else if (percent < 15) {
        ghost_set(phone, COL_GHOST_LOW);
        snprintf(label, sizeof(label), "BAT %s %3d%% LOW", bars[segs], percent);
    } else {
        ghost_set(phone, theme_text_muted());
        snprintf(label, sizeof(label), "BAT %s %3d%%", bars[segs], percent);
    }
    ncplane_putstr_yx(phone, STATUS_ROW, STATUS_BATTERY_COL, label);
}

void draw_signal(struct ncplane *phone, int bars,
                        bool connected, int tick) {

    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    int sig_col = (int)cols - 13;

    if (sig_col < 1) return;

    int level = bars;
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    static const char *sigbars[] = { "░░░░", "▓░░░", "▓▓░░", "▓▓▓░", "▓▓▓▓" };

    char text[20];
    if (!connected) {
        ghost_set(phone, ((tick % 8) < 4) ? theme_text_muted() : theme_border());
        snprintf(text, sizeof(text), "SIG ░░░░ LOST");
    } else {
        ghost_set(phone, theme_text_muted());
        snprintf(text, sizeof(text), "SIG %s", sigbars[level]);
    }
    ncplane_putstr_yx(phone, STATUS_ROW, sig_col, text);
}

void draw_status_bar(struct ncplane *phone, int tick) {
    battery_status_t  batt = hardware_get_battery();
    cellular_status_t cell = hardware_get_cellular();
    draw_battery(phone, batt.percent,     batt.charging,  tick);
    draw_signal (phone, cell.signal_bars, cell.connected, tick);

    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    (void)rows;

    time_t now = time(NULL);
    struct tm tm_now;
    if (localtime_r(&now, &tm_now)) {
        char clockbuf[8];
        snprintf(clockbuf, sizeof(clockbuf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        int col = (int)cols - 20;
        if (col < 20) col = 20;
        ghost_set(phone, theme_text_primary());
        ncplane_putstr_yx(phone, STATUS_ROW, col, clockbuf);
    }
}

void draw_frame(struct ncplane *phone, int tick,
                       const char *screen_name) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    ncplane_erase(phone);
    if (rows < (unsigned)FRAME_MIN_ROWS || cols < (unsigned)FRAME_MIN_COLS)
        return;

    /* ── Full-bleed background (borderless) ───────────────────────────── */
    ghost_fill_rect(phone, 0, 0, (int)rows, (int)cols, ' ', theme_bg(), theme_bg());

    /* ── Themed status strip (row 0, +row 1 on the instrument theme) ──── */
    bh_status_strip(phone, tick);

    /* ── Themed divider on row 2, carrying the screen tag. The instrument
     *    theme integrates its divider into the telemetry strip, so skip it
     *    there to avoid a doubled rule. ─────────────────────────────────── */
    if (!bh_divider_in_status()) {
        bh_divider(phone, 2, bh_screen_tag(screen_name));
    }
}
