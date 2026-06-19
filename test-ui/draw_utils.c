#include "draw_utils.h"
#include "config.h"
#include "bh_skin.h"
#include "services/theme_service.h"

#include <string.h>
/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 2: PRIMITIVE DRAWING HELPERS
 *
 *  These are the building blocks every screen reuses.  Understand these
 *  five functions and you can draw anything in any screen without help.
 *
 *  C CONCEPT: static functions
 *  ────────────────────────────
 *  'static' before a function = "private to this .c file."
 *  No other file can call it.  This prevents name collisions between files
 *  and signals that this is internal plumbing, not a public API.
 *
 *  C CONCEPT: function parameters and pointers
 *  ─────────────────────────────────────────────
 *  Parameters are copies of the values passed in (pass-by-value) EXCEPT
 *  for pointers.  A pointer holds a MEMORY ADDRESS, not the data itself.
 *
 *  Analogy: a pointer is like a street address.  The address (4 bytes)
 *  is cheap to copy.  What lives at that address (the house = the plane
 *  struct) is NOT copied — both caller and function access the same data.
 *
 *  'struct ncplane *n'  — n is a pointer to an ncplane.  Passing n gives
 *  the function direct access to the plane so it can draw on it.
 *
 *  'const char *text'   — pointer to a read-only string.  'const' prevents
 *  accidental modification.  String literals ("HOME") live in read-only
 *  memory and must always be accessed through const char *.
 * ══════════════════════════════════════════════════════════════════════════ */

/* ──────────────────────────────────────────────────────────────────────────
 *  ghost_set()  —  Set foreground colour, lock background to COL_BG
 *
 *  The single most-called function in the codebase.  Every draw operation
 *  calls this (or ghost_text which calls it internally) before placing text.
 *
 *  NOTCURSES: ncplane_set_fg_rgb(plane, colour)
 *  ─────────────────────────────────────────────
 *  Sets the foreground (glyph/text) colour for ALL subsequent draws on
 *  'plane' until changed again.  Colour is 0xRRGGBB — three 8-bit channels
 *  packed into the lower 24 bits of a uint32_t:
 *    bits 23-16 = Red   (0x00–0xFF)
 *    bits 15-8  = Green (0x00–0xFF)
 *    bits 7-0   = Blue  (0x00–0xFF)
 *
 *  NOTCURSES: ncplane_set_bg_rgb(plane, colour)
 *  ─────────────────────────────────────────────
 *  Same as above for background — the rectangle of colour behind each glyph.
 *
 *  IMPORTANT: Colours are STICKY — they remain set until explicitly changed.
 *  Never assume the colour is what you set earlier; other drawing calls
 *  may have changed it.  Always set colour immediately before drawing.
 * ────────────────────────────────────────────────────────────────────────── */
void ghost_set(struct ncplane *n, uint32_t fg) {
    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, theme_bg());
}

/* ──────────────────────────────────────────────────────────────────────────
 *  ghost_text()  —  Set colour and draw a string at (row, col)
 *
 *  Use this for every piece of text your screens draw.
 *
 *  NOTCURSES: ncplane_putstr_yx(plane, row, col, string)
 *  ──────────────────────────────────────────────────────
 *  Draws 'string' starting at cell (row, col) using the currently-set
 *  colours.  The cursor advances right after each glyph.
 *
 *  "_yx" ordering: Y (row) comes first, X (col) second.  Notcurses
 *  consistently uses (row, col) ordering throughout its API, never (x, y).
 *  Row 0 = top of plane.  Col 0 = left edge of plane.
 *
 *  UNICODE NOTE:
 *  C strings are byte arrays.  ▰ is 3 UTF-8 bytes, ● is 3 bytes, etc.
 *  Notcurses handles UTF-8 parsing internally — you just pass the string.
 *  Each Unicode character typically occupies 1 terminal column (some CJK
 *  characters occupy 2).  Column arithmetic uses terminal columns, not bytes.
 *
 *  USAGE EXAMPLES:
 *    ghost_text(p, 4, 2, COL_GHOST_ON,  "BATTERY");
 *    ghost_text(p, 4, 12, COL_GHOST_PCT, "▰▰▰▱  75%");
 *    ghost_text(p, 6, 2, COL_GHOST_LOW, "LOW SIGNAL");
 * ────────────────────────────────────────────────────────────────────────── */
 void ghost_text(struct ncplane *n, int row, int col,
                       uint32_t colour, const char *text) {
    ghost_set(n, colour);
    ncplane_putstr_yx(n, row, col, text);
}

/* ──────────────────────────────────────────────────────────────────────────
 *  ghost_hline()  —  Draw a horizontal run of one repeated glyph
 *
 *  Use for content separators, progress indicators, or decorative rules
 *  within a screen.  Not for the status separator (draw_frame handles that).
 *
 *  C CONCEPT: for loops
 *  ─────────────────────
 *  for (initialiser; condition; step) { body }
 *
 *  Execution order:
 *    1.  initialiser runs once before the loop starts
 *    2.  condition is checked before each iteration — exit when false
 *    3.  body executes
 *    4.  step executes
 *    5.  back to 2
 *
 *  'i++' is shorthand for i = i + 1.
 *  'i < length' means the loop runs for i = 0, 1, 2, … length-1.
 *  Total iterations = length.
 *
 *  USAGE:
 *    ghost_hline(p, 10, 2, 32, "─", COL_SEPARATOR);
 *    // draws 32 thin-line glyphs starting at row 10, col 2
 * ────────────────────────────────────────────────────────────────────────── */
void ghost_hline(struct ncplane *n, int row, int col,
                        int length, const char *glyph, uint32_t colour) {
    ghost_set(n, colour);
    for (int i = 0; i < length; i++) {
        ncplane_putstr_yx(n, row, col + i, glyph);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 *  ghost_fill_rect()  —  Fill a rectangle with a single character
 *
 *  Use to:
 *    • Clear a sub-region before redrawing it (fill with ' ', COL_BG, COL_BG)
 *    • Draw a highlight bar for a selected menu item
 *    • Paint a coloured background block
 *
 *  C CONCEPT: ncplane_putchar_yx vs ncplane_putstr_yx
 *  ───────────────────────────────────────────────────
 *  putchar_yx draws a single ASCII character (one byte, value 0-127).
 *  putstr_yx  draws a null-terminated string, handling multi-byte UTF-8.
 *  For spaces and simple ASCII: use putchar_yx — it's marginally faster.
 *  For any Unicode glyph (▰, ●, ┃, etc.): always use putstr_yx.
 *
 *  USAGE:
 *    // Clear a 4-row × 28-col region of content
 *    ghost_fill_rect(p, 5, 2, 4, 28, ' ', COL_BG, COL_BG);
 *
 *    // Highlight bar for selected menu item (dark bg, bright text)
 *    ghost_fill_rect(p, 7, 1, 1, PHONE_COLS-2, ' ', COL_BG, 0x1C1C1C);
 * ────────────────────────────────────────────────────────────────────────── */
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

/* ──────────────────────────────────────────────────────────────────────────
 *  ghost_label_value()  —  Draw a dim label + brighter value on one row
 *
 *  This is the most common layout pattern across all screens:
 *
 *    col 2           col 14
 *    │               │
 *    SIGNAL          ●●●○
 *    BATTERY         ▰▰▰▱  75%
 *    CARRIER         BH-NET
 *    NETWORK         LTE
 *
 *  Align all value_col arguments to the same column for a clean two-column
 *  layout.  Define VALUE_COL = 14 (or whatever fits) in config.h and use
 *  it everywhere for consistency.
 *
 *  USAGE:
 *    ghost_label_value(p, 5, 2, VALUE_COL, "SIGNAL",  "●●●○");
 *    ghost_label_value(p, 6, 2, VALUE_COL, "BATTERY", "▰▰▰▱  75%");
 *    ghost_label_value(p, 7, 2, VALUE_COL, "CARRIER", carrier_name);
 * ────────────────────────────────────────────────────────────────────────── */
void ghost_label_value(struct ncplane *n,
                                 int row, int label_col, int value_col,
                                 const char *label, const char *value) {
    ghost_text(n, row, label_col, theme_text_muted(), label);   /* dim */
    ghost_text(n, row, value_col, theme_text_primary(), value); /* bright */
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
