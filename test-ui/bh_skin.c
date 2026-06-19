#include "bh_skin.h"
#include "config.h"
#include "draw_utils.h"
#include "platform/hardware.h"
#include "services/theme_service.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

/* ── small local helpers ──────────────────────────────────────────────────── */

/* Draw text with an explicit fg AND bg (the highlight path: ghost_set forces
 * bg back to the theme background, which would erase a selection bar). */
static void put2(struct ncplane *n, int row, int col,
                 uint32_t fg, uint32_t bg, const char *s) {
    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, bg);
    ncplane_putstr_yx(n, row, col, s);
}

static void clock_hm(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    if (localtime_r(&t, &tm)) snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else                      snprintf(buf, n, "00:00");
}

static void clock_hms(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    if (localtime_r(&t, &tm)) snprintf(buf, n, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    else                      snprintf(buf, n, "00:00:00");
}

/* 3-cell battery bracket "[##.]" using full/low thirds. */
static void battery_bracket(char *buf, size_t n, int pct) {
    int thirds = (pct + 16) / 33;        /* 0..3 */
    if (thirds > 3) thirds = 3;
    if (thirds < 0) thirds = 0;
    char inner[16] = "";
    for (int i = 0; i < 3; i++) strcat(inner, i < thirds ? "▓" : "░"); /* ▓ / ░ */
    snprintf(buf, n, "[%s]", inner);
}

/* signal ramp glyph by bars (0..4). */
static const char *signal_ramp(int bars, int connected) {
    if (!connected) return "▁▁▁";          /* ▁▁▁ */
    switch (bars) {
        case 0: return "▁▁▁";              /* ▁▁▁ */
        case 1: return "▂▁▁";              /* ▂▁▁ */
        case 2: return "▂▄▁";              /* ▂▄▁ */
        case 3: return "▂▄▆";              /* ▂▄▆ */
        default:return "▂▄▆";              /* ▂▄▆ */
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  STATUS STRIP
 * ════════════════════════════════════════════════════════════════════════ */
void bh_status_strip(struct ncplane *n, int tick) {
    (void)tick;
    unsigned urows, ucols;
    ncplane_dim_yx(n, &urows, &ucols);
    int cols = (int)ucols;
    if (cols < 12) return;

    const bh_theme_t *t = theme_active();
    battery_status_t  b = hardware_get_battery();
    cellular_status_t c = hardware_get_cellular();

    char clk[12]; clock_hm(clk, sizeof(clk));
    int center = (cols - (int)strlen(clk)) / 2;
    int left   = 2;
    int row    = STATUS_ROW;

    char tmp[40];

    switch (t->status_style) {
    case ST_TEXT: { /* phosphor: SIM:OK . HH:MM . PWR:NN */
        put2(n, row, left, t->mid, t->bg, "SIM:OK");
        put2(n, row, center, t->bright, t->bg, clk);
        snprintf(tmp, sizeof(tmp), "PWR:%d", b.percent);
        put2(n, row, cols - 2 - (int)strlen(tmp), t->mid, t->bg, tmp);
        break;
    }
    case ST_TEXT2: { /* dossier: SIG:n/4 . HH:MM . PWR:NN */
        snprintf(tmp, sizeof(tmp), "SIG:%d/4", c.connected ? c.signal_bars : 0);
        put2(n, row, left, t->dim, t->bg, tmp);
        put2(n, row, center, t->bright, t->bg, clk);
        snprintf(tmp, sizeof(tmp), "PWR:%d", b.percent);
        put2(n, row, cols - 2 - (int)strlen(tmp), t->dim, t->bg, tmp);
        break;
    }
    case ST_RITE: { /* boot-rite: telemetry in the teal accent */
        uint32_t teal = t->accent;
        put2(n, row, left, teal, t->bg, "SIM:OK");
        put2(n, row, center, teal, t->bg, clk);
        snprintf(tmp, sizeof(tmp), "PWR:%d", b.percent);
        put2(n, row, cols - 2 - (int)strlen(tmp), teal, t->bg, tmp);
        break;
    }
    case ST_BITS: { /* one-bit: block signal . HH:MM . block battery */
        static const char *sb[] = {"░░░░","█░░░",
            "██░░","███░","████"};
        int s = c.connected ? c.signal_bars : 0; if (s>4) s=4; if (s<0) s=0;
        int bb = (b.percent + 12) / 25; if (bb>4) bb=4; if (bb<0) bb=0;
        put2(n, row, left, t->bright, t->bg, sb[s]);
        put2(n, row, center, t->bright, t->bg, clk);
        put2(n, row, cols - 2 - 4, t->bright, t->bg, sb[bb]);
        break;
    }
    case ST_THERMAL: { /* heat-colored signal + 4-block heat battery */
        put2(n, row, left, 0x85B7EB, t->bg, signal_ramp(c.signal_bars, c.connected));
        put2(n, row, center, t->bright, t->bg, clk);
        /* battery drains from the green (cool) end: hot,warm,cool,empty.
         * Tiles are 2 cells wide to read square (1 cell is a tall 1:2). */
        int bcells = (b.percent + 12) / 25; if (bcells>4) bcells=4; if (bcells<0) bcells=0;
        static const uint32_t heat[4] = {0xE24B4A, 0xEF9F27, 0x97C459, 0x378ADD};
        int bx = cols - 2 - 8;
        for (int i = 0; i < 4; i++)
            put2(n, row, bx + i * 2, (i < bcells) ? heat[i] : 0x2C2C2A, t->bg, "██");
        break;
    }
    case ST_UTC: { /* instrument: two rows of lab telemetry */
        char hms[12]; clock_hms(hms, sizeof(hms));
        snprintf(tmp, sizeof(tmp), "UTC %s", hms);
        put2(n, row, left, t->mid, t->bg, tmp);
        put2(n, row, cols - 3, t->dim, t->bg, "┼"); /* ┼ */
        int rssi = -113 + (c.connected ? c.signal_bars : 0) * 12;
        snprintf(tmp, sizeof(tmp), "RSSI %ddBm", rssi);
        put2(n, row + 1, left, t->dim, t->bg, tmp);
        double vbat = 3.20 + (b.percent / 100.0) * 0.95;
        snprintf(tmp, sizeof(tmp), "VBAT %.2fV", vbat);
        put2(n, row + 1, cols - 2 - (int)strlen(tmp), t->dim, t->bg, tmp);
        /* The telemetry row itself doubles as this theme's divider, so no
         * separate rule is drawn (bh_divider_in_status() returns 1). */
        break;
    }
    case ST_GLYPH:
    default: { /* signal ramp . HH:MM . [battery] */
        uint32_t sigc = t->is_vale ? t->bright : t->mid;
        put2(n, row, left, sigc, t->bg, signal_ramp(c.signal_bars, c.connected));
        put2(n, row, center, t->bright, t->bg, clk);
        char bat[16]; battery_bracket(bat, sizeof(bat), b.percent);
        put2(n, row, cols - 2 - (int)strlen(bat), t->mid, t->bg, bat);
        break;
    }
    }
}

int bh_divider_in_status(void) {
    return theme_active()->status_style == ST_UTC ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  DIVIDER
 * ════════════════════════════════════════════════════════════════════════ */
static void band_fill(struct ncplane *n, int row, int x0, int x1,
                      const char *a, const char *b, uint32_t fg, uint32_t bg) {
    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, bg);
    int k = 0;
    for (int x = x0; x < x1; x++, k++)
        ncplane_putstr_yx(n, row, x, (k % 2) ? b : a);
}

void bh_divider(struct ncplane *n, int row, const char *tag) {
    unsigned urows, ucols;
    ncplane_dim_yx(n, &urows, &ucols);
    int cols = (int)ucols;
    const bh_theme_t *t = theme_active();
    if (!tag) tag = "";

    switch (t->div_style) {
    case DV_LABEL: { /* redline: ──── TAG ──── (tag in accent) */
        ncplane_set_fg_rgb(n, t->deep);
        ncplane_set_bg_rgb(n, t->bg);
        for (int x = 2; x < cols - 2; x++) ncplane_putstr_yx(n, row, x, "─");
        int tl = (int)strlen(tag);
        int tx = (cols - tl) / 2;
        put2(n, row, tx - 1, t->deep, t->bg, " ");
        put2(n, row, tx, t->accent, t->bg, tag);
        put2(n, row, tx + tl, t->deep, t->bg, " ");
        break;
    }
    case DV_BAND: /* rushnyk / boot: ◆╳◆╳ ornament band in the band color */
        band_fill(n, row, 2, cols - 2, "◆", "╳", t->dim, t->bg);
        break;
    case DV_DIM: { /* blueprint: | ──── TAG ──── | dimension line */
        ncplane_set_fg_rgb(n, t->dim);
        ncplane_set_bg_rgb(n, t->bg);
        for (int x = 3; x < cols - 3; x++) ncplane_putstr_yx(n, row, x, "─");
        put2(n, row, 2, t->dim, t->bg, "|");
        put2(n, row, cols - 3, t->dim, t->bg, "|");
        int tl = (int)strlen(tag);
        int tx = (cols - tl) / 2;
        put2(n, row, tx - 1, t->dim, t->bg, " ");
        put2(n, row, tx, t->mid, t->bg, tag);
        put2(n, row, tx + tl, t->dim, t->bg, " ");
        break;
    }
    case DV_DBL: /* dossier: double rule */
        ncplane_set_fg_rgb(n, t->bright);
        ncplane_set_bg_rgb(n, t->bg);
        for (int x = 2; x < cols - 2; x++) ncplane_putstr_yx(n, row, x, "═");
        break;
    case DV_CHK: /* one-bit: checker band */
        band_fill(n, row, 2, cols - 2, "▀", "▄", t->bright, t->bg);
        break;
    case DV_AUR: { /* polar: aurora band */
        static const char *aur[] = {"▁","▂","▄","▆","▄","▂","▁"};
        ncplane_set_bg_rgb(n, t->bg);
        int k = 0;
        for (int x = 2; x < cols - 2; x++, k++) {
            ncplane_set_fg_rgb(n, t->accent);
            ncplane_putstr_yx(n, row, x, aur[k % 7]);
        }
        break;
    }
    case DV_DOT: /* instrument (when used standalone): dotted */
        ncplane_set_fg_rgb(n, t->deep);
        ncplane_set_bg_rgb(n, t->bg);
        for (int x = 2; x < cols - 2; x++) ncplane_putstr_yx(n, row, x, "·");
        break;
    case DV_SOLID:
    default:
        ncplane_set_fg_rgb(n, t->deep);
        ncplane_set_bg_rgb(n, t->bg);
        for (int x = 2; x < cols - 2; x++) ncplane_putstr_yx(n, row, x, "─");
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  FOOTER
 * ════════════════════════════════════════════════════════════════════════ */
void bh_footer(struct ncplane *n, const char *tag) {
    unsigned urows, ucols;
    ncplane_dim_yx(n, &urows, &ucols);
    int rows = (int)urows, cols = (int)ucols;
    if (rows < 4 || cols < 8) return;
    const bh_theme_t *t = theme_active();
    int frow = rows - 2;       /* footer text row   */
    int rrow = rows - 3;       /* rule above footer  */
    if (!tag) tag = "";

    /* rule above the footer (skip for bands which are their own rule) */
    if (t->div_style != DV_BAND && t->footer_style != FT_BIT) {
        uint32_t rc = t->footer_style == FT_DOSSIER ? t->dim : t->deep;
        const char *g = (t->footer_style == FT_UTC) ? "·" : "─";
        ncplane_set_fg_rgb(n, rc);
        ncplane_set_bg_rgb(n, t->bg);
        for (int x = 2; x < cols - 2; x++) ncplane_putstr_yx(n, rrow, x, g);
    }

    switch (t->footer_style) {
    case FT_REDLINE:
        put2(n, frow, 2, t->deep, t->bg, "◤");                 /* ◤ */
        { const char *m = "BH-OS"; put2(n, frow, (cols-(int)strlen(m))/2, t->accent, t->bg, m); }
        put2(n, frow, cols - 3, t->deep, t->bg, "◥");          /* ◥ */
        break;
    case FT_DIAMOND: {
        const char *m = "✶ BLACKHAND ✶";                  /* ✶ BLACKHAND ✶ */
        put2(n, frow, (cols - 13) / 2, t->deep, t->bg, m);
        break;
    }
    case FT_UTC:
        put2(n, frow, 2, t->dim, t->bg, "▁▂▄▂▁▂▆▄▂▁");
        put2(n, frow, cols - 2 - 7, t->dim, t->bg, "NET:LTE");
        break;
    case FT_RITE:
        put2(n, frow, 2, t->accent, t->bg, "INIT OK");
        put2(n, frow, cols - 3, t->dim, t->bg, "✶");
        break;
    case FT_DOSSIER:
        put2(n, frow, 2, t->dim, t->bg, "REF: BLACKHAND/0.4");
        break;
    case FT_BIT: {
        /* inverted tag block on the left, wordmark right */
        char tg[16]; snprintf(tg, sizeof(tg), " %s ", tag[0] ? tag : "MENU");
        put2(n, frow, 2, t->bg, t->bright, tg);
        const char *m = "BLACKHAND";
        put2(n, frow, cols - 2 - (int)strlen(m), t->bright, t->bg, m);
        ncplane_set_fg_rgb(n, t->bright);
        ncplane_set_bg_rgb(n, t->bg);
        for (int x = 2; x < cols - 2; x++) ncplane_putstr_yx(n, rrow, x, "─");
        break;
    }
    case FT_POLAR:
        put2(n, frow, 2, t->mid, t->bg, "-37.81°S");
        put2(n, frow, cols - 2 - 9, t->accent, t->bg, "144.96°E");
        break;
    case FT_WORDMARK:
    default:
        put2(n, frow, 2, t->dim, t->bg, "BLACKHAND");
        put2(n, frow, cols - 2 - 4, t->dim, t->bg, "v0.4");
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  LIST ITEM  (all 10 selection styles)
 * ════════════════════════════════════════════════════════════════════════ */
void bh_list_item(struct ncplane *n, int row, int col, int width,
                  const char *left, const char *right, int sel, int index) {
    const bh_theme_t *t = theme_active();
    if (!left)  left  = "";
    if (!right) right = "";
    int rlen   = (int)strlen(right);
    int right_x = col + width - rlen;
    char buf[64];

    switch (t->sel_style) {
    case SEL_CURSOR: {
        uint32_t sbg = sel ? theme_selection_bg() : t->bg;
        uint32_t fg  = sel ? theme_selection_text() : t->mid;
        if (sel) ghost_fill_rect(n, row, col - 1, 1, width + 2, ' ', fg, sbg);
        snprintf(buf, sizeof(buf), "%s %s", sel ? ">" : " ", left);
        put2(n, row, col, fg, sbg, buf);
        if (rlen) put2(n, row, right_x, sel ? fg : t->dim, sbg, right);
        break;
    }
    case SEL_INDEX: {
        snprintf(buf, sizeof(buf), "%02d", index + 1);
        put2(n, row, col, t->dim, t->bg, buf);
        put2(n, row, col + 3, sel ? t->bright : t->mid, t->bg, left);
        if (rlen)        put2(n, row, right_x, t->dim, t->bg, right);
        else if (sel)    put2(n, row, col + width - 1, t->bright, t->bg, "■"); /* ■ */
        break;
    }
    case SEL_BRACKET: {
        if (sel) snprintf(buf, sizeof(buf), "[ %s ]", left);
        else     snprintf(buf, sizeof(buf), "%s", left);
        int bl = (int)strlen(buf);
        int cx = col + (width - bl) / 2;
        put2(n, row, cx, sel ? t->accent2 : t->mid, t->bg, buf);
        if (rlen) put2(n, row, right_x, t->dim, t->bg, right);
        break;
    }
    case SEL_DIAMOND: {
        snprintf(buf, sizeof(buf), "%s %s", sel ? "◆" : "◇", left); /* ◆ / ◇ */
        put2(n, row, col, sel ? t->bright : t->dim, t->bg, buf);
        if (rlen) put2(n, row, right_x, sel ? t->accent : t->deep, t->bg, right);
        break;
    }
    case SEL_HEAT: {
        snprintf(buf, sizeof(buf), "%s %s", sel ? "›" : " ", left);     /* › */
        put2(n, row, col, sel ? t->accent2 : t->mid, t->bg, buf);
        if (rlen) put2(n, row, right_x, t->dim, t->bg, right);
        break;
    }
    case SEL_REG: {
        uint32_t sbg = sel ? theme_selection_bg() : t->bg;
        uint32_t fg  = sel ? theme_selection_text() : t->mid;
        if (sel) ghost_fill_rect(n, row, col - 1, 1, width + 2, ' ', fg, sbg);
        put2(n, row, col, fg, sbg, left);
        if (rlen) snprintf(buf, sizeof(buf), "%s", right);
        else      snprintf(buf, sizeof(buf), "0x0%X", (index + 1) & 0xF);
        put2(n, row, col + width - (int)strlen(buf), sel ? fg : t->dim, sbg, buf);
        break;
    }
    case SEL_BOX: {
        if (sel) {
            snprintf(buf, sizeof(buf), "[%s", left);
            put2(n, row, col, t->bright, t->bg, buf);
            put2(n, row, col + width - 1, t->bright, t->bg, "]");
        } else {
            put2(n, row, col + 1, t->mid, t->bg, left);
        }
        snprintf(buf, sizeof(buf), "A·%d", index + 1);                   /* A·n */
        if (rlen) snprintf(buf, sizeof(buf), "%s", right);
        put2(n, row, col + width - 1 - (int)strlen(buf), sel ? t->mid : t->dim, t->bg, buf);
        break;
    }
    case SEL_INK: {
        uint32_t sbg = sel ? theme_selection_bg() : t->bg;   /* solid ink bar */
        uint32_t fg  = sel ? theme_selection_text() : t->mid;
        if (sel) ghost_fill_rect(n, row, col - 1, 1, width + 2, ' ', fg, sbg);
        put2(n, row, col, fg, sbg, left);
        if (rlen) put2(n, row, right_x, sel ? fg : t->deep, sbg, right);
        break;
    }
    case SEL_BIT: {
        uint32_t sbg = sel ? t->bright : t->bg;              /* full invert */
        uint32_t fg  = sel ? t->bg : t->bright;
        if (sel) ghost_fill_rect(n, row, col - 1, 1, width + 2, ' ', fg, sbg);
        put2(n, row, col, fg, sbg, left);
        if (rlen) put2(n, row, right_x, fg, sbg, right);
        break;
    }
    case SEL_STAR:
    default: {
        snprintf(buf, sizeof(buf), "%s %s", sel ? "✶" : " ", left);     /* ✶ */
        put2(n, row, col, sel ? 0xFFFFFF : t->mid, t->bg, buf);
        if (rlen) put2(n, row, right_x, t->dim, t->bg, right);
        break;
    }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  ACTION CELLS  (global semantic colors)
 * ════════════════════════════════════════════════════════════════════════ */
void bh_action_cells(struct ncplane *n, const char *left, const char *right, int focus) {
    unsigned urows, ucols;
    ncplane_dim_yx(n, &urows, &ucols);
    int rows = (int)urows, cols = (int)ucols;
    if (rows < 4 || cols < 12) return;
    const bh_theme_t *t = theme_active();
    if (!left)  left  = "OK";
    if (!right) right = "NO";

    int frow  = rows - 2;
    int mid   = cols / 2;
    int lw    = mid - 3;             /* left cell inner width  */
    int gap   = 3;
    int lx    = 2;
    int rx    = mid + 1;
    int rw    = cols - 2 - rx;
    (void)gap; (void)lw; (void)rw;

    uint32_t ab = theme_affirm_border(),  at = theme_affirm_text();
    uint32_t db = theme_destruct_border(), dt = theme_destruct_text();

    /* left = affirmative */
    {
        int focused = (focus == 0);
        int cw = mid - 1 - lx;
        if (focused) ghost_fill_rect(n, frow, lx, 1, cw, ' ', t->bg, ab);
        int tx = lx + (cw - (int)strlen(left)) / 2;
        put2(n, frow, tx, focused ? t->bg : at, focused ? ab : t->bg, left);
        /* side ticks to read as a bordered cell */
        put2(n, frow, lx, focused ? t->bg : ab, focused ? ab : t->bg, "[");
        put2(n, frow, lx + cw - 1, focused ? t->bg : ab, focused ? ab : t->bg, "]");
    }
    /* right = destructive */
    {
        int focused = (focus == 1);
        int cw = cols - 2 - rx;
        if (focused) ghost_fill_rect(n, frow, rx, 1, cw, ' ', t->bg, db);
        int tx = rx + (cw - (int)strlen(right)) / 2;
        put2(n, frow, tx, focused ? t->bg : dt, focused ? db : t->bg, right);
        put2(n, frow, rx, focused ? t->bg : db, focused ? db : t->bg, "[");
        put2(n, frow, rx + cw - 1, focused ? t->bg : db, focused ? db : t->bg, "]");
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SCREEN TAG MAPPING
 * ════════════════════════════════════════════════════════════════════════ */
const char *bh_screen_tag(const char *s) {
    if (!s) return "";
    if (!strcmp(s, "HOME"))     return "MENU";
    if (!strcmp(s, "SETTINGS")) return "CONF";
    if (!strcmp(s, "CALLS"))    return "DIAL";
    if (!strcmp(s, "MESSAGES")) return "MSGS";
    if (!strcmp(s, "CONTACTS")) return "CNTC";
    if (!strcmp(s, "MP3"))      return "PLAY";
    if (!strcmp(s, "VOICE"))    return "MEMO";
    if (!strcmp(s, "NOTES"))    return "NOTE";
    if (!strcmp(s, "ALARM"))    return "WAKE";
    if (!strcmp(s, "THEME"))    return "THEM";
    if (!strcmp(s, "BT"))       return "CONN";
    return s;
}
