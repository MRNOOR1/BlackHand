#include "draw_utils.h"
#include "config.h"
#include "services/theme_service.h"
#include "frame_renderer.h"

#include <string.h>
#include <stdio.h>
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

/* ── themed list row — the ONE place selection styles are implemented ────── */
void ghost_list_row_meta(struct ncplane *n, int row, int cols,
                         int index, int selected,
                         const char *label, const char *meta)
{
    if (!label) label = "";
    if (!meta)  meta  = "";
    int width = cols - 2 * CONTENT_COL;
    if (width < 6) return;

    frame_track_row(index, selected);

    int meta_col = cols - CONTENT_COL - (int)strlen(meta);

    switch (theme_sel_style()) {

    case SEL_INDEX: {          /* 0n LABEL … ■ / meta */
        char idx[8];
        snprintf(idx, sizeof(idx), "%02d ", index + 1);
        ghost_text(n, row, CONTENT_COL, theme_border(), idx);
        ghost_text(n, row, CONTENT_COL + 3,
                   selected ? theme_text_primary() : theme_text_muted(), label);
        if (meta[0])
            ghost_text(n, row, meta_col, theme_border(), meta);
        else if (selected)
            ghost_text(n, row, cols - CONTENT_COL - 1, theme_text_primary(), "■");
        break;
    }

    case SEL_BRACKET: {        /* centered [ LABEL ] in bright accent */
        char line[40];
        if (selected) snprintf(line, sizeof(line), "[ %.12s ]", label);
        else          snprintf(line, sizeof(line), "%.12s", label);
        int col = (cols - (int)strlen(line)) / 2;
        if (col < CONTENT_COL) col = CONTENT_COL;
        ghost_text(n, row, col,
                   selected ? theme_accent2() : theme_text_muted(), line);
        if (meta[0])
            ghost_text(n, row, meta_col, theme_border(), meta);
        break;
    }

    case SEL_DIAMOND: {        /* ◆ filled / ◇ hollow — filled IS the state */
        ncplane_set_bg_rgb(n, theme_bg());
        ncplane_set_fg_rgb(n, selected ? theme_text_primary() : theme_border());
        ncplane_putstr_yx(n, row, CONTENT_COL, selected ? "◆" : "◇");
        char body[32];
        snprintf(body, sizeof(body), " %.14s", label);
        ncplane_putstr_yx(n, row, CONTENT_COL + 1, body);
        if (meta[0]) ghost_text(n, row, meta_col, theme_fg_deep(), meta);
        break;
    }

    case SEL_HEAT: {           /* heat block marker; bucket binding pending */
        if (selected) {
            ncplane_set_bg_rgb(n, theme_bg());
            ncplane_set_fg_rgb(n, theme_accent());
            ncplane_putstr_yx(n, row, CONTENT_COL, "█");
            ghost_text(n, row, CONTENT_COL + 2, theme_accent2(), label);
        } else {
            ghost_text(n, row, CONTENT_COL + 2, theme_text_muted(), label);
        }
        if (meta[0]) ghost_text(n, row, meta_col, theme_border(), meta);
        break;
    }

    case SEL_REG: {            /* instrument: invert + 0x0n register meta */
        char reg[12];
        if (!meta[0]) {
            snprintf(reg, sizeof(reg), "0x%02d", index + 1);
            meta = reg;
            meta_col = cols - CONTENT_COL - (int)strlen(meta);
        }
        uint32_t fg = selected ? theme_selection_text() : theme_text_muted();
        uint32_t bg = selected ? theme_selection_bg()   : theme_bg();
        ncplane_set_fg_rgb(n, fg);
        ncplane_set_bg_rgb(n, bg);
        for (int x = 0; x < width && CONTENT_COL + x < cols - 1; x++)
            ncplane_putchar_yx(n, row, CONTENT_COL + x, ' ');
        ncplane_putstr_yx(n, row, CONTENT_COL + 1, label);
        ncplane_set_fg_rgb(n, selected ? theme_selection_text() : theme_border());
        ncplane_putstr_yx(n, row, meta_col, meta);
        break;
    }

    case SEL_BOX: {            /* blueprint: outlined row, drawing ref meta */
        char ref[8];
        if (!meta[0]) {
            snprintf(ref, sizeof(ref), "A·%d", index + 1);
            meta = ref;
            meta_col = cols - CONTENT_COL - 3;
        }
        if (selected) {
            ncplane_set_bg_rgb(n, theme_bg());
            ncplane_set_fg_rgb(n, theme_text_primary());
            ncplane_putstr_yx(n, row, CONTENT_COL - 1, "▕");
            ncplane_putstr_yx(n, row, CONTENT_COL + 1, label);
            ncplane_putstr_yx(n, row, cols - CONTENT_COL, "▏");
            ghost_text(n, row, meta_col, theme_text_muted(), meta);
        } else {
            ghost_text(n, row, CONTENT_COL + 1, theme_text_muted(), label);
            ghost_text(n, row, meta_col, theme_border(), meta);
        }
        break;
    }

    case SEL_INK: {            /* dossier: solid ink bar / ··· leaders */
        if (selected) {
            ncplane_set_fg_rgb(n, theme_selection_text());
            ncplane_set_bg_rgb(n, theme_selection_bg());
            for (int x = 0; x < width && CONTENT_COL + x < cols - 1; x++)
                ncplane_putchar_yx(n, row, CONTENT_COL + x, ' ');
            ncplane_putstr_yx(n, row, CONTENT_COL + 1, label);
            if (meta[0]) ncplane_putstr_yx(n, row, meta_col - 1, meta);
        } else {
            ghost_text(n, row, CONTENT_COL + 1, theme_text_muted(), label);
            char lead[40];
            snprintf(lead, sizeof(lead), "··· %s", meta);
            ghost_text(n, row, cols - CONTENT_COL - (int)strlen(lead),
                       theme_fg_deep(), lead);
        }
        break;
    }

    case SEL_BIT: {            /* one-bit: full invert, no dim level exists */
        if (selected) {
            ncplane_set_fg_rgb(n, theme_selection_text());
            ncplane_set_bg_rgb(n, theme_selection_bg());
            for (int x = 0; x < width && CONTENT_COL + x < cols - 1; x++)
                ncplane_putchar_yx(n, row, CONTENT_COL + x, ' ');
            ncplane_putstr_yx(n, row, CONTENT_COL + 1, label);
            if (meta[0]) ncplane_putstr_yx(n, row, meta_col - 1, meta);
        } else {
            ghost_text(n, row, CONTENT_COL + 1, theme_text_primary(), label);
            if (meta[0]) ghost_text(n, row, meta_col, theme_text_primary(), meta);
        }
        break;
    }

    case SEL_STAR: {           /* polar: ✶ prefix, white selected text */
        ncplane_set_bg_rgb(n, theme_bg());
        if (selected) {
            ncplane_set_fg_rgb(n, theme_accent());
            ncplane_putstr_yx(n, row, CONTENT_COL, "✶");
            ghost_text(n, row, CONTENT_COL + 2, theme_accent2(), label);
        } else {
            ghost_text(n, row, CONTENT_COL + 2, theme_text_muted(), label);
        }
        if (meta[0]) ghost_text(n, row, meta_col, theme_border(), meta);
        break;
    }

    case SEL_INVERT:           /* inverted bar + "> " cursor */
    default: {
        uint32_t fg = selected ? theme_selection_text() : theme_text_muted();
        uint32_t bg = selected ? theme_selection_bg()   : theme_bg();
        ncplane_set_fg_rgb(n, fg);
        ncplane_set_bg_rgb(n, bg);
        for (int x = 0; x < width && CONTENT_COL + x < cols - 1; x++)
            ncplane_putchar_yx(n, row, CONTENT_COL + x, ' ');
        ncplane_putstr_yx(n, row, CONTENT_COL, selected ? "> " : "  ");
        ncplane_putstr_yx(n, row, CONTENT_COL + 2, label);
        if (meta[0]) {
            ncplane_set_fg_rgb(n, selected ? theme_selection_text()
                                           : theme_fg_deep());
            ncplane_putstr_yx(n, row, meta_col, meta);
        }
        break;
    }
    }
}

void ghost_list_row(struct ncplane *n, int row, int cols,
                    int index, int selected, const char *label)
{
    ghost_list_row_meta(n, row, cols, index, selected, label, "");
}

/* ── action cells — global semantic colors, never theme tokens ───────────── */
static void bh_cell(struct ncplane *n, int row, int col, const char *verb,
                    uint32_t border, uint32_t text, int focus)
{
    int len = (int)strlen(verb);
    ncplane_set_bg_rgb(n, theme_bg());
    ncplane_set_fg_rgb(n, border);
    ncplane_putstr_yx(n, row, col, "▐");
    if (focus) { ncplane_set_fg_rgb(n, theme_bg()); ncplane_set_bg_rgb(n, border); }
    else       { ncplane_set_fg_rgb(n, text);       ncplane_set_bg_rgb(n, theme_bg()); }
    char body[16];
    snprintf(body, sizeof(body), " %s ", verb);
    ncplane_putstr_yx(n, row, col + 1, body);
    ncplane_set_bg_rgb(n, theme_bg());
    ncplane_set_fg_rgb(n, border);
    ncplane_putstr_yx(n, row, col + 1 + len + 2, "▌");
}

void ghost_action_cells(struct ncplane *n,
                        const char *affirm, const char *destruct, int focused)
{
    unsigned rows, cols;
    ncplane_dim_yx(n, &rows, &cols);
    if (rows < 4 || cols < 16) return;

    int row = (int)rows - FOOTER_ROW_OFFSET;
    int W   = (int)cols;
    int light = theme_is_light();

    uint32_t ab = light ? BH_AFFIRM_BORDER_LIGHT   : BH_AFFIRM_BORDER;
    uint32_t at = light ? BH_AFFIRM_TEXT_LIGHT     : BH_AFFIRM_TEXT;
    uint32_t db = light ? BH_DESTRUCT_BORDER_LIGHT : BH_DESTRUCT_BORDER;
    uint32_t dt = light ? BH_DESTRUCT_TEXT_LIGHT   : BH_DESTRUCT_TEXT;

    /* clear the footer band (rule row + cell row) */
    ghost_fill_rect(n, row - 1, 0, 2, W, ' ', theme_bg(), theme_bg());

    if (!affirm || !affirm[0]) return;

    if (destruct && destruct[0]) {
        int aw = (int)strlen(affirm)   + 4;
        int dw = (int)strlen(destruct) + 4;
        int acol = (W / 2 - aw) / 2;
        int dcol = W / 2 + (W / 2 - dw) / 2;
        if (acol < 1) acol = 1;
        bh_cell(n, row, acol, affirm,   ab, at, focused == 0);
        bh_cell(n, row, dcol, destruct, db, dt, focused == 1);
    } else {
        int aw = (int)strlen(affirm) + 4;
        bh_cell(n, row, (W - aw) / 2, affirm, ab, at, focused == 0);
    }
}

/* ghost_softkeys — LEGACY SHIM. The design pack replaces contextual softkey
 * labels with the theme footer (key conventions are fixed: Q back, E
 * contextual, see blueprints §2). Every existing call site keeps working;
 * the labels are simply no longer drawn. Decision screens call
 * ghost_action_cells() instead. */
void ghost_softkeys(struct ncplane *n, const char *left_label, const char *right_label) {
    (void)left_label; (void)right_label;
    draw_footer(n);
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

    ghost_fill_rect(n, top, left, h, w, ' ', theme_text_primary(), theme_bg());
    ghost_text(n, top + 1, left + 2, theme_text_primary(), question ? question : "Are you sure?");
    /* Selected option uses ACCENT so the choice is unambiguous in every theme —
     * same visual language as softkeys. */
    ghost_text(n, top + 3, left + 2,
               yes_selected ? theme_text_muted() : theme_accent(),
               yes_selected ? "  NO" : "> NO");
    ghost_text(n, top + 3, left + 12,
               yes_selected ? theme_accent() : theme_text_muted(),
               yes_selected ? "> YES" : "  YES");
    ghost_softkeys(n, "[NO]", "[YES]");
}
