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


/*
 * Rounded-corner safety (spi-display-bringup.md §6.3, recomputed for 10x18).
 *
 * The panel corners are rounded (~28 px radius). With the 10x18 font that is
 * 2.8 cols wide x 1.55 rows tall, so it clips only the first and last character
 * rows: on those two rows, columns 0-2 and (cols-3)..(cols-1) sit under the
 * bezel and must never carry a glyph. Rows 1..(rows-2) are full width.
 *
 * The frame draws a DOUBLE-LINE border (theme_border() colored) that mirrors
 * the glass:
 *   - Top/bottom runs and their corner turns (╔ ╗ ╚ ╝) are inset to the safe
 *     span [BORDER_CORNER_INSET, cols-1-BORDER_CORNER_INSET]; no glyph lands in
 *     an extreme corner cell.
 *   - Left/right sides (║) run only on the full-width rows 1..(rows-2), on the
 *     extreme columns, so they never touch a rounded corner cell.
 * The bottom row is thus a finished double-line edge (cols inset..), not empty
 * bleed; only the four corner cells stay background-only.
 */
#define BORDER_CORNER_INSET 3   /* first safe column on the top/bottom rows */

static void draw_border(struct ncplane *phone, int rows, int cols) {
    ncplane_set_fg_rgb(phone, theme_border());
    ncplane_set_bg_rgb(phone, theme_bg());

    int x0  = BORDER_CORNER_INSET;
    int x1  = cols - 1 - BORDER_CORNER_INSET;   /* last safe column */
    int top = 0, bot = rows - 1;
    if (x1 <= x0) return;                        /* plane too narrow */

    /* top and bottom double rules, corner turns inside the safe span */
    ncplane_putstr_yx(phone, top, x0, "╔");
    ncplane_putstr_yx(phone, top, x1, "╗");
    ncplane_putstr_yx(phone, bot, x0, "╚");
    ncplane_putstr_yx(phone, bot, x1, "╝");
    for (int x = x0 + 1; x < x1; x++) {
        ncplane_putstr_yx(phone, top, x, "═");
        ncplane_putstr_yx(phone, bot, x, "═");
    }

    /* left and right double sides on the full-width rows only */
    for (int y = 1; y <= rows - 2; y++) {
        ncplane_putstr_yx(phone, y, 0,        "║");
        ncplane_putstr_yx(phone, y, cols - 1, "║");
    }
}

void draw_frame(struct ncplane *phone, int tick,
                       const char *screen_name) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    ncplane_erase(phone);
    if (rows < (unsigned)FRAME_MIN_ROWS || cols < (unsigned)FRAME_MIN_COLS)
        return;

    /* ── Full-bleed background (covers every cell, including the four
     *    background-only corner cells — see the corner-safety note above) ── */
    ghost_fill_rect(phone, 0, 0, (int)rows, (int)cols, ' ', theme_bg(), theme_bg());

    /* ── Double-line themed perimeter border ─────────────────────────────── */
    draw_border(phone, (int)rows, (int)cols);

    /* ── Themed status strip on STATUS_ROW (row 1), never on the top
     *    corner-bleed row. The instrument theme also uses row 2. ────────── */
    bh_status_strip(phone, tick);

    /* ── Themed divider on row 2, carrying the screen tag. The instrument
     *    theme integrates its divider into the telemetry strip, so skip it
     *    there to avoid a doubled rule. ─────────────────────────────────── */
    if (!bh_divider_in_status()) {
        bh_divider(phone, 2, bh_screen_tag(screen_name));
    }
}
