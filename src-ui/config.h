/*
 * ============================================================================
 *  config.h — Central Configuration File
 * ============================================================================
 *
 *  WHAT IS THIS FILE?
 *  ------------------
 *  This file contains ALL configurable values for the BlackHand UI.
 *  By putting everything in one place, you can easily:
 *    - Change the color theme
 *    - Resize the phone display
 *    - Adjust layout positions
 *    - Translate text labels
 *
 *  Just edit values here and recompile - no need to hunt through code!
 *
 *
 *  C CONCEPTS DEMONSTRATED:
 *  ------------------------
 *  1. #define preprocessor directives - creating constants
 *  2. Hexadecimal numbers - 0xRRGGBB color format
 *  3. String literals - text in double quotes
 *  4. Header guards - preventing double inclusion
 *
 *
 *  HOW TO USE THIS FILE:
 *  ---------------------
 *  1. Find the value you want to change
 *  2. Edit it
 *  3. Save and recompile: cd build && make
 *  4. Run the app: ./blackhand-ui
 *
 *
 *  C CONCEPT: #define MACROS
 *  -------------------------
 *  #define NAME VALUE creates a "macro" - the preprocessor replaces
 *  every occurrence of NAME with VALUE before compilation.
 *
 *  EXAMPLE:
 *    #define PI 3.14159
 *    float circumference = 2 * PI * radius;
 *
 *  After preprocessing becomes:
 *    float circumference = 2 * 3.14159 * radius;
 *
 *  ADVANTAGES OF #define:
 *    - No memory used (replaced at compile time)
 *    - Can be used for any text replacement
 *    - Works with any data type
 *
 *  DISADVANTAGES:
 *    - No type checking (it's just text replacement)
 *    - Can cause unexpected behavior with complex expressions
 *    - Debugging shows the value, not the name
 *
 *  ALTERNATIVE: const int
 *    const int PHONE_COLS = 50;  // Type-checked, debugger-friendly
 *
 *  We use #define here because it's the traditional way for config values
 *  and works well with the preprocessor.
 *
 * ============================================================================
 */

#ifndef BLACKHAND_CONFIG_H
#define BLACKHAND_CONFIG_H

/*
Nav bar
*/

#define COL_GHOST_ON 0xE0E0E0
#define COL_GHOST_OFF 0x1E1E1E
#define COL_GHOST_PCT 0x2C2C2C
#define COL_GHOST_LOW 0x7F1D1D
#define STATUS_BATTERY_PCT_COL (STATUS_BATTERY_COL + 5)
/* ═══════════════════════════════════════════════════════════════════════════
 *  PHONE DIMENSIONS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  These define the size of the "phone" display in terminal characters.
 *
 *  TERMINAL CHARACTER SIZES:
 *    - Most terminals use characters about twice as tall as wide
 *    - A 50x15 phone is roughly square visually
 *    - Increase for more content area, decrease for smaller footprint
 *
 *  MINIMUM SIZES:
 *    - PHONE_COLS should be at least 30 for menu items + border
 *    - PHONE_ROWS should be at least 10 for status bar + content + footer
 */

/* ─── HyperPixel 4.0 Display: 400×800 pixels ──────────────────────────── */
/*
 * At a monospace font of ~10×20 pixels per cell (e.g. Iosevka 10pt):
 *   400 / 10 = 40 columns
 *   800 / 20 = 40 rows
 *
 * The display is split into two zones:
 *   PHONE_SCREEN_ROWS  — the "display" (status, content, softkey footer)
 *   KEYPAD_ROWS         — visual on-screen keypad below the screen
 *
 * Total plane height = PHONE_SCREEN_ROWS + KEYPAD_ROWS
 */

/* Width of the phone plane in terminal columns */
#define PHONE_COLS 36

/* Height of the screen portion (border + status + content + footer) */
#define PHONE_SCREEN_ROWS 20

/* Height of the on-screen keypad area below the screen */
#define KEYPAD_ROWS 16

/* Total height of the combined phone plane */
#define PHONE_ROWS (PHONE_SCREEN_ROWS + KEYPAD_ROWS)

/* ─── Keypad Layout Constants ──────────────────────────────────────────── */
/*
 *  Keypad region spans rows PHONE_SCREEN_ROWS .. PHONE_ROWS-1 on the plane.
 *
 *  Visual layout (row offsets relative to PHONE_SCREEN_ROWS):
 *
 *    +0   ┌─ separator ─────────────────────────────────┐
 *    +1   │  [LSK]                            [RSK]     │
 *    +2   │                                             │
 *    +3   │              ▲                              │
 *    +4   │          ◀  [OK]  ▶                         │
 *    +5   │              ▼                              │
 *    +6   │                                             │
 *    +7   │         [ 1 ] [ 2 ] [ 3 ]                   │
 *    +8   │                                             │
 *    +9   │         [ 4 ] [ 5 ] [ 6 ]                   │
 *    +10  │                                             │
 *    +11  │         [ 7 ] [ 8 ] [ 9 ]                   │
 *    +12  │                                             │
 *    +13  │         [ * ] [ 0 ] [ # ]                   │
 *    +14  │                                             │
 *    +15  └─────────────────────────────────────────────┘
 */
#define KEYPAD_START_ROW    PHONE_SCREEN_ROWS
#define KEYPAD_SOFTKEY_ROW  (KEYPAD_START_ROW + 1)
#define KEYPAD_DPAD_ROW     (KEYPAD_START_ROW + 3)
#define KEYPAD_NUM_ROW      (KEYPAD_START_ROW + 7)

/* ═══════════════════════════════════════════════════════════════════════════
 *  COLOR PALETTE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  All colors are 24-bit RGB values in hexadecimal format: 0xRRGGBB
 *
 *  C CONCEPT: HEXADECIMAL NUMBERS
 *  ------------------------------
 *  Hexadecimal (base 16) uses digits 0-9 and letters A-F.
 *  Each hex digit represents 4 bits (0-15).
 *
 *  COLOR FORMAT: 0xRRGGBB
 *    - RR = Red component (00-FF, which is 0-255 in decimal)
 *    - GG = Green component (00-FF)
 *    - BB = Blue component (00-FF)
 *
 *  EXAMPLES:
 *    0xFF0000 = Red   (full red, no green, no blue)
 *    0x00FF00 = Green (no red, full green, no blue)
 *    0x0000FF = Blue  (no red, no green, full blue)
 *    0xFFFFFF = White (all colors at maximum)
 *    0x000000 = Black (all colors at zero)
 *    0x808080 = Grey  (all colors at middle)
 *
 *  TIPS FOR CHOOSING COLORS:
 *    - Use a color picker tool (many available online)
 *    - Keep contrast in mind (text vs background)
 *    - Consider color-blind users
 *    - Test on different terminals (colors may vary)
 */

/* ─── Background ───────────────────────────────────────────────────────── */

/*
 * COL_BG - Main Background Color
 *
 * Used for: Phone interior, behind all content
 * Current: Very dark grey (almost black)
 * Try: 0x000000 (pure black), 0x1a1a2e (dark blue), 0x0d1117 (GitHub dark)
 */
#define COL_BG 0x1a1a2e

/* ─── Border / Frame ───────────────────────────────────────────────────── */

/*
 * COL_BORDER - Box Border Color
 *
 * Used for: The heavy box-drawing characters around the phone edge
 * Current: Muted sage green
 * Try: 0x5c6370 (grey), 0x61afef (blue), 0x98c379 (green)
 */
#define COL_BORDER 0xE0E0E0

/*
 * COL_SEPARATOR - Internal Separator Lines
 *
 * Used for: Horizontal lines below status bar and above footer
 * Current: Medium grey (subtle)
 * Try: 0x333333 (darker), 0x777777 (lighter)
 */
#define COL_SEPARATOR 0xE0E0E0

/* ─── Status Bar (Battery & Signal) ────────────────────────────────────── */

/*
 * COL_STATUS_TEXT - General Status Bar Text
 *
 * Used for: Any non-colored text in the status bar
 * Current: Light grey
 */
#define COL_STATUS_TEXT 0x888888

/*
 * Battery Colors - Change based on charge level
 *
 * COL_BATTERY_GOOD - Above 50%
 * COL_BATTERY_MED  - 20% to 50%
 * COL_BATTERY_LOW  - Below 20%
 *
 * The draw_battery() function in main.c uses these thresholds.
 */
#define COL_BATTERY_GOOD 0x7ec850 /* Green - healthy */
#define COL_BATTERY_MED 0xf0c040  /* Yellow/orange - warning */
#define COL_BATTERY_LOW 0xe05040  /* Red - critical */

/*
 * Signal Colors
 *
 * COL_SIGNAL_ON  - Bars that represent actual signal
 * COL_SIGNAL_OFF - Bars above the signal level (empty)
 */
#define COL_SIGNAL_ON 0x7ec850  /* Green - has signal */
#define COL_SIGNAL_OFF 0x555555 /* Grey - no signal */

/* ─── Header ───────────────────────────────────────────────────────────── */

/*
 * COL_HEADER_TEXT - Header Text Color
 *
 * Used for: Title text, brand text in the header area
 * Current: Warm off-white
 */
#define COL_HEADER_TEXT 0xd8dad3

/* ─── Footer ───────────────────────────────────────────────────────────── */

/*
 * COL_FOOTER_TEXT - Footer Hint Text
 *
 * Used for: "[q]Quit" and other footer hints
 * Current: Warm tan/beige
 */
#define COL_FOOTER_TEXT 0xa5a58d

/* ─── Menu (Home Screen) ───────────────────────────────────────────────── */

/*
 * COL_MENU_NORMAL - Unselected Menu Items
 *
 * Current: Soft pink/mauve
 * Try: 0xabb2bf (grey), 0xe5c07b (gold)
 */
#define COL_MENU_NORMAL 0xc9ada7

/*
 * COL_MENU_SELECTED - Currently Selected Menu Item
 *
 * Current: Bright white (stands out from normal items)
 * Try: 0x61afef (blue), 0x98c379 (green), 0xe06c75 (red)
 */
#define COL_MENU_SELECTED 0xffffff

/* ─── Placeholder Screens ──────────────────────────────────────────────── */

/*
 * Colors for unimplemented screens showing "Coming soon..."
 */
#define COL_PLACEHOLDER 0x555555 /* "Coming soon..." text */
#define COL_HINT 0xa5a58d        /* "[h] go Home" hint */

/* ─── Settings Screen ──────────────────────────────────────────────────── */

#define COL_SETTINGS_HEADER 0xf2e9e4 /* "Settings" heading */
#define COL_SETTINGS_TEXT 0xc9ada7   /* Setting item text */

/* ─── Dev Mode Label ───────────────────────────────────────────────────── */

/*
 * COL_DEV_LABEL - Development Mode Indicator
 *
 * Used for: "[ Dev — 50x15 phone screen ]" text outside the phone
 * Current: Very dim grey (barely visible)
 */
#define COL_DEV_LABEL 0x444444

/* ═══════════════════════════════════════════════════════════════════════════
 *  LAYOUT CONSTANTS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  These define WHERE things are positioned on the screen.
 *  Positions are in terminal cells (row, column) starting from 0.
 *
 *  COORDINATE SYSTEM:
 *    Row 0 = top of the plane
 *    Col 0 = left of the plane
 *    Row increases going DOWN
 *    Col increases going RIGHT
 */

/* ─── Frame Layout ─────────────────────────────────────────────────────── */

/*
 * FRAME_MIN_* - Minimum size to attempt drawing
 *
 * If the plane is smaller than this, we skip drawing to avoid garbled output.
 */
#define FRAME_MIN_ROWS 3
#define FRAME_MIN_COLS 10

/* ─── Status Bar Layout ────────────────────────────────────────────────── */

/*
 * STATUS_ROW - Row where battery and signal are drawn
 *
 * Row 0 is the top border, so row 1 is the first interior row.
 * The status bar is drawn here, with a separator line at row 2.
 */
#define STATUS_ROW 1

/*
 * STATUS_BATTERY_COL - Column where battery icon starts
 * STATUS_SIGNAL_COL  - Column where signal icon starts
 *
 * Battery is on the left (col 2, leaving room for border).
 * Signal is right-anchored dynamically in draw_signal() (cols - 6).
 */
#define STATUS_BATTERY_COL 2

/* ─── Home Screen Layout ───────────────────────────────────────────────── */

/*
 * HOME_CONTENT_START_ROW - First row of menu items
 *
 * Row 0 = border
 * Row 1 = status bar
 * Row 2 = separator
 * Row 3 = first menu item (START_ROW = 3)
 */
#define HOME_CONTENT_START_ROW 3

/*
 * HOME_CONTENT_COL - Left column for menu items
 *
 * Column 0 is the border, so content starts at column 2.
 */
#define HOME_CONTENT_COL 2

/*
 * HOME_ROW_SPACING - Rows between menu items
 *
 * 1 = items on consecutive rows (dense)
 * 2 = one empty row between items (spacious)
 */
#define HOME_ROW_SPACING 1

/*
 * HOME_MIN_* - Minimum size to draw the menu
 *
 * Below this, we show "Too small" message instead.
 */
#define HOME_MIN_ROWS 6
#define HOME_MIN_COLS 20

/* ─── Settings Screen Layout ───────────────────────────────────────────── */

#define SETTINGS_HEADER_ROW 3  /* "Settings" heading row */
#define SETTINGS_CONTENT_COL 3 /* Left column for content */
#define SETTINGS_FIRST_ROW 5   /* First setting item row */
#define SETTINGS_MIN_ROWS 6
#define SETTINGS_MIN_COLS 20

/* ─── Unified Layout Constants ─────────────────────────────────────────── */
#define CONTENT_START_ROW 3 /* first row below separator */
#define CONTENT_COL 3       /* left margin for content */
#define FOOTER_ROW_OFFSET 2 /* footer is rows - this */
#define INNER_WIDTH(cols) ((int)(cols) - 2 * CONTENT_COL)

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEXT LABELS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  All user-visible text strings in one place.
 *  To translate the UI, change these strings.
 *
 *  C CONCEPT: STRING LITERALS
 *  --------------------------
 *  Text in "double quotes" creates a string literal.
 *  The compiler stores it in read-only memory and adds a '\0' at the end.
 *
 *  "Hello" is stored as: ['H', 'e', 'l', 'l', 'o', '\0']
 *
 *  UNICODE/SPECIAL CHARACTERS:
 *  - \u25b8 is the Unicode escape for ▸ (right triangle)
 *  - You can use actual Unicode characters if your editor supports them
 *  - The terminal must support Unicode to display them
 */

/*
 * TEXT_BRAND - App name/logo in the header
 */
#define TEXT_BRAND " BH-OPS "

/*
 * TEXT_FOOTER - Help hint in the footer
 */
#define TEXT_FOOTER " [q]Quit "

/*
 * TEXT_DEV_LABEL - Development mode indicator (outside phone)
 */
#define TEXT_DEV_LABEL "[ Dev - compact ops layout ]"

/*
 * TEXT_COMING_SOON - Placeholder for unimplemented screens
 */
#define TEXT_COMING_SOON "Coming soon..."

/*
 * TEXT_GO_HOME - Hint for returning to home screen
 */
#define TEXT_GO_HOME "[h] go Home"

/*
 * TEXT_TOO_SMALL - Error when terminal is too small
 */
#define TEXT_TOO_SMALL "Too small"
#define TEXT_SCREEN_TOO_SMALL "Screen too small"

/* ═══════════════════════════════════════════════════════════════════════════
 *  MENU CURSOR
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * MENU_CURSOR - Symbol shown next to selected menu item
 *
 * \u25b8 is Unicode for ▸ (right-pointing triangle)
 * The space after it provides padding before the label.
 *
 * ALTERNATIVES:
 *   ">"         - Simple ASCII arrow
 *   "> "        - Arrow with space
 *   "* "        - Asterisk
 *   "→ "        - Unicode arrow (U+2192)
 *   "● "        - Bullet point (U+25CF)
 *   "\u25b6 "   - Larger triangle (U+25B6)
 */
#define MENU_CURSOR "▶ "

/*
 * MENU_CURSOR_BLANK - Spaces to align unselected items
 *
 * Must be the same visual width as MENU_CURSOR.
 * Two spaces = same width as "▸ "
 */
#define MENU_CURSOR_BLANK "  "

#endif /* BLACKHAND_CONFIG_H */
