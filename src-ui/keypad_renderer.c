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
 *  INPUT MAPPING (dev keyboard):
 *    q/Q       = Left Soft Key (LSK)
 *    e/E       = Right Soft Key (RSK)
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

    int ksr = KEYPAD_START_ROW;
    if (ksr >= (int)rows) return;
    int kh = (int)rows - ksr;
    int center = (int)cols / 2;

    /* ── Background fill for keypad area ─────────────────────────────── */
    ghost_fill_rect(phone, ksr, 0, kh, (int)cols,
                    ' ', theme_bg(), theme_bg());

    /* ── Keypad border frame ─────────────────────────────────────────── */
    ncplane_set_fg_rgb(phone, theme_border());
    ncplane_set_bg_rgb(phone, theme_bg());

    if (cols >= 2 && kh >= 2) {
        ncplane_putstr_yx(phone, ksr, 0, "\u250c");
        for (int x = 1; x < (int)cols - 1; x++) {
            ncplane_putstr_yx(phone, ksr, x, "\u2500");
        }
        ncplane_putstr_yx(phone, ksr, (int)cols - 1, "\u2510");

        for (int y = ksr + 1; y < (int)rows - 1; y++) {
            ncplane_putstr_yx(phone, y, 0, "\u2502");
            ncplane_putstr_yx(phone, y, (int)cols - 1, "\u2502");
        }

        ncplane_putstr_yx(phone, (int)rows - 1, 0, "\u2514");
        for (int x = 1; x < (int)cols - 1; x++) {
            ncplane_putstr_yx(phone, (int)rows - 1, x, "\u2500");
        }
        ncplane_putstr_yx(phone, (int)rows - 1, (int)cols - 1, "\u2518");
    }

    /* ── Soft keys row (ksr + 1) ─────────────────────────────────────── */
    int sk_row = KEYPAD_SOFTKEY_ROW;
    if (sk_row >= (int)rows - 1) return;
    int lsk_active = (active_key == 'q' || active_key == 'Q');
    int rsk_active = (active_key == 'e' || active_key == 'E');

    int side_pad = 2;
    int sk_gap = 2;
    int sk_w = ((int)cols - (2 * side_pad) - sk_gap) / 2;
    if (sk_w < 7) sk_w = 7;
    int left_sk_col = side_pad;
    int right_sk_col = (int)cols - side_pad - sk_w;

    draw_button_fixed(phone, sk_row, left_sk_col, sk_w, "LSK", lsk_active);
    draw_button_fixed(phone, sk_row, right_sk_col, sk_w, "RSK", rsk_active);

    /* ── D-pad (3 rows starting at ksr + 3) ──────────────────────────── */
    int dpad_row = KEYPAD_DPAD_ROW;
    if (dpad_row + 2 >= (int)rows - 1) return;

    /* Up arrow */
    int up_active = (active_key == NCKEY_UP);
    ncplane_set_fg_rgb(phone, up_active ? theme_text_primary() : theme_text_muted());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, dpad_row, center, "\u25B2");

    /* Left arrow, OK button, Right arrow */
    int left_active  = (active_key == NCKEY_LEFT);
    int ok_active    = (active_key == NCKEY_ENTER || active_key == '\n');
    int right_active = (active_key == NCKEY_RIGHT);

    ncplane_set_fg_rgb(phone, left_active ? theme_text_primary() : theme_text_muted());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, dpad_row + 1, center - 5, "\u25C0");

    draw_button(phone, dpad_row + 1, center - 2, "OK", ok_active);

    ncplane_set_fg_rgb(phone, right_active ? theme_text_primary() : theme_text_muted());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, dpad_row + 1, center + 4, "\u25B6");

    /* Down arrow */
    int down_active = (active_key == NCKEY_DOWN);
    ncplane_set_fg_rgb(phone, down_active ? theme_text_primary() : theme_text_muted());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, dpad_row + 2, center, "\u25BC");

    /* ── Numeric pad (4 rows of 3, starting at ksr + 7) ──────────────── */
    static const char *num_labels[4][3] = {
        { "1", "2", "3" },
        { "4", "5", "6" },
        { "7", "8", "9" },
        { "*", "0", "#" },
    };

    static const uint32_t num_keys[4][3] = {
        { '1', '2', '3' },
        { '4', '5', '6' },
        { '7', '8', '9' },
        { '*', '0', '#' },
    };

    int num_start = KEYPAD_NUM_ROW;
    int gap = 2;                           /* space between buttons */
    int row_spacing = 2;                   /* rows between numeric rows */
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
