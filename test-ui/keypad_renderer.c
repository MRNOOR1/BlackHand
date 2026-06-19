/*
 * ============================================================================
 *  keypad_renderer.c — On-Screen Keypad Visual Rendering
 * ============================================================================
 *
 *  Draws a visual representation of the phone's physical keypad below the
 *  screen content area.  This is purely visual feedback — it shows the user
 *  what keys are available and highlights the active key on press.
 *
 *  KEYPAD LAYOUT:
 *  ──────────────
 *    [LSK]                              [RSK]     ← Soft keys
 *
 *                    ▲                             ← D-pad
 *                ◀  [OK]  ▶
 *                    ▼
 *
 *             [ 1 ] [ 2 ] [ 3 ]                   ← Numeric pad
 *             [ 4 ] [ 5 ] [ 6 ]
 *             [ 7 ] [ 8 ] [ 9 ]
 *             [ * ] [ 0 ] [ # ]
 *
 *  INPUT MAPPING (from config.h):
 *    KEY_BIND_SOFT_LEFT  = Left Soft Key (LSK)
 *    KEY_BIND_SOFT_RIGHT = Right Soft Key (RSK)
 *    Arrow keys = D-pad directions
 *    Enter     = D-pad center / OK
 *    0-9, *, # = Numeric keypad
 * ============================================================================
 */

#include "keypad_renderer.h"
#include "config.h"
#include "draw_utils.h"
#include "services/theme_service.h"

#include <stdint.h>
#include <string.h>

keypad_layout keypad_layout_compute(int rows, int cols) {
    (void)cols;

    const int min_screen_rows = 12;
    const int min_keypad_rows = 12;

    int keypad_rows = (rows * 40) / 100;
    if (keypad_rows < min_keypad_rows) keypad_rows = min_keypad_rows;
    if (rows - keypad_rows < min_screen_rows) keypad_rows = rows - min_screen_rows;
    if (keypad_rows < min_keypad_rows) keypad_rows = min_keypad_rows;
    if (keypad_rows > rows) keypad_rows = rows;

    int start_row = rows - keypad_rows;
    if (start_row < 0) start_row = 0;

    int kh = rows - start_row;
    int softkey_row = start_row + 1;
    int dpad_row = start_row + ((kh >= 14) ? 3 : 2);
    int num_start_row = dpad_row + ((kh >= 14) ? 4 : 3);

    int rows_after_num = rows - num_start_row - 1;
    int row_spacing = rows_after_num / 4;
    if (row_spacing < 1) row_spacing = 1;
    if (row_spacing > 2) row_spacing = 2;

    keypad_layout out = {
        .start_row = start_row,
        .softkey_row = softkey_row,
        .dpad_row = dpad_row,
        .num_start_row = num_start_row,
        .row_spacing = row_spacing,
    };
    return out;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Helper: draw a single "button" at (row, col) with a label
 *
 *  A button looks like:  [ OK ]  or  [ 5 ]
 *  Width is always label_len + 4 (brackets + spaces).
 *  If 'highlight' is true, the button uses primary colour; otherwise muted.
 * ────────────────────────────────────────────────────────────────────────── */
static void draw_button(struct ncplane *n, int row, int col,
                        const char *label, int highlight) {
    uint32_t fg = highlight ? theme_text_primary() : theme_text_muted();
    uint32_t bg = highlight ? theme_border() : theme_bg();

    /* Draw bracket + label + bracket */
    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, bg);
    ncplane_putstr_yx(n, row, col, "[");

    ncplane_set_fg_rgb(n, highlight ? theme_bg() : theme_text_muted());
    ncplane_set_bg_rgb(n, bg);
    ncplane_putstr_yx(n, row, col + 1, " ");
    ncplane_putstr_yx(n, row, col + 2, label);
    int label_len = (int)strlen(label);
    ncplane_putstr_yx(n, row, col + 2 + label_len, " ");

    ncplane_set_fg_rgb(n, fg);
    ncplane_putstr_yx(n, row, col + 3 + label_len, "]");

    /* Reset to default */
    ncplane_set_fg_rgb(n, theme_text_muted());
    ncplane_set_bg_rgb(n, theme_bg());
}

static void draw_button_fixed(struct ncplane *n, int row, int col, int width,
                              const char *label, int highlight) {
    int label_len = (int)strlen(label);
    if (width < label_len + 4) {
        width = label_len + 4;
    }

    uint32_t fg = highlight ? theme_text_primary() : theme_text_muted();
    uint32_t bg = highlight ? theme_border() : theme_bg();

    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, bg);
    ncplane_putstr_yx(n, row, col, "[");

    for (int i = 1; i < width - 1; i++) {
        ncplane_putstr_yx(n, row, col + i, " ");
    }

    int label_col = col + 1 + ((width - 2 - label_len) / 2);
    ncplane_set_fg_rgb(n, highlight ? theme_bg() : theme_text_muted());
    ncplane_putstr_yx(n, row, label_col, label);

    ncplane_set_fg_rgb(n, fg);
    ncplane_putstr_yx(n, row, col + width - 1, "]");

    ncplane_set_fg_rgb(n, theme_text_muted());
    ncplane_set_bg_rgb(n, theme_bg());
}

/* ──────────────────────────────────────────────────────────────────────────
 *  draw_keypad()  —  Render the full on-screen keypad
 *
 *  Draws onto the phone plane in rows KEYPAD_START_ROW and below.
 *  active_key: the key code currently pressed (for highlight), or 0.
 * ────────────────────────────────────────────────────────────────────────── */
void draw_keypad(struct ncplane *phone, uint32_t active_key) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    keypad_layout layout = keypad_layout_compute((int)rows, (int)cols);
    int ksr = layout.start_row;
    if (ksr >= (int)rows) return;
    int kh = (int)rows - ksr;
    int center = (int)cols / 2;

    /* ── Background fill for keypad area ─────────────────────────────── */
    ghost_fill_rect(phone, ksr, 0, kh, (int)cols,
                    ' ', theme_bg(), theme_bg());

    /* ── Keypad top separator (borderless layout) ───────────────────── */
    ncplane_set_fg_rgb(phone, theme_border());
    ncplane_set_bg_rgb(phone, theme_bg());

    if (cols >= 2) {
        for (int x = 0; x < (int)cols; x++) {
            ncplane_putstr_yx(phone, ksr, x, "\u2500");
        }
    }

    /* ── Soft keys row (ksr + 1) ─────────────────────────────────────── */
    int sk_row = layout.softkey_row;
    if (sk_row >= (int)rows - 1) return;
    int lsk_active = (active_key == KEY_SOFT_LEFT_ACTION);
    int rsk_active = (active_key == KEY_SOFT_RIGHT_ACTION);

    int side_pad = 2;
    int sk_gap = 2;
    int sk_w = ((int)cols - (2 * side_pad) - sk_gap) / 2;
    if (sk_w < 7) sk_w = 7;
    int left_sk_col = side_pad;
    int right_sk_col = (int)cols - side_pad - sk_w;

    draw_button_fixed(phone, sk_row, left_sk_col, sk_w, "LSK", lsk_active);
    draw_button_fixed(phone, sk_row, right_sk_col, sk_w, "RSK", rsk_active);

    /* ── D-pad (3 rows of proper button rectangles) ─────────────────── */
    /*
     *  Row 1:  [        UP        ]
     *  Row 2:  [ LEFT ] [  OK  ] [ RIGHT ]
     *  Row 3:  [       DOWN       ]
     *
     *  Each arrow key is a full draw_button_fixed() rectangle so it is
     *  clearly visible and easy to tap on a touchscreen.
     */
    int dpad_row = layout.dpad_row;
    if (dpad_row + 2 >= (int)rows - 1) return;

    int dpad_gap = 1;
    int dpad_btn_w = ((int)cols - (2 * side_pad) - (2 * dpad_gap)) / 3;
    if (dpad_btn_w < 5) dpad_btn_w = 5;
    int dpad_total_w = 3 * dpad_btn_w + 2 * dpad_gap;
    int dpad_left = center - dpad_total_w / 2;
    int dpad_mid_col = dpad_left + dpad_btn_w + dpad_gap;
    int dpad_right_col = dpad_mid_col + dpad_btn_w + dpad_gap;

    /* UP — full width across all 3 columns */
    int up_active = (active_key == NCKEY_UP);
    draw_button_fixed(phone, dpad_row, dpad_left, dpad_total_w, "\u25B2 UP", up_active);

    /* LEFT / OK / RIGHT — 3 equal buttons */
    int left_active  = (active_key == NCKEY_LEFT);
    int ok_active    = (active_key == NCKEY_ENTER || active_key == '\n');
    int right_active = (active_key == NCKEY_RIGHT);

    draw_button_fixed(phone, dpad_row + 1, dpad_left, dpad_btn_w, "\u25C0", left_active);
    draw_button_fixed(phone, dpad_row + 1, dpad_mid_col, dpad_btn_w, "OK", ok_active);
    draw_button_fixed(phone, dpad_row + 1, dpad_right_col, dpad_btn_w, "\u25B6", right_active);

    /* DOWN — full width across all 3 columns */
    int down_active = (active_key == NCKEY_DOWN);
    draw_button_fixed(phone, dpad_row + 2, dpad_left, dpad_total_w, "\u25BC DOWN", down_active);

    /* ── Numeric pad (4 rows of 3, starting at ksr + 7) ──────────────── */
    static const char *num_labels[4][3] = {
        { "1", "2ABC", "3DEF" },
        { "4GHI", "5JKL", "6MNO" },
        { "7PQRS", "8TUV", "9WXYZ" },
        { "*.,!?", "0 SPC", "#CASE" },
    };

    static const uint32_t num_keys[4][3] = {
        { '1', '2', '3' },
        { '4', '5', '6' },
        { '7', '8', '9' },
        { '*', '0', '#' },
    };

    int num_start = layout.num_start_row;
    int gap = 2;                           /* space between buttons */
    int row_spacing = layout.row_spacing;  /* rows between numeric rows */
    int btn_w = ((int)cols - (2 * side_pad) - (2 * gap)) / 3;
    if (btn_w < 5) btn_w = 5;
    int total_w = 3 * btn_w + 2 * gap;
    int num_left = center - total_w / 2;

    for (int r = 0; r < 4; r++) {
        int row = num_start + r * row_spacing;
        if (row >= (int)rows) break;
        for (int c = 0; c < 3; c++) {
            int col = num_left + c * (btn_w + gap);
            int highlight = (active_key == num_keys[r][c]);
            draw_button_fixed(phone, row, col, btn_w, num_labels[r][c], highlight);
        }
    }
}
