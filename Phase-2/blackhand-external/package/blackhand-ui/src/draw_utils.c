#include "draw_utils.h"
#include "config.h"
#include "bh_skin.h"
#include "services/theme_service.h"

#include <string.h>

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

/*
 * ghost_softkeys() now renders the active theme's decorative footer strip
 * (wordmark, corner ticks, ornament, sparkline, coordinates ...). The legacy
 * soft-key label arguments are accepted for source compatibility but the
 * footer is theme-owned, exactly like the design pack's gallery. Functional
 * key hints follow the global 6-key convention (Q back / E action).
 */
void ghost_softkeys(struct ncplane *n, const char *left_label, const char *right_label) {
    (void)left_label;
    (void)right_label;
    bh_footer(n, NULL);
}

void ghost_confirm_popup(struct ncplane *n, const char *question, int yes_selected) {
    unsigned rows, cols;
    ncplane_dim_yx(n, &rows, &cols);
    int w = 30;
    int h = 5;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < 3) top = 3;
    if (left < 2) left = 2;

    (void)yes_selected;
    ghost_fill_rect(n, top, left, h, w, ' ', theme_text_primary(), theme_bg());
    ghost_text(n, top + 1, left + 2, theme_text_primary(), question ? question : "Are you sure?");
    ghost_text(n, top + 3, left + 2,  theme_affirm_border(), "[A] YES");
    ghost_text(n, top + 3, left + 14, theme_text_muted(),    "[Q] NO");
    ghost_softkeys(n, NULL, NULL);
}
