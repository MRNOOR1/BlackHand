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
#include "keypad_renderer.h"
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
 *  create_phone_plane()  —  Create the centred phone canvas
 *
 *  RETURNS: pointer to the new plane, or NULL on failure.
 *  Always check for NULL — using a NULL pointer crashes the program.
 *
 *  NOTCURSES: plane hierarchy
 *  ───────────────────────────
 *  Planes form a tree.  Each child plane is positioned RELATIVE to its
 *  parent.  Moving the parent moves all children with it.  Destroying
 *  the parent destroys all children too.
 *
 *  stdplane (full terminal)
 *    └── phone plane (centred child)
 *
 *  NOTCURSES: struct ncplane_options
 *  ────────────────────────────────────
 *  Configuration for ncplane_create():
 *    .y     top-left row, relative to parent
 *    .x     top-left column, relative to parent
 *    .rows  height in terminal rows
 *    .cols  width in terminal columns
 *    .name  debug name (appears in notcurses diagnostics, not on screen)
 *
 *  NOTCURSES: ncplane_dim_yx(plane, *rows, *cols)
 *  ─────────────────────────────────────────────────
 *  Reads the plane dimensions into variables via pointers.  '&' gets the
 *  address of a variable:
 *    &term_rows  is the memory address where term_rows lives
 *  The function writes the dimensions to those addresses.  After the call,
 *  term_rows and term_cols hold the terminal's current dimensions.
 *  This is "pass by pointer" — C's way for a function to "return" multiple
 *  values (since a function has only one actual return slot).
 *
 *  C CONCEPT: struct designated initializers
 *  ───────────────────────────────────────────
 *  { .field = value }  syntax sets only the named fields.
 *  All other fields are zero-initialised.  This is safer than positional
 *  initializers because adding a new field to the struct later won't
 *  silently shift your values.
 * ────────────────────────────────────────────────────────────────────────── */
/* ──────────────────────────────────────────────────────────────────────────
 *  create_phone_plane()  —  Create the full phone plane (screen + keypad)
 *
 *  The plane covers the full PHONE_ROWS height.  The top PHONE_SCREEN_ROWS
 *  hold the display (status bar, content, softkey footer).  The bottom
 *  KEYPAD_ROWS hold the visual on-screen keypad.
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
 *  create_screen_plane()  —  Create the screen sub-plane (child of phone)
 *
 *  This child plane occupies only the top PHONE_SCREEN_ROWS of the phone
 *  plane.  All screen draw/input functions receive this plane so they see
 *  only the screen area (PHONE_SCREEN_ROWS × PHONE_COLS) and never draw into
 *  the keypad.
 * ────────────────────────────────────────────────────────────────────────── */
static struct ncplane *create_screen_plane(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    keypad_layout layout = keypad_layout_compute((int)rows, (int)cols);
    int screen_rows = layout.start_row;
    if (screen_rows < 8) screen_rows = 8;
    if (screen_rows >= (int)rows) screen_rows = (int)rows > 1 ? ((int)rows - 1) : 1;

    struct ncplane_options opts = {
        .y    = 0,
        .x    = 0,
        .rows = screen_rows,
        .cols = (int)cols,
        .name = "screen",
    };
    return ncplane_create(phone, &opts);
}

/* ──────────────────────────────────────────────────────────────────────────
 *  keypad_mouse_to_key — translate a touchscreen tap into a key code.
 *
 *  Every button region is tested as a rectangle that covers the full
 *  inter-button space so there are no dead zones between buttons.
 *  This is critical for touchscreen use where finger precision is low.
 * ────────────────────────────────────────────────────────────────────────── */
static uint32_t keypad_mouse_to_key(struct ncplane *phone, const ncinput *ni) {
    int phone_y = 0;
    int phone_x = 0;
    unsigned phone_rows, phone_cols;

    ncplane_dim_yx(phone, &phone_rows, &phone_cols);
    keypad_layout layout = keypad_layout_compute((int)phone_rows, (int)phone_cols);

    ncplane_yx(phone, &phone_y, &phone_x);

    int y = (int)ni->y - phone_y;
    int x = (int)ni->x - phone_x;

    int pr = (int)phone_rows;
    int pc = (int)phone_cols;

    /* Ignore taps outside the keypad region entirely */
    if (y < layout.start_row || y >= pr || x < 0 || x >= pc) {
        return 0;
    }

    /* ── Soft keys: full-width halves of the softkey band ────────────── */
    /* Soft key band = from softkey_row up to (but not including) dpad_row */
    int sk_band_top = layout.softkey_row;
    int sk_band_bot = layout.dpad_row;
    int sk_band_h   = sk_band_bot - sk_band_top;
    if (sk_band_h < 1) sk_band_h = 1;

    if (y >= sk_band_top && y < sk_band_bot) {
        int half = pc / 2;
        if (x < half)  return 'q';  /* LSK — left half of screen  */
        else            return 'e';  /* RSK — right half of screen */
    }

    /* ── D-pad: 3-row band split into 5 zones ───────────────────────── */
    /*
     *   row dpad_row:       entire row = UP
     *   row dpad_row+1:     left third = LEFT, middle = OK/ENTER, right = RIGHT
     *   row dpad_row+2:     entire row = DOWN
     *
     *   The band extends from dpad_row to num_start_row.
     */
    int dpad_top = layout.dpad_row;
    int dpad_bot = layout.num_start_row;
    int dpad_h   = dpad_bot - dpad_top;
    if (dpad_h < 3) dpad_h = 3;

    if (y >= dpad_top && y < dpad_top + dpad_h) {
        int dpad_third = pc / 3;
        int rel_row = y - dpad_top;
        int up_rows = dpad_h / 3;
        if (up_rows < 1) up_rows = 1;
        int down_start = dpad_h - up_rows;

        if (rel_row < up_rows) {
            return NCKEY_UP;
        } else if (rel_row >= down_start) {
            return NCKEY_DOWN;
        } else {
            /* Middle band: LEFT / OK / RIGHT */
            if (x < dpad_third)          return NCKEY_LEFT;
            else if (x >= pc - dpad_third) return NCKEY_RIGHT;
            else                           return NCKEY_ENTER;
        }
    }

    /* ── Numeric keypad: 4×3 grid of rectangular regions ─────────────── */
    static const uint32_t num_keys[4][3] = {
        { '1', '2', '3' },
        { '4', '5', '6' },
        { '7', '8', '9' },
        { '*', '0', '#' },
    };

    int num_top = layout.num_start_row;
    int num_total_h = pr - num_top;
    if (num_total_h < 4) num_total_h = 4;

    if (y >= num_top && y < pr) {
        /* Divide the remaining height into 4 equal row bands */
        int row_h = num_total_h / 4;
        if (row_h < 1) row_h = 1;
        int col_w = pc / 3;
        if (col_w < 1) col_w = 1;

        int r = (y - num_top) / row_h;
        int c = x / col_w;
        if (r > 3) r = 3;
        if (c > 2) c = 2;
        return num_keys[r][c];
    }

    return 0;
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

    /* ── Phone plane (full height: screen + keypad) ──────────────────── */
    struct ncplane *phone = create_phone_plane(std);
    if (!phone) {
        notcurses_stop(nc);
        return 1;
    }

    /* ── Screen plane (child of phone, covers only the screen area) ─── */
    /*
     * All screen draw/input functions receive 'screen' instead of 'phone'.
     * They see a PHONE_SCREEN_ROWS × PHONE_COLS canvas — exactly the
     * display area above the keypad.  The keypad is drawn directly on the
     * parent 'phone' plane below the screen area.
     */
    struct ncplane *screen = create_screen_plane(phone);
    if (!screen) {
        ncplane_destroy(phone);
        notcurses_stop(nc);
        return 1;
    }

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

    /*
     * last_key tracks the most recently pressed key for keypad highlight
     * feedback.  Reset to 0 on each timeout (no key pressed) so the
     * highlight only flashes for one frame.
     */
    uint32_t last_key = 0;
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
         *   3. draw_keypad() on the phone plane — visual keypad below screen
         *   4. notcurses_render() — composite everything to terminal
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
                ghost_text(screen, 4, 3, COL_PLACEHOLDER, TEXT_COMING_SOON);
                ghost_text(screen, 6, 3, COL_HINT,        TEXT_GO_HOME);
                break;
        }

        /* ── KEYPAD (drawn on the parent phone plane) ────────────────── */
        draw_keypad(phone, last_key);

        /* ── DEBUG OVERLAY (drawn last, on top of everything) ────────── */
        debug_draw(phone);

        /* ── RENDER ──────────────────────────────────────────────────── */
        notcurses_render(nc);

        /* ── INPUT ───────────────────────────────────────────────────── */
        ncinput ni;
        struct timespec timeout = { .tv_sec = 0, .tv_nsec = 33000000L };
        uint32_t key = notcurses_get(nc, &timeout, &ni);

        if (key == 0) {
            last_key = 0;   /* no key → clear highlight */
            continue;
        }

        /* ── Record EVERY event for debug display (before any filtering) ── */
        debug_record_event(key, &ni);

        if (ni.evtype == NCTYPE_REPEAT) {
            continue;
        }

        /* ── Touchscreen / mouse: map tap position to a key code ─────── */
        /*
         * Accept NCKEY_BUTTON1 on PRESS (mouse click) or RELEASE (touch
         * lift — Linux evdev touchscreens often only report release).
         * Also accept any NCKEY_BUTTON* variant since some touch drivers
         * report different button IDs.
         */
        if (key == NCKEY_BUTTON1 || key == NCKEY_BUTTON2 || key == NCKEY_BUTTON3) {
            /*
             * Process only RELEASE for touchscreen taps.
             *
             * Why: many drivers emit both PRESS and RELEASE for one tap,
             * which can trigger actions twice (toggles appear "broken").
             * HyperPixel/evdev reliably provides RELEASE, so using it as
             * the single source of truth avoids double-dispatch.
             */
            if (ni.evtype != NCTYPE_RELEASE) {
                continue;
            }
            uint32_t mapped_key = keypad_mouse_to_key(phone, &ni);
            if (mapped_key == 0) {
                continue;
            }
            key = mapped_key;
            /* Fall through to normal key processing below */
        }

        /* Filter keyboard release events (NOT touch — those were handled above) */
        else if (ni.evtype == NCTYPE_RELEASE) {
            continue;
        }

        /* ── Numeric key → arrow key mapping (dumbphone navigation) ── */
        /*
         * On fbcon, arrow keys may not produce NCKEY_UP/DOWN/LEFT/RIGHT
         * if the terminal isn't properly configured.  Map numeric keys
         * as a fallback so the on-screen keypad always works:
         *   2 = UP,  8 = DOWN,  4 = LEFT,  6 = RIGHT,  5 = ENTER
         *
         * This ONLY applies when NOT on a text editing screen (notes edit
         * mode uses number keys for multi-tap text entry).
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

        /* Store for keypad visual highlight on next frame */
        last_key = key;

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
            if (!phone) {
                break;
            }

            screen = create_screen_plane(phone);
            if (!screen) {
                ncplane_destroy(phone);
                break;
            }

            last_key = 0;
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

    /* ── Cleanup — REVERSE order of creation ────────────────────────────── */
    /*
     * ncplane_destroy(phone) also destroys its child (screen plane).
     */
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
