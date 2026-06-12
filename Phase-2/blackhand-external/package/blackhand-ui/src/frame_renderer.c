#include "frame_renderer.h"
#include "draw_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "platform/hardware.h"
#include "services/theme_service.h"
#include "services/bluetooth_service.h"
#include "ui-ipcs/modem_ipc.h"


/* ──────────────────────────────────────────────────────────────────────────
 *  draw_battery()
 *
 *  Output examples:
 *    Normal    75%  →  ▰▰▰▱  75%
 *    Charging  60%  →  ▰▰▰▱  ⚡60%
 *    Low       12%  →  ▰▱▱▱  12%   (whole widget blinks)
 *    Empty      0%  →  ▱▱▱▱   0%
 *
 *  Glyphs:
 *    ▰  U+25B0  BLACK LOWER RIGHT TRIANGLE  — filled segment
 *    ▱  U+25B1  WHITE LOWER RIGHT TRIANGLE  — hollow segment
 *    ⚡  U+26A1  HIGH VOLTAGE SIGN           — charging
 *
 *  C CONCEPT: the modulo operator  %
 *  ───────────────────────────────────
 *  a % b  gives the REMAINDER after dividing a by b.
 *    10 % 3  = 1   (10 = 3×3 + 1)
 *     7 % 4  = 3   ( 7 = 4×1 + 3)
 *
 *  tick % 10  cycles 0,1,2,3,4,5,6,7,8,9,0,1,2,…
 *  (tick % 10) < 5  is true for exactly half of all ticks → 50% blink duty.
 *
 *  C CONCEPT: the ternary operator  ? :
 *  ──────────────────────────────────────
 *  condition ? value_if_true : value_if_false
 *  Produces a value — a compact if/else that can be used inside expressions.
 *    int max = (a > b) ? a : b;
 *  We use it to pick "▰" or "▱" per segment, and to pick a colour.
 *
 *  C CONCEPT: snprintf — safe string formatting
 *  ──────────────────────────────────────────────
 *  snprintf(buffer, max_bytes, format_string, values…)
 *  Like printf() but writes into a char array instead of the terminal.
 *  The 'n' limits output to max_bytes bytes (including '\0') — safe.
 *
 *  Format specifiers:
 *    %d    decimal integer
 *    %s    string (char *)
 *    %f    float  (use %.2f for 2 decimal places)
 *    %%    literal percent sign
 *
 *  Buffer sizing:
 *    "⚡100%\0" = 3 (⚡ = 3 UTF-8 bytes) + 4 ("100%") + 1 ('\0') = 8 bytes.
 *    char label[16] gives comfortable headroom.
 *
 *  C CONCEPT: char arrays (C strings)
 *  ────────────────────────────────────
 *  C has no built-in string type.  A string is a char array terminated
 *  by '\0' (the null byte).  "Hello" is stored as:
 *    ['H','e','l','l','o','\0']   — 6 bytes for 5 visible characters.
 *  Always allocate at least strlen + 1 bytes to accommodate '\0'.
 * ────────────────────────────────────────────────────────────────────────── */
void draw_battery(struct ncplane *phone, int percent,
                         bool charging, int tick) {

    /* Map 0-100% → 0-4 filled segments using ceiling-like division */
    int segs = (percent + 24) / 25;
    if (segs > 4) segs = 4;
    if (segs < 0) segs = 0;

    /*
     * LOW BATTERY BLINK
     * tick % 10 cycles 0–9.  Ticks 0-4: visible.  Ticks 5-9: hidden.
     * We overwrite with spaces rather than ncplane_erase() because erase()
     * clears the ENTIRE plane (wiping clock and signal too).
     * 15 spaces covers the longest possible battery string comfortably.
     */
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

/* ──────────────────────────────────────────────────────────────────────────
 *  draw_signal()
 *
 *  Output:
 *    4 bars    →  ●●●●   (all filled, right-anchored)
 *    3 bars    →  ●●●○
 *    No signal →  ✕○○○   (✕ pulses between two dark greys)
 *
 *  Glyphs:
 *    ●  U+25CF  BLACK CIRCLE      — active bar
 *    ○  U+25CB  WHITE CIRCLE      — inactive bar
 *    ✕  U+2715  MULTIPLICATION X  — no-signal marker
 *
 *  RIGHT-ANCHORED POSITIONING
 *  ───────────────────────────
 *  Position is computed dynamically from the plane width at draw-time:
 *    sig_col = cols - 6
 *  This keeps signal flush against the right border regardless of PHONE_COLS
 *  or terminal resize.  NCKEY_RESIZE causes a redraw; the next call to
 *  draw_signal() picks up the new cols automatically.
 *
 *  Column layout (right edge):
 *    cols-7  →  ✕ prefix  (only when disconnected)
 *    cols-6  →  circle 0
 *    cols-5  →  circle 1
 *    cols-4  →  circle 2
 *    cols-3  →  circle 3
 *    cols-2  →  gap before border
 *    cols-1  →  right border ┃
 *
 *  C CONCEPT: casting  (int)unsigned_value
 *  ─────────────────────────────────────────
 *  cols is unsigned (plane width is never negative).  We subtract from it
 *  to find sig_col, which could theoretically go negative on a tiny terminal.
 *  Casting to int first makes the arithmetic signed so negative results are
 *  handled correctly (and we then guard with 'if (sig_col < 1) return').
 * ────────────────────────────────────────────────────────────────────────── */
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

/* ──────────────────────────────────────────────────────────────────────────
 *  draw_status_bar()  —  Read hardware state and draw both indicators
 *
 *  C CONCEPT: returning structs by value
 *  ──────────────────────────────────────
 *  hardware_get_battery() returns a battery_status_t struct.
 *  The ENTIRE struct is copied into the local variable 'batt'.
 *  For small structs (a few int/bool fields) this is fine — copying a
 *  handful of bytes is fast.  For large structs, pass a pointer instead.
 *
 *  Struct field access: batt.percent, batt.charging
 *  The '.' operator reads a named field from a struct variable.
 *  If you had a pointer to a struct: ptr->percent  (arrow operator).
 * ────────────────────────────────────────────────────────────────────────── */
void draw_status_bar(struct ncplane *phone, int tick) {
    battery_status_t  batt = hardware_get_battery();
    cellular_status_t cell = hardware_get_cellular();

    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    (void)rows; (void)tick;

    int bars = cell.signal_bars;
    if (bars < 0) bars = 0;
    if (bars > 4) bars = 4;

    time_t now = time(NULL);
    struct tm tm_now;
    char clk[12] = "--:--";
    char clk_s[12] = "--:--:--";
    if (localtime_r(&now, &tm_now)) {
        snprintf(clk,   sizeof(clk),   "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        snprintf(clk_s, sizeof(clk_s), "%02d:%02d:%02d",
                 tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    }

    int ccol = ((int)cols - 5) / 2;   /* centered clock column */

    /* One fixed skeleton, eight status formats — all driven by the theme
     * token, never by theme id (design pack §6). */
    switch (theme_status_style()) {

    case STATUS_TEXT: {        /* SIM:OK · HH:MM · PWR:82 */
        char sim[12], pwr[12];
        snprintf(sim, sizeof(sim), "SIM:%s", cell.connected ? "OK" : "--");
        snprintf(pwr, sizeof(pwr), "PWR:%d", batt.percent);
        ghost_text(phone, 0, 1, theme_text_muted(), sim);
        ghost_text(phone, 0, ccol, theme_text_primary(), clk);
        ghost_text(phone, 0, (int)cols - 1 - (int)strlen(pwr),
                   theme_text_muted(), pwr);
        break;
    }

    case STATUS_TEXT2: {       /* SIG:3/4 · HH:MM · PWR:78  (dossier) */
        char sig[12], pwr[12];
        snprintf(sig, sizeof(sig), "SIG:%d/4", bars);
        snprintf(pwr, sizeof(pwr), "PWR:%d", batt.percent);
        ghost_text(phone, 0, 1, theme_border(), sig);
        ghost_text(phone, 0, ccol, theme_text_primary(), clk);
        ghost_text(phone, 0, (int)cols - 1 - (int)strlen(pwr),
                   theme_border(), pwr);
        break;
    }

    case STATUS_RITE: {        /* boot-rite: telemetry in instrument teal */
        char sim[12], pwr[12];
        snprintf(sim, sizeof(sim), "SIM:%s", cell.connected ? "OK" : "--");
        snprintf(pwr, sizeof(pwr), "PWR:%d", batt.percent);
        ghost_text(phone, 0, 1, theme_accent(), sim);
        ghost_text(phone, 0, ccol, theme_accent(), clk);
        ghost_text(phone, 0, (int)cols - 1 - (int)strlen(pwr),
                   theme_accent(), pwr);
        break;
    }

    case STATUS_BARS: {        /* ▮▮▯▯ · HH:MM · 78%  (redline) */
        ncplane_set_bg_rgb(phone, theme_bg());
        for (int i = 0; i < 4; i++) {
            ncplane_set_fg_rgb(phone, theme_text_muted());
            ncplane_putstr_yx(phone, 0, 1 + i, (i < bars) ? "▮" : "▯");
        }
        ghost_text(phone, 0, ccol, theme_text_primary(), clk);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", batt.percent);
        ghost_text(phone, 0, (int)cols - 1 - (int)strlen(pct),
                   theme_text_muted(), pct);
        break;
    }

    case STATUS_BITS: {        /* ███░ · HH:MM · ██░░  (one-bit) */
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_set_fg_rgb(phone, theme_text_primary());
        for (int i = 0; i < 4; i++)
            ncplane_putstr_yx(phone, 0, 1 + i, (i < bars) ? "█" : "░");
        ncplane_putstr_yx(phone, 0, ccol, clk);
        int bsegs = (batt.percent + 24) / 25;
        if (bsegs > 4) bsegs = 4;
        for (int i = 0; i < 4; i++)
            ncplane_putstr_yx(phone, 0, (int)cols - 5 + i,
                              (i < bsegs) ? "█" : "░");
        break;
    }

    case STATUS_THERMAL: {     /* cold-blue signal · clock · 4 heat blocks */
        ncplane_set_bg_rgb(phone, theme_bg());
        static const char *steps[] = { "▂", "▄", "▆" };
        for (int i = 0; i < 3; i++) {
            int lit = (bars > i + 1) || (bars > 0 && i == 0);
            ncplane_set_fg_rgb(phone, lit ? 0x85B7EB : theme_fg_deep());
            ncplane_putstr_yx(phone, 0, 1 + i, steps[i]);
        }
        ghost_text(phone, 0, ccol, theme_text_primary(), clk);
        /* battery drains from the green end: hot→cool left to right */
        static const uint32_t heat[4] = { 0xE24B4A, 0xEF9F27, 0x97C459, 0x97C459 };
        int bsegs = (batt.percent + 24) / 25;
        if (bsegs > 4) bsegs = 4;
        for (int i = 0; i < 4; i++) {
            ncplane_set_fg_rgb(phone, (i < bsegs) ? heat[i] : theme_fg_deep());
            ncplane_putstr_yx(phone, 0, (int)cols - 5 + i, "█");
        }
        break;
    }

    case STATUS_INSTRUMENT: {  /* two rows: UTC clock ┼ / live telemetry */
        char utc[24];
        snprintf(utc, sizeof(utc), "UTC %s", clk_s);
        ghost_text(phone, 0, 1, theme_text_muted(), utc);
        ghost_text(phone, 0, (int)cols - 2, theme_border(), "┼");
        char tele[48];
        snprintf(tele, sizeof(tele), "SIG %d/4 · PWR %d%% · %s",
                 bars, batt.percent,
                 modem_ipc_is_online() ? "NET:UP" : "NET:--");
        ghost_text(phone, 1, 1, theme_border(), tele);
        break;
    }

    case STATUS_GLYPH:         /* ▂▄▆ · HH:MM · [▓▓░] */
    default: {
        static const char *steps[] = { "▂", "▄", "▆" };
        ncplane_set_bg_rgb(phone, theme_bg());
        for (int i = 0; i < 3; i++) {
            int lit = (bars > i + 1) || (bars > 0 && i == 0);
            ncplane_set_fg_rgb(phone, lit ? theme_text_muted() : theme_fg_deep());
            ncplane_putstr_yx(phone, 0, 1 + i, steps[i]);
        }
        ghost_text(phone, 0, ccol, theme_text_primary(), clk);
        int thirds = (batt.percent + 16) / 33;
        if (thirds > 3) thirds = 3;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_putstr_yx(phone, 0, (int)cols - 6, "[");
        for (int i = 0; i < 3; i++) {
            ncplane_set_fg_rgb(phone, (i < thirds) ? theme_text_muted()
                                                   : theme_fg_deep());
            ncplane_putstr_yx(phone, 0, (int)cols - 5 + i,
                              (i < thirds) ? "▓" : "░");
        }
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_putstr_yx(phone, 0, (int)cols - 2, "]");
        break;
    }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  FRAME — status strip (row 0/0-1) + divider with screen tag (row 2).
 *  Footer is drawn LAST (see draw_footer) so the list tracker is complete.
 * ══════════════════════════════════════════════════════════════════════════ */

static char s_tag[16] = "";      /* current screen tag, for footer + stamp  */
static int  s_trk_sel = -1;      /* selected row index seen this frame      */
static int  s_trk_max = -1;      /* highest row index seen this frame       */

void frame_track_reset(void)            { s_trk_sel = -1; s_trk_max = -1; }
void frame_track_row(int index, int selected)
{
    if (index > s_trk_max) s_trk_max = index;
    if (selected) s_trk_sel = index;
}

void draw_frame(struct ncplane *phone, int tick,
                       const char *screen_name) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    ncplane_erase(phone);
    if (rows < (unsigned)FRAME_MIN_ROWS || cols < (unsigned)FRAME_MIN_COLS)
        return;

    snprintf(s_tag, sizeof(s_tag), "%s", screen_name ? screen_name : "");
    frame_track_reset();

    ghost_fill_rect(phone, 0, 0, (int)rows, (int)cols, ' ', theme_bg(), theme_bg());
    draw_status_bar(phone, tick);

    /* ── divider (row 2) — eight styles, all token-driven ──────────────── */
    ncplane_set_bg_rgb(phone, theme_bg());
    const int W = (int)cols;
    switch (theme_div_style()) {

    case DIV_ORNAMENT:
        ncplane_set_fg_rgb(phone, theme_border());
        for (int x = 0; x < W; x++)
            ncplane_putstr_yx(phone, 2, x, (x % 2 == 0) ? "◆" : "╳");
        break;

    case DIV_DOTTED:
        ncplane_set_fg_rgb(phone, theme_fg_deep());
        for (int x = 0; x < W; x++) ncplane_putstr_yx(phone, 2, x, "┄");
        break;

    case DIV_DOUBLE:
        ncplane_set_fg_rgb(phone, theme_text_primary());
        for (int x = 0; x < W; x++) ncplane_putstr_yx(phone, 2, x, "═");
        break;

    case DIV_CHECKER:
        ncplane_set_fg_rgb(phone, theme_text_primary());
        for (int x = 0; x < W; x++)
            ncplane_putstr_yx(phone, 2, x, (x % 2 == 0) ? "▀" : "▄");
        break;

    case DIV_AURORA: {
        static const char *aur[] = { "▁","▂","▄","▆","▄","▂","▁" };
        ncplane_set_fg_rgb(phone, theme_accent());
        for (int x = 0; x < W; x++)
            ncplane_putstr_yx(phone, 2, x, aur[x % 7]);
        break;
    }

    case DIV_LABELED: {        /* ── TAG ── accent label, redline */
        ncplane_set_fg_rgb(phone, theme_fg_deep());
        for (int x = 0; x < W; x++) ncplane_putstr_yx(phone, 2, x, "─");
        if (s_tag[0]) {
            char lbl[20];
            snprintf(lbl, sizeof(lbl), " %.12s ", s_tag);
            int col = (W - (int)strlen(lbl)) / 2;
            if (col > 0) {
                ncplane_set_fg_rgb(phone, theme_accent());
                ncplane_putstr_yx(phone, 2, col, lbl);
            }
        }
        break;
    }

    case DIV_DIMENSION: {      /* |—— TAG ——| drawing dimension line */
        ncplane_set_fg_rgb(phone, theme_border());
        for (int x = 1; x < W - 1; x++) ncplane_putstr_yx(phone, 2, x, "─");
        ncplane_putstr_yx(phone, 2, 0,     "|");
        ncplane_putstr_yx(phone, 2, W - 1, "|");
        if (s_tag[0]) {
            char lbl[20];
            snprintf(lbl, sizeof(lbl), " %.12s ", s_tag);
            int col = (W - (int)strlen(lbl)) / 2;
            if (col > 1) {
                ncplane_set_fg_rgb(phone, theme_text_muted());
                ncplane_putstr_yx(phone, 2, col, lbl);
            }
        }
        break;
    }

    case DIV_SOLID:
    default: {
        ncplane_set_fg_rgb(phone, theme_border());
        for (int x = 0; x < W; x++) ncplane_putstr_yx(phone, 2, x, "─");
        if (s_tag[0]) {
            int nlen = (int)strlen(s_tag);
            if (nlen + 8 < W) {
                ncplane_set_fg_rgb(phone, theme_border());
                ncplane_putstr_yx(phone, 2, 2, "┫");
                ncplane_set_fg_rgb(phone, theme_text_primary());
                char pad[24];
                snprintf(pad, sizeof(pad), " %.12s ", s_tag);
                ncplane_putstr_yx(phone, 2, 3, pad);
                ncplane_set_fg_rgb(phone, theme_border());
                ncplane_putstr_yx(phone, 2, 3 + nlen + 2, "┣");
            }
        }
        break;
    }
    }

    /* dossier stamp — MENU only, the single non-grid element in the OS */
    if (theme_active()->stamp && strcmp(s_tag, "MENU") == 0) {
        int r = (int)rows - 6, c = (int)cols - 10;
        if (r > 4 && c > 2) {
            ncplane_set_bg_rgb(phone, theme_bg());
            ncplane_set_fg_rgb(phone, 0xA32D2D);
            ncplane_putstr_yx(phone, r,     c, "┌──────┐");
            ncplane_putstr_yx(phone, r + 1, c, "│BH·OS │");
            ncplane_putstr_yx(phone, r + 2, c, "└──────┘");
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  FOOTER — eleven styles. Replaced by action cells on decision screens.
 * ══════════════════════════════════════════════════════════════════════════ */

void draw_footer(struct ncplane *phone)
{
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    if (rows < 6 || cols < 16) return;

    int rule = (int)rows - FOOTER_ROW_OFFSET - 1;
    int row  = (int)rows - FOOTER_ROW_OFFSET;
    int W    = (int)cols;

    /* rule above the footer text */
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_set_fg_rgb(phone, theme_fg_deep());
    for (int x = CONTENT_COL; x < W - CONTENT_COL; x++)
        ncplane_putstr_yx(phone, rule, x, "─");

    switch (theme_footer_style()) {

    case FOOTER_READOUT: {     /* SEL 0n/0N · RDY — live cursor readout */
        char sel[20];
        if (s_trk_sel >= 0 && s_trk_max >= 0)
            snprintf(sel, sizeof(sel), "SEL %02d/%02d", s_trk_sel + 1, s_trk_max + 1);
        else
            snprintf(sel, sizeof(sel), "SEL --/--");
        ghost_text(phone, row, CONTENT_COL, theme_border(), sel);
        ghost_text(phone, row, W - CONTENT_COL - 3, theme_border(), "RDY");
        break;
    }

    case FOOTER_CORNERS:
        ghost_text(phone, row, CONTENT_COL, theme_border(), "◤");
        ghost_text(phone, row, (W - 5) / 2, theme_accent(), "BH-OS");
        ghost_text(phone, row, W - CONTENT_COL - 1, theme_border(), "◥");
        break;

    case FOOTER_STAR: {
        const char *t = "✶ BLACKHAND ✶";
        ghost_text(phone, row, (W - 13) / 2, theme_fg_deep(), t);
        break;
    }

    case FOOTER_HEAT:
        ghost_text(phone, row, CONTENT_COL, theme_border(), "HEAT=USE");
        ghost_text(phone, row, W - CONTENT_COL - 5, theme_text_muted(), "σ 24H");
        break;

    case FOOTER_SPARK: {       /* signal-history sparkline, shifts per poll */
        static char hist[10] = {1,2,4,2,1,2,6,4,2,1};
        static time_t last = 0;
        time_t now = time(NULL);
        if (now != last) {
            last = now;
            cellular_status_t c = hardware_get_cellular();
            memmove(hist, hist + 1, 9);
            hist[9] = (char)(c.signal_bars * 2);
        }
        static const char *lv[] = { "▁","▁","▂","▃","▄","▅","▆","▇","█" };
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_set_fg_rgb(phone, theme_border());
        for (int i = 0; i < 10; i++) {
            int v = hist[i]; if (v < 0) v = 0; if (v > 8) v = 8;
            ncplane_putstr_yx(phone, row, CONTENT_COL + i, lv[v]);
        }
        const char *net = modem_ipc_is_online() ? "NET:UP" : "NET:--";
        ghost_text(phone, row, W - CONTENT_COL - 6, theme_text_muted(), net);
        break;
    }

    case FOOTER_RITE:
        ghost_text(phone, row, CONTENT_COL, theme_accent(), "INIT OK");
        ghost_text(phone, row, W - CONTENT_COL - 1, theme_border(), "✶");
        break;

    case FOOTER_TITLEBLOCK:
        ghost_text(phone, row, CONTENT_COL, theme_text_muted(), "BLACKHAND");
        ghost_text(phone, row, W - CONTENT_COL - 7, theme_border(), "SHT 1/1");
        break;

    case FOOTER_REF:
        ghost_text(phone, row, CONTENT_COL, theme_border(), "REF: BLACKHAND/0.4");
        break;

    case FOOTER_BIT: {
        ncplane_set_fg_rgb(phone, theme_selection_text());
        ncplane_set_bg_rgb(phone, theme_selection_bg());
        char tag[16];
        snprintf(tag, sizeof(tag), " %s ", s_tag[0] ? s_tag : "BH");
        ncplane_putstr_yx(phone, row, CONTENT_COL, tag);
        ghost_text(phone, row, W - CONTENT_COL - 9, theme_text_primary(),
                   "BLACKHAND");
        break;
    }

    case FOOTER_COORDS:        /* shares the hands-white data source */
        ghost_text(phone, row, CONTENT_COL, theme_text_muted(), "LAT ──");
        ghost_text(phone, row, W - CONTENT_COL - 6, theme_accent(), "LON ──");
        break;

    case FOOTER_WORDMARK:
    default:
        ghost_text(phone, row, CONTENT_COL, theme_border(), "BLACKHAND");
        ghost_text(phone, row, W - CONTENT_COL - 4, theme_border(), "v0.4");
        break;
    }
}
