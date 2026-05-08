/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║  main.c — BlackHand OS  ·  Entry Point & UI Engine                     ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *  HOW TO READ THESE COMMENTS
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *  Every C and Notcurses concept is explained the first time it appears,
 *  then used without repetition ater that.  Read this file top-to-bottom
 *  once before building any new screen — after that you will have everything
 *  you need to write any view independently.
 *
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *  ARCHITECTURE OVERVIEW
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *
 *  ┌─────────────────────────────────────────────────────────────┐
 *  │  main()                                                     │
 *  │    │                                                        │
 *  │    ├── hardware_init()          read battery/signal         │
 *  │    ├── notcurses_init()         start terminal graphics     │
 *  │    ├── create_phone_plane()     our drawing canvas          │
 *  │    │                                                        │
 *  │    └── LOOP ─────────────────────────────────────────────  │
 *  │            │                                                │
 *  │            ├── draw_frame()     border + status bar        │
 *  │            ├── screen_*_draw()  active screen content      │
 *  │            ├── notcurses_render() push to terminal         │
 *  │            └── handle input → update screen_id            │
 *  └─────────────────────────────────────────────────────────────┘
 *
 *  TO ADD A NEW SCREEN — complete checklist:
 *  ─────────────────────────────────────────
 *    1.  Add   SCREEN_CALLS       to the screen_id enum in ui.h
 *    2.  Add   "CALLS"            to the screen_name switch in main()
 *    3.  Create screen_calls.c    with screen_calls_draw() and
 *                                      screen_calls_input()
 *    4.  Declare both functions   in ui.h
 *    5.  Add a draw case          to the draw switch in the main loop
 *    6.  Add an input case        to the input switch in the main loop
 *    7.  Route a key to it        in whichever screen_*_input() navigates there
 *
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *  RECOMMENDED DISPLAY DIMENSIONS  (HyperPixel 4.0  480 × 800 portrait)
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *
 *  Font: Iosevka Term  ss08 variant  (thin, razor-precise, luxury feel)
 *  Install: build from https://github.com/be5invis/Iosevka
 *  Or use pre-built: apt install fonts-iosevka  (may not include ss08)
 *
 *  Set in /etc/default/console-setup:
 *    FONTFACE="Iosevka Term"
 *    FONTSIZE="12x20"         ← approx 40 cols × 40 rows on HyperPixel 4.0
 *
 *  Alternative: Departure Mono  — bitmap-inspired, unique rhythm, very
 *  distinctive.  https://departuremono.com/  (free, open source)
 *
 *  Vertu Signature S had a 240×320 display (3:4 portrait ratio, very tall
 *  and narrow).  To match that feel:
 *
 *    #define PHONE_COLS  36    // ~90 % of a 40-col terminal
 *    #define PHONE_ROWS  38    // ~95 % of a 40-row terminal
 *
 *  This leaves a 2-cell margin around the frame — enough for the DEV_LABEL
 *  corner text without crowding the border.  The 36:38 ratio is close to
 *  the Signature S's tall-narrow silhouette.
 *
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *  CONTENT AREA — where your screen functions draw
 *  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *
 *  Row 0          ┏━━━━━━━━━━━━━━━━━━┓  top border
 *  Row 1          ┃ 09:41    ▰▰▰▱ ●●┃  status bar   (STATUS_ROW = 1)
 *  Row 2          ┣━━━━ HOME ━━━━━━━┫  separator
 *  Rows 3..35     ┃                  ┃  CONTENT AREA  ← screens draw here
 *  Row 36         ┗━━━━━━━━━━━━━━━━━━┛  bottom border
 *
 *  Content area:
 *    First row  = 3                  (CONTENT_ROW_START in config.h)
 *    Last row   = PHONE_ROWS - 2     (last interior row above bottom border)
 *    First col  = 2                  (one inside the border + one margin)
 *    Last col   = PHONE_COLS - 3     (one inside border + one margin)
 *    Inner width = PHONE_COLS - 4    (usable columns per row)
 */


/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 1: INCLUDES
 *
 *  C CONCEPT: #include
 *  ────────────────────
 *  #include copies the contents of another file into this one at compile
 *  time.  Think of it as "paste this code here before compiling."
 *
 *  Two forms:
 *    #include <file.h>   — system/library headers, searched in system paths
 *    #include "file.h"   — your own headers, searched from current directory
 *
 *  Headers (.h files) contain DECLARATIONS — they tell the compiler what
 *  functions and types exist without providing their full implementation.
 *  The compiler needs declarations before it can compile calls to those
 *  functions.  The actual implementations live in .c files.
 * ══════════════════════════════════════════════════════════════════════════ */

#include <stdlib.h>
/*  Provides setenv().  Used to set TERM=linux as a fallback when running
 *  directly on the framebuffer console (no shell to set it for us).        */

#include <locale.h>
/*  Provides setlocale().  Must be called before any Unicode output so the
 *  C runtime uses the correct character encoding (UTF-8 on the Pi).
 *  Without it, multi-byte characters like ▰ ● ┏ display as garbage.        */

#include <notcurses/notcurses.h>
/*  The entire Notcurses API.  Key types you will use in every screen:
 *
 *  struct notcurses *nc
 *    The engine — one per program.  Created by notcurses_init(),
 *    destroyed by notcurses_stop().  Manages the terminal, all planes,
 *    the render pipeline, and input.
 *
 *  struct ncplane *plane
 *    A rectangular drawing surface — our canvas.  Like a transparent layer
 *    in Photoshop.  Multiple planes exist simultaneously and composite on
 *    render.  The standard plane (stdplane) covers the whole terminal.
 *    Every screen_*_draw() function receives the phone plane and draws on it.
 *
 *  nccell
 *    One character cell: glyph + fg colour + bg colour + style flags.
 *    Used directly when working with box-drawing borders.
 *
 *  ncinput
 *    One keyboard or mouse event.  Fields: .id (key code), .evtype,
 *    .modifiers (shift/ctrl/alt), .y and .x (mouse position).             */

#include <stdint.h>
/*  Fixed-width integer types.  Notcurses uses these for colours and keys:
 *
 *  uint32_t  unsigned 32-bit (0 – 4,294,967,295)
 *            Used for 0xRRGGBB colours and Unicode key codes.
 *
 *  uint64_t  unsigned 64-bit
 *            Used for Notcurses "channels" (packed fg+bg colour pair).
 *
 *  uint8_t   unsigned 8-bit (0 – 255)
 *            Useful for individual R, G, B components when doing colour math.
 *
 *  WHY NOT JUST USE 'int'?
 *  Plain 'int' is platform-dependent — 16, 32, or 64 bits depending on
 *  CPU and compiler.  uint32_t is always exactly 32 bits, which Notcurses
 *  requires for its packed colour encoding.                                  */

#include <stdio.h>
/*  Standard I/O.  Functions used here:
 *    fprintf(stderr, "msg\n")           print errors to stderr
 *    snprintf(buffer, size, fmt, ...)   format a string into a char array   */

#include <string.h>
/*  String functions.  Used here:
 *    strlen(str)          count bytes in str (not counting '\0')
 *    strcat(dest, src)    append src to dest (dest must have room!)          */

#include <stdbool.h>
#include <time.h>
/*  Provides 'bool', 'true' (= 1), and 'false' (= 0).
 *  Without this header you'd write int charging = 1 instead of
 *  bool charging = true — less readable and more error-prone.               */

#include "ui.h"
/*  Our screen system.  Contains:
 *    typedef enum { SCREEN_HOME, SCREEN_SETTINGS, … } screen_id;
 *    Declarations for screen_*_draw() and screen_*_input() functions.
 *
 *  An enum is a set of named integer constants.  The compiler assigns
 *  sequential values starting from 0 unless you specify otherwise.
 *  Using enum names instead of raw integers makes switch statements
 *  self-documenting and lets the compiler warn about unhandled cases.       */

#include "config.h"
/*  All magic numbers in one place.  Add to this file whenever you need
 *  a new constant — never hardcode numbers directly in .c files.
 *
 *  Key constants defined here:
 *    PHONE_ROWS, PHONE_COLS          frame dimensions
 *    STATUS_ROW          = 1         status bar row
 *    STATUS_BATTERY_COL  = 2         battery glyphs start column
 *    STATUS_BATTERY_PCT_COL          percentage label column
 *    CONTENT_ROW_START   = 3         first row screens may draw on
 *    FRAME_MIN_ROWS, FRAME_MIN_COLS  safety limits before drawing
 *    COL_BG              0x080808    background (near-black)
 *    COL_BORDER          0x242424    border colour
 *    COL_SEPARATOR       0x1C1C1C    separator line colour
 *    COL_GHOST_ON        0xE0E0E0    active glyph, off-white
 *    COL_GHOST_OFF       0x1E1E1E    inactive glyph, near-invisible
 *    COL_GHOST_PCT       0x2C2C2C    percentage/dim text
 *    COL_GHOST_LOW       0x7F1D1D    low battery warning, deep red
 *    COL_PLACEHOLDER     0x333333    placeholder text
 *    COL_HINT            0x2A2A2A    key-hint text
 *    COL_DEV_LABEL       0x1A1A1A    corner dev tag                        */

#include "platform/hardware.h"
#include "services/settings_service.h"
#include "frame_renderer.h"
#include "draw_utils.h"
#include "services/theme_service.h"
#include "services/notes_service.h"
#include "services/mp3_service.h"
#include "services/voice_memo_service.h"
#include "services/contacts_service.h"
#include "services/alarm_service.h"
#include "services/comm_service.h"
#include "services/pin_service.h"
#include "services/bluetooth_service.h"


/* ══════════════════════════════════════════════════════════════════════════
 *  DEBUG INPUT DISPLAY
 *
 *  Shows the last input event on screen so you can verify what the device
 *  is sending.  Remove this section once input is confirmed working.
 * ══════════════════════════════════════════════════════════════════════════ */
static char g_debug_line1[128] = "NO INPUT YET";
static char g_debug_line2[128] = "";
static int  g_debug_touch_x = -1;
static int  g_debug_touch_y = -1;
static int  g_debug_event_count = 0;

static void debug_record_event(uint32_t key, const ncinput *ni) {
    g_debug_event_count++;

    const char *evtype_str = "?";
    switch (ni->evtype) {
        case NCTYPE_UNKNOWN: evtype_str = "UNK"; break;
        case NCTYPE_PRESS:   evtype_str = "PRS"; break;
        case NCTYPE_REPEAT:  evtype_str = "RPT"; break;
        case NCTYPE_RELEASE: evtype_str = "REL"; break;
        default:             evtype_str = "???"; break;
    }

    if (key == NCKEY_BUTTON1 || key == NCKEY_BUTTON2 || key == NCKEY_BUTTON3) {
        g_debug_touch_y = (int)ni->y;
        g_debug_touch_x = (int)ni->x;
        snprintf(g_debug_line1, sizeof(g_debug_line1),
                 "#%d TOUCH btn=%d y=%d x=%d %s",
                 g_debug_event_count,
                 (key == NCKEY_BUTTON1) ? 1 : (key == NCKEY_BUTTON2) ? 2 : 3,
                 (int)ni->y, (int)ni->x, evtype_str);
    } else if (key > 0x100000) {
        /* Special key (arrow, enter, etc.) */
        const char *name = "SPECIAL";
        if (key == NCKEY_UP)        name = "UP";
        else if (key == NCKEY_DOWN) name = "DOWN";
        else if (key == NCKEY_LEFT) name = "LEFT";
        else if (key == NCKEY_RIGHT)name = "RIGHT";
        else if (key == NCKEY_ENTER)name = "ENTER";
        else if (key == NCKEY_BACKSPACE) name = "BKSP";
        else if (key == NCKEY_TAB)  name = "TAB";
        else if (key == NCKEY_RESIZE) name = "RESIZE";
        snprintf(g_debug_line1, sizeof(g_debug_line1),
                 "#%d KEY=0x%X [%s] %s",
                 g_debug_event_count, key, name, evtype_str);
    } else if (key >= 32 && key <= 126) {
        snprintf(g_debug_line1, sizeof(g_debug_line1),
                 "#%d KEY='%c' (0x%X) %s",
                 g_debug_event_count, (char)key, key, evtype_str);
    } else {
        snprintf(g_debug_line1, sizeof(g_debug_line1),
                 "#%d KEY=0x%X %s",
                 g_debug_event_count, key, evtype_str);
    }

    /* Line 2: touch marker position */
    if (g_debug_touch_x >= 0) {
        snprintf(g_debug_line2, sizeof(g_debug_line2),
                 "LAST TOUCH: y=%d x=%d", g_debug_touch_y, g_debug_touch_x);
    }
}

static void debug_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    /* Draw debug info at the very bottom of the phone plane */
    int dbg_row1 = (int)rows - 2;
    int dbg_row2 = (int)rows - 1;
    if (dbg_row1 < 0) return;

    ncplane_set_fg_rgb(phone, 0x00FF00);  /* bright green for visibility */
    ncplane_set_bg_rgb(phone, 0x000000);  /* black background */

    /* Clear the debug rows */
    for (int x = 0; x < (int)cols; x++) {
        ncplane_putchar_yx(phone, dbg_row1, x, ' ');
        if (dbg_row2 < (int)rows)
            ncplane_putchar_yx(phone, dbg_row2, x, ' ');
    }

    ncplane_set_fg_rgb(phone, 0x00FF00);
    ncplane_set_bg_rgb(phone, 0x000000);
    ncplane_putstr_yx(phone, dbg_row1, 0, g_debug_line1);

    if (g_debug_line2[0] && dbg_row2 < (int)rows) {
        ncplane_set_fg_rgb(phone, 0xFFFF00);  /* yellow */
        ncplane_set_bg_rgb(phone, 0x000000);
        ncplane_putstr_yx(phone, dbg_row2, 0, g_debug_line2);
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 3: PHONE PLANE CREATION
 * ══════════════════════════════════════════════════════════════════════════ */

/* ──────────────────────────────────────────────────────────────────────────
 *  create_phone_plane()  —  Full-bleed canvas covering the whole terminal.
 *
 *  The TUI fills the terminal completely (no border, no keypad zone below).
 *  All screen draw functions receive this single plane.
 * ────────────────────────────────────────────────────────────────────────── */
static struct ncplane *create_phone_plane(struct ncplane *std) {
    unsigned term_rows, term_cols;
    ncplane_dim_yx(std, &term_rows, &term_cols);

    struct ncplane_options opts = {
        .y    = 0,
        .x    = 0,
        .rows = (int)term_rows,
        .cols = (int)term_cols,
        .name = "phone",
    };

    return ncplane_create(std, &opts);
}

/* ──────────────────────────────────────────────────────────────────────────
 *  screen_zone_touch_key — map a screen tap position to a navigation key.
 *
 *  With no visual keypad, the whole screen is the interaction surface.
 *  Zones (relative to total screen area):
 *    Left 20%        → 'q'  (back / LSK)
 *    Right 20%       → 'e'  (open / RSK)
 *    Top 30% center  → UP
 *    Bottom 30% ctr  → DOWN
 *    Middle center   → ENTER
 *
 *  The status bar area (rows 0-1) is excluded from tap handling.
 * ────────────────────────────────────────────────────────────────────────── */
static uint32_t screen_zone_touch_key(int y, int x, int rows, int cols) {
    /* Ignore taps on the status bar */
    if (y <= STATUS_ROW + 1) return 0;

    int content_rows = rows - (STATUS_ROW + 2);
    int rel_y = y - (STATUS_ROW + 2);
    if (content_rows <= 0) return 0;

    /* Left / right edge zones */
    int edge_w = cols / 5;
    if (edge_w < 2) edge_w = 2;
    if (x < edge_w)          return 'q';
    if (x >= cols - edge_w)  return 'e';

    /* Top / bottom / center zones in the middle strip */
    int top_band    = content_rows * 3 / 10;
    int bottom_band = content_rows * 7 / 10;

    if (rel_y < top_band)    return NCKEY_UP;
    if (rel_y >= bottom_band) return NCKEY_DOWN;
    return NCKEY_ENTER;
}


/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 6: MAIN
 *
 *  C CONCEPT: int main(void)
 *  ──────────────────────────
 *  The OS calls main() to start the program.
 *  Return 0 = success.  Return non-zero = error.
 *  Check with:  echo $?  in the shell after the program exits.
 *
 *  C CONCEPT: the event loop pattern
 *  ─────────────────────────────────────
 *  All interactive terminal programs follow this structure:
 *    1.  Initialise resources
 *    2.  LOOP until quit:
 *          a.  Draw current state to buffer
 *          b.  Render buffer to terminal
 *          c.  Block waiting for input  (CPU idle here — no busy-wait)
 *          d.  Update state based on input
 *    3.  Clean up resources
 *
 *  The 'tick' counter increments every frame and drives all animations.
 *  At 10fps, tick advances 10 per second.  tick % N cycles every N/10 sec.
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void) {

    /* ── TERM fallback — ensure notcurses knows the terminal type ────── */
    /*
     * setenv("TERM", "linux", 0)
     * ───────────────────────────
     * The third argument (0) means "don't overwrite" — if TERM is already
     * set (e.g. by the shell or inittab), this is a no-op.  But if the UI
     * is launched directly from init with no shell, TERM may be unset,
     * which causes notcurses_init() to fail.  "linux" is the correct
     * terminal type for the kernel framebuffer console (fbcon).
     */
    setenv("TERM", "linux", 0);

    /* ── Locale — MUST be first, before any Unicode output ─────────────── */
    /*
     * setlocale(LC_ALL, "")
     * ──────────────────────
     * Sets the program's locale to the system default (read from the LANG
     * or LC_ALL environment variable).  On Raspbian this is typically
     * en_GB.UTF-8 or en_US.UTF-8 — both enable correct UTF-8 output.
     * LC_ALL affects all categories: encoding, numbers, dates, collation.
     */
    setlocale(LC_ALL, "");

    /* ── Hardware ───────────────────────────────────────────────────────── */
    hardware_init();
    settings_service_init();
    pin_service_init();
    theme_service_init();
    notes_service_init();
    mp3_service_init("/data/music");
    voice_memo_service_init();
    contact_service_init();
    alarm_service_init();
    comm_service_init();
    bluetooth_service_init();

    /* ── Notcurses initialisation ───────────────────────────────────────── */
    /*
     * struct notcurses_options
     * ─────────────────────────
     * Configuration for notcurses_init().  Unset fields default to 0.
     *
     * NCOPTION_SUPPRESS_BANNERS
     *   Prevents Notcurses printing "notcurses vX.Y.Z" on startup.
     *   Always use this in production so the terminal looks clean.
     *
     * Other useful flags to know:
     *   NCOPTION_NO_ALTERNATE_SCREEN  keep terminal scrollback (good during dev)
     *   NCOPTION_NO_WINCH_SIGHANDLER  don't auto-handle resize signals
     *   NCOPTION_NO_QUIT_SIGHANDLERS  don't auto-handle Ctrl-C
     */
    struct notcurses_options nc_opts = {
        .flags = NCOPTION_SUPPRESS_BANNERS,
    };

    /*
     * notcurses_init(options, output_stream)
     * ────────────────────────────────────────
     * output_stream = NULL means stdout (the terminal).
     *
     * What this does internally:
     *   • Queries terminal capabilities (colour depth, Unicode, box chars)
     *   • Enters the "alternate screen"  (your scrollback is preserved)
     *   • Enables raw input mode  (no line buffering, no echo)
     *   • Creates the standard plane covering the full terminal
     *
     * Returns NULL on failure.  Always check before using 'nc'.
     */
    struct notcurses *nc = notcurses_init(&nc_opts, NULL);
    if (!nc) {
        fprintf(stderr, "Notcurses init failed\n");
        return 1;
    }

    /* ── Enable mouse/touchscreen input ─────────────────────────────── */
    /*
     * Without this call notcurses never delivers NCKEY_BUTTON1 events,
     * so touchscreen taps are silently dropped.  NCMICE_ALL_EVENTS
     * captures press, release, and motion (we filter to press only in
     * the event loop below).
     */
    notcurses_mice_enable(nc, NCMICE_ALL_EVENTS);

    /*
     * notcurses_stdplane(nc)
     * ───────────────────────
     * Returns the standard plane — always exists after successful init,
     * covers the full terminal, is the root of the plane tree.
     * Never returns NULL.  Use as parent for our phone plane.
     */
    struct ncplane *std = notcurses_stdplane(nc);

    /* ── Phone plane (full-bleed TUI canvas) ─────────────────────────── */
    struct ncplane *phone = create_phone_plane(std);
    if (!phone) {
        notcurses_stop(nc);
        return 1;
    }
    /* 'screen' is an alias — all draw functions use one plane */
    struct ncplane *screen = phone;

    /* ── State ──────────────────────────────────────────────────────────── */
    /*
     * current_screen tracks which view is active.
     * Changing this in the input handler is the entire navigation system.
     * The enum type (screen_id) means the compiler will warn if you assign
     * a value that doesn't exist in the enum.
     */
    screen_id current_screen = SCREEN_HOME;

    /*
     * tick drives all animation.  Increments each frame.
     * int holds up to ~2.1 billion — won't overflow for years.
     * All animation uses tick % N so it loops forever.
     */
    int tick = 0;

    uint32_t last_dispatch_key = 0;
    struct timespec last_dispatch_ts = {0};

    /* ── Event loop ─────────────────────────────────────────────────────── */
    while (1) {

        /* ── RESOLVE SCREEN NAME ───────────────────────────────────────── */
        const char *screen_name;
        switch (current_screen) {
            case SCREEN_HOME:     screen_name = "HOME";     break;
            case SCREEN_SETTINGS: screen_name = "SETTINGS"; break;
            case SCREEN_CALLS:    screen_name = "CALLS";    break;
            case SCREEN_MESSAGES: screen_name = "MESSAGES"; break;
            case SCREEN_CONTACTS: screen_name = "CONTACTS"; break;
            case SCREEN_MP3:      screen_name = "MP3";      break;
            case SCREEN_VOICE_MEMO: screen_name = "VOICE";  break;
            case SCREEN_NOTES:    screen_name = "NOTES";    break;
            case SCREEN_ALARM:    screen_name = "ALARM";    break;
            case SCREEN_THEME:    screen_name = "THEME";    break;
            case SCREEN_BLUETOOTH:screen_name = "BT";       break;
            default:              screen_name = "";           break;
        }

        /* ── DRAW PHASE ──────────────────────────────────────────────────── */
        /*
         * Drawing order:
         *   1. draw_frame() on the screen plane — clears + draws border/status
         *   2. screen_*_draw() on the screen plane — content inside the frame
         *   3. notcurses_render() — composite everything to terminal
         *
         * The screen plane is a child of phone, positioned at (0,0) and
         * sized PHONE_SCREEN_ROWS × PHONE_COLS.  All screen functions see
         * only this PHONE_SCREEN_ROWS-row canvas.  The keypad is drawn on
         * the parent phone plane starting at KEYPAD_START_ROW.
         */
        draw_frame(screen, tick, screen_name);
        tick++;
        voice_memo_service_tick();
        mp3_service_update();
        if (alarm_service_tick()) {
            if (mp3_service_get_state() == MP3_PLAYING) {
                mp3_service_pause();
            }
            voice_memo_service_play_stop();
            current_screen = SCREEN_ALARM;
        }

        switch (current_screen) {
            case SCREEN_HOME:       screen_home_draw(screen);       break;
            case SCREEN_SETTINGS:   screen_settings_draw(screen);   break;
            case SCREEN_CALLS:      screen_calls_draw(screen);      break;
            case SCREEN_MESSAGES:   screen_messages_draw(screen);   break;
            case SCREEN_CONTACTS:   screen_contacts_draw(screen);   break;
            case SCREEN_MP3:        screen_mp3_draw(screen);        break;
            case SCREEN_VOICE_MEMO: screen_voice_memo_draw(screen); break;
            case SCREEN_NOTES:      screen_notes_draw(screen);      break;
            case SCREEN_ALARM:      screen_alarm_draw(screen);      break;
            case SCREEN_THEME:      screen_theme_draw(screen);      break;
            case SCREEN_BLUETOOTH:  screen_bluetooth_draw(screen);  break;
            default:
                ghost_text(screen, CONTENT_START_ROW + 1, CONTENT_COL, COL_PLACEHOLDER, TEXT_COMING_SOON);
                ghost_text(screen, CONTENT_START_ROW + 3, CONTENT_COL, COL_HINT,        TEXT_GO_HOME);
                break;
        }

        /* ── DEBUG OVERLAY (drawn last, on top of everything) ────────── */
        debug_draw(phone);

        /* ── RENDER ──────────────────────────────────────────────────── */
        notcurses_render(nc);

        /* ── INPUT ───────────────────────────────────────────────────── */
        ncinput ni;
        struct timespec timeout = { .tv_sec = 0, .tv_nsec = 33000000L };
        uint32_t key = notcurses_get(nc, &timeout, &ni);

        if (key == 0) {
            continue;
        }

        debug_record_event(key, &ni);

        if (ni.evtype == NCTYPE_REPEAT) {
            continue;
        }

        /* ── Touchscreen tap → navigation key ───────────────────────── */
        /*
         * The visual keypad is gone.  Map touch taps to screen zones:
         *   left edge  → back ('q')
         *   right edge → open ('e')
         *   top center → UP
         *   bottom ctr → DOWN
         *   middle     → ENTER
         *
         * We process only RELEASE events to avoid double-dispatch on
         * drivers that emit both PRESS and RELEASE for one tap.
         */
        if (key == NCKEY_BUTTON1 || key == NCKEY_BUTTON2 || key == NCKEY_BUTTON3) {
            if (ni.evtype != NCTYPE_RELEASE) {
                continue;
            }
            unsigned ph_rows, ph_cols;
            ncplane_dim_yx(phone, &ph_rows, &ph_cols);
            uint32_t mapped = screen_zone_touch_key(
                (int)ni.y, (int)ni.x, (int)ph_rows, (int)ph_cols);
            if (mapped == 0) continue;
            key = mapped;
        } else if (ni.evtype == NCTYPE_RELEASE) {
            continue;
        }

        /* ── Numeric key → arrow key fallback (hardware d-pad) ──────── */
        /*
         * On fbcon without proper terminfo, arrow keys may arrive as
         * numeric characters.  Map them here so hardware navigation works.
         * Skip when a text-entry screen needs the numeric keys for multi-tap.
         */
        if (!((current_screen == SCREEN_NOTES && screen_notes_is_edit_mode()) ||
              (current_screen == SCREEN_CONTACTS && screen_contacts_is_edit_mode()))) {
            switch (key) {
                case '2': key = NCKEY_UP;    break;
                case '8': key = NCKEY_DOWN;  break;
                case '4': key = NCKEY_LEFT;  break;
                case '6': key = NCKEY_RIGHT; break;
                case '5': key = NCKEY_ENTER; break;
                default: break;
            }
        }

        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        long delta_ms = (now_ts.tv_sec - last_dispatch_ts.tv_sec) * 1000L +
                        (now_ts.tv_nsec - last_dispatch_ts.tv_nsec) / 1000000L;
        if (key == last_dispatch_key && delta_ms >= 0 && delta_ms < 45) {
            continue;
        }
        last_dispatch_key = key;
        last_dispatch_ts = now_ts;

        /* ── GLOBAL KEYS ─────────────────────────────────────────────── */
        if (key == NCKEY_RESIZE) {
            ncplane_destroy(phone);
            phone = create_phone_plane(std);
            if (!phone) break;
            screen = phone;
            continue;
        }
        if (current_screen == SCREEN_HOME && (key == 'q' || key == 'Q')) {
            break;
        }

        /* ── SCREEN-SPECIFIC INPUT ROUTING ───────────────────────────── */
        screen_id prev_screen = current_screen;
        switch (current_screen) {
            case SCREEN_HOME:       current_screen = screen_home_input(key);       break;
            case SCREEN_SETTINGS:   current_screen = screen_settings_input(key);   break;
            case SCREEN_CALLS:      current_screen = screen_calls_input(key);      break;
            case SCREEN_MESSAGES:   current_screen = screen_messages_input(key);   break;
            case SCREEN_CONTACTS:   current_screen = screen_contacts_input(key);   break;
            case SCREEN_MP3:        current_screen = screen_mp3_input(key);        break;
            case SCREEN_VOICE_MEMO: current_screen = screen_voice_memo_input(key); break;
            case SCREEN_NOTES:      current_screen = screen_notes_input(key);      break;
            case SCREEN_ALARM:      current_screen = screen_alarm_input(key);      break;
            case SCREEN_THEME:      current_screen = screen_theme_input(key);      break;
            case SCREEN_BLUETOOTH:  current_screen = screen_bluetooth_input(key);  break;
            default: break;
        }

        if (current_screen == SCREEN_MP3 && prev_screen != SCREEN_MP3) {
            mp3_service_rescan("/data/music");
        }

        if (current_screen == SCREEN_CALLS && prev_screen != SCREEN_CALLS) {
            mp3_service_stop();
        }
    }

    /* ── Cleanup ─────────────────────────────────────────────────────────── */
    ncplane_destroy(phone);
    notcurses_stop(nc);
    mp3_service_shutdown();
    voice_memo_service_shutdown();
    contact_service_shutdown();
    alarm_service_shutdown();
    comm_service_shutdown();
    bluetooth_service_shutdown();
    notes_service_shutdown();
    settings_service_shutdown();
    hardware_cleanup();

    return 0;
}
