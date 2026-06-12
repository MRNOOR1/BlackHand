#ifndef DRAW_UTILS_H
#define DRAW_UTILS_H

#include <stdint.h>
#include <notcurses/notcurses.h>

void ghost_set(struct ncplane *n, uint32_t fg);
void ghost_text(struct ncplane *n, int row, int col, uint32_t colour, const char *text);
void ghost_hline(struct ncplane *n, int row, int col, int length, const char *glyph, uint32_t colour);
void ghost_fill_rect(struct ncplane *n, int row, int col, int h, int w, char ch, uint32_t fg, uint32_t bg);
void ghost_label_value(struct ncplane *n, int row, int label_col, int value_col, const char *label, const char *value);
void ghost_softkeys(struct ncplane *n, const char *left_label, const char *right_label);

/* ── themed list row (UI Theme Spec §5) ─────────────────────────────────────
 * Renders one menu/list row according to the active theme's sel_style:
 *   SEL_INVERT  — inverted full-width bar + "> " cursor
 *   SEL_CURSOR  — "> " cursor, bright text
 *   SEL_BRACKET — centered [ LABEL ] in accent
 *   SEL_DIAMOND — ◆ selected / ◇ unselected
 *   SEL_INDEX   — "01 LABEL" + trailing ■ when selected
 *   SEL_HEAT    — inverted with accent bg (heat binding pending)
 * `index` is 0-based; used only by SEL_INDEX. Layout never changes with
 * theme — only glyphs and colors do. */
void ghost_list_row(struct ncplane *n, int row, int cols,
                    int index, int selected, const char *label);

/* Same, with a right-aligned meta column (time, ON/OFF, duration, 0x0n).
 * Implements all 10 selection styles from the design pack. Feeds the frame
 * list tracker so FOOTER_READOUT can show SEL 0n/0N. */
void ghost_list_row_meta(struct ncplane *n, int row, int cols,
                         int index, int selected,
                         const char *label, const char *meta);

/* ── action cells (design pack §2) ──────────────────────────────────────────
 * Decision screens replace the footer with two bordered cells in the GLOBAL
 * semantic colors: left = affirmative green, right = destructive red.
 * destruct == NULL renders a single centered affirmative cell.
 * focused: 0 = affirm side, 1 = destruct side, -1 = no focus shown. */
void ghost_action_cells(struct ncplane *n,
                        const char *affirm, const char *destruct, int focused);
void ghost_confirm_popup(struct ncplane *n, const char *question, int yes_selected);

#endif
