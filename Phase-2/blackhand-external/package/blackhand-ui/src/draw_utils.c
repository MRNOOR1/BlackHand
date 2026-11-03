#include "draw_utils.h"
#include "config.h"
#include "bh_skin.h"
#include "services/theme_service.h"

#include <string.h>
#include <stdio.h>

void ghost_set(struct ncplane *n, uint32_t fg) {
    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, theme_bg());
}

void ghost_text(struct ncplane *n, int row, int col,
                      uint32_t colour, const char *text) {
    ghost_set(n, colour);
    ncplane_putstr_yx(n, row, col, text);
}

void ghost_hline(struct ncplane *n, int row, int col,
                       int length, const char *glyph, uint32_t colour) {
    ghost_set(n, colour);
    for (int i = 0; i < length; i++) {
        ncplane_putstr_yx(n, row, col + i, glyph);
    }
}

void ghost_fill_rect(struct ncplane *n,
                            int row, int col, int h, int w,
                             char ch, uint32_t fg, uint32_t bg) {
    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, bg);
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            ncplane_putchar_yx(n, row + r, col + c, ch);
        }
    }
}

void ghost_label_value(struct ncplane *n,
                                int row, int label_col, int value_col,
                                const char *label, const char *value) {
    ghost_text(n, row, label_col, theme_text_muted(), label);
    ghost_text(n, row, value_col, theme_text_primary(), value);
}

/* Functional actions remain visible in every theme. */
void ghost_softkeys(struct ncplane *n, const char *left_label, const char *right_label) {
    bh_footer(n, NULL);
    if (!left_label && !right_label) return;
    unsigned rows, cols; ncplane_dim_yx(n, &rows, &cols);
    int available = (int)cols - 2 * CONTENT_COL;
    if (available < 4 || rows < 3) return;
    int half = available / 2;
    char left[64], right[64];
    snprintf(left, sizeof(left), "%.*s", half, left_label ? left_label : "");
    snprintf(right, sizeof(right), "%.*s", half, right_label ? right_label : "");
    ghost_fill_rect(n, (int)rows - 2, CONTENT_COL, 1, available, ' ', theme_text_primary(), theme_bg());
    ghost_text(n, (int)rows - 2, CONTENT_COL, theme_text_primary(), left);
    ghost_text(n, (int)rows - 2, (int)cols - CONTENT_COL - (int)strlen(right), theme_text_primary(), right);
}

void ghost_confirm_popup(struct ncplane *n, const char *question, int yes_selected) {
    unsigned rows, cols;
    ncplane_dim_yx(n, &rows, &cols);
    /* Clamp the box to the plane so it never overflows the right edge: leave a
     * 1-col margin each side, but keep enough width for the "[A] YES / [Q] NO"
     * row (needs ~21 cols). */
    int w = (int)cols - 2;
    if (w > 28) w = 28;
    if (w < 21) w = 21;
    int h = 5;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < 3) top = 3;
    if (left < 1) left = 1;

    (void)yes_selected;
    ghost_fill_rect(n, top, left, h, w, ' ', theme_text_primary(), theme_bg());
    ghost_text(n, top + 1, left + 2, theme_text_primary(), question ? question : "Are you sure?");
    ghost_text(n, top + 3, left + 2,  theme_affirm_border(), "[A] YES");
    ghost_text(n, top + 3, left + 14, theme_text_muted(),    "[Q] NO");
    ghost_softkeys(n, NULL, NULL);
}
