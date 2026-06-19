#ifndef BH_SKIN_H
#define BH_SKIN_H
/*
 * bh_skin.h -- themed skeleton primitives.
 * ----------------------------------------------------------------------------
 * The "generic" renderers every screen reuses so that the active theme owns
 * the look: status strip, divider, footer, list rows (all 10 selection styles)
 * and the global semantic action cells. No screen draws raw colors; it asks
 * these helpers and the active theme decides the appearance.
 * ----------------------------------------------------------------------------
 */
#include <notcurses/notcurses.h>

/* Status strip on row 0 (+row 1 on the two-row instrument theme). */
void bh_status_strip(struct ncplane *n, int tick);

/* Divider rule under the status strip; tag is the 4-5 char screen name. */
void bh_divider(struct ncplane *n, int row, const char *tag);

/* Returns 1 when the active theme integrates its divider into the status strip
 * (instrument-bench) so callers can skip a separate divider row. */
int bh_divider_in_status(void);

/* Themed bottom footer strip (decorative, theme-specific). */
void bh_footer(struct ncplane *n, const char *tag);

/* One themed list row implementing the active theme's selection style.
 *   left  : the item label (required)
 *   right : right-aligned meta string (e.g. time / value), may be NULL/""
 *   sel   : non-zero if this row is selected
 *   index : zero-based row index (used by index / register styles) */
void bh_list_item(struct ncplane *n, int row, int col, int width,
                  const char *left, const char *right, int sel, int index);

/* Two global semantic action cells (left = affirmative, right = destructive).
 * focus: 0 = left focused, 1 = right focused, -1 = neither. */
void bh_action_cells(struct ncplane *n, const char *left, const char *right, int focus);

/* Map an internal screen name ("HOME","MP3"...) to a design tag ("MENU","PLAY"). */
const char *bh_screen_tag(const char *screen_name);

#endif
