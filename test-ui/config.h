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

#include <notcurses/notcurses.h>

/*
Nav bar
*/

#define COL_GHOST_LOW 0x7F1D1D
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

/* ─── Target panel: D200N2415V1 — 2.0" IPS, 480×360, ST7701S ──────────────
 *
 * The product display is 480×360 (4:3) used in LANDSCAPE orientation.
 * Mapping pixels → character grid with a 12×24 px monospace cell:
 *     480 / 12 = 40 columns
 *     360 / 24 = 15 rows
 * A 12×24 cell is a 1:2 (w:h) ratio, which matches a typical terminal cell,
 * so the host preview keeps the panel's real 4:3 proportions.
 * Configure on the Pi with (e.g. /etc/default/console-setup):
 *     FONTFACE="Iosevka Term"
 *     FONTSIZE="12x24"
 *
 * The skeleton fills these 18 rows:
 *     row 0       status strip
 *     row 1       divider (carries the screen tag)
 *     rows 2..15  content (menu list, player, dialer, detail view)
 *     rows 16..17 footer (theme footer, or action cells on decision screens)
 *
 * The display is split into two zones:
 *   PHONE_SCREEN_ROWS  — the "display" (status, content, footer)
 *   KEYPAD_ROWS        — optional visual on-screen keypad (disabled here)
 */

/* Width of the phone plane in character cells (480 px / 12) */
#define PHONE_COLS 40

/* Height of the screen portion in character cells (360 px / 24) */
#define PHONE_SCREEN_ROWS 15

/* Keypad has been removed from the UI-focused layout */
#define KEYPAD_ROWS 0

/* Total height of the combined phone plane */
#define PHONE_ROWS PHONE_SCREEN_ROWS

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
#define KEYPAD_START_ROW PHONE_SCREEN_ROWS
#define KEYPAD_SOFTKEY_ROW (KEYPAD_START_ROW + 1)
#define KEYPAD_DPAD_ROW (KEYPAD_START_ROW + 3)
#define KEYPAD_NUM_ROW (KEYPAD_START_ROW + 7)

/* ═══════════════════════════════════════════════════════════════════════════
 *  APPEARANCE CONSTANTS IN ACTIVE USE
 * ═══════════════════════════════════════════════════════════════════════════ */

#define COL_PLACEHOLDER 0x555555 /* "Coming soon..." text */
#define COL_HINT 0xa5a58d        /* "[h] go Home" hint */

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
#define CONTENT_COL 2       /* left margin for content */
#define FOOTER_ROW_OFFSET 2 /* footer is rows - this */
#define INNER_WIDTH(cols) ((int)(cols) - 2 * CONTENT_COL)

/* Shared popup layout styling */
#define UI_POPUP_MIN_TOP 3
#define UI_POPUP_MIN_LEFT 1
#define UI_POPUP_TEXT_INSET_X 2
#define UI_POPUP_TITLE_ROW_OFFSET 1
#define UI_POPUP_INPUT_ROW_OFFSET 3
#define UI_POPUP_HINT_ROW_OFFSET 5

/* Screen-specific popup dimensions */
#define SETTINGS_PIN_POPUP_WIDTH 28
#define SETTINGS_PIN_POPUP_HEIGHT 7
#define HOME_PIN_POPUP_WIDTH 26
#define HOME_PIN_POPUP_HEIGHT 6
#define NOTES_SAVE_POPUP_HEIGHT 7
#define VOICE_MEMO_NAME_POPUP_HEIGHT 7
#define ALARM_TIME_POPUP_WIDTH 32
#define ALARM_TIME_POPUP_HEIGHT 7
#define ALARM_TIME_POPUP_MIN_LEFT 2
#define ALARM_RING_POPUP_WIDTH 34
#define ALARM_RING_POPUP_HEIGHT 8

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
#define MENU_CURSOR ">"

/*
 * MENU_CURSOR_BLANK - Spaces to align unselected items
 *
 * Must be the same visual width as MENU_CURSOR.
 * Two spaces = same width as "▸ "
 */
#define MENU_CURSOR_BLANK ""

/* ═══════════════════════════════════════════════════════════════════════════
 *  CONTROL BINDINGS
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Canonical logical softkey action codes (synthetic — not printable chars). */
#define KEY_SOFT_LEFT_ACTION  UINT32_C(0x00200001)
#define KEY_SOFT_RIGHT_ACTION UINT32_C(0x00200002)

/* Primary soft key bindings: Q = back/cancel, E = open/new/confirm. */
#define KEY_BIND_SOFT_LEFT  'q'
#define KEY_BIND_SOFT_RIGHT 'e'

/* Alternate soft key bindings for hardware keypads without Q/E keys. */
#define KEY_BIND_SOFT_LEFT_ALT_1 NCKEY_HOME
#define KEY_BIND_SOFT_LEFT_ALT_2 NCKEY_F07
#define KEY_BIND_SOFT_RIGHT_ALT_1 NCKEY_PGUP
#define KEY_BIND_SOFT_RIGHT_ALT_2 NCKEY_F09

/* Navigation/action bindings (arrow keys pass through). */
#define KEY_BIND_UP    NCKEY_UP
#define KEY_BIND_DOWN  NCKEY_DOWN
#define KEY_BIND_LEFT  NCKEY_LEFT
#define KEY_BIND_RIGHT NCKEY_RIGHT
#define KEY_BIND_SELECT NCKEY_ENTER

/* WASD navigation keys — mapped to arrows/enter outside text-entry modes.
 * W=up  S=down  A=primary-action(enter)  D=secondary-action(left/delete) */
#define KEY_BIND_WASD_UP     'w'
#define KEY_BIND_WASD_DOWN   's'
#define KEY_BIND_WASD_ACTION 'a'
#define KEY_BIND_WASD_DELETE 'd'

/* App-level quit binding (only active from HOME). */
#define KEY_BIND_APP_QUIT 'x'

/* ═══════════════════════════════════════════════════════════════════════════
 *  STORAGE PATHS
 * ═══════════════════════════════════════════════════════════════════════════ */

#define APP_PATH_SETTINGS_FILE "./settings.conf"
#define APP_PATH_NOTES_DIR "./Notes"
#define APP_PATH_CONTACTS_DIR "./Contacts"
#define APP_PATH_ALARMS_DIR "./Alarms"
#define APP_PATH_VOICE_MEMOS_DIR "./VoiceMemos"
#define APP_PATH_MUSIC_DIR "./Music"

/* ═══════════════════════════════════════════════════════════════════════════
 *  SETTINGS OPTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SETTINGS_KEY_NIGHT_MODE "night_mode"
#define SETTINGS_KEY_BLUETOOTH "bluetooth"
#define SETTINGS_KEY_HAND_WHITE "hand_white"
#define SETTINGS_KEY_AUX_INPUT "aux_input"
#define SETTINGS_KEY_LIGHT_THEME "light_theme"
#define SETTINGS_KEY_VOLUME "volume"
#define SETTINGS_KEY_BRIGHTNESS "brightness"
#define SETTINGS_KEY_TIMEOUT_SEC "timeout_sec"

#define SETTINGS_LABEL_NIGHT_MODE "Night Mode"
#define SETTINGS_LABEL_BLUETOOTH "Bluetooth"
#define SETTINGS_LABEL_HAND_WHITE "Hand White"
#define SETTINGS_LABEL_AUX_INPUT "Aux Input"

#define SETTINGS_DEFAULT_NIGHT_MODE 0
#define SETTINGS_DEFAULT_BLUETOOTH 0
#define SETTINGS_DEFAULT_HAND_WHITE 0
#define SETTINGS_DEFAULT_AUX_INPUT 1

#define SETTINGS_DEFAULT_THEME_INDEX UI_DEFAULT_THEME_INDEX
#define SETTINGS_DEFAULT_VOLUME 7
#define SETTINGS_MIN_VOLUME 0
#define SETTINGS_MAX_VOLUME 10

#define SETTINGS_DEFAULT_BRIGHTNESS 8
#define SETTINGS_MIN_BRIGHTNESS 1
#define SETTINGS_MAX_BRIGHTNESS 10

#define SETTINGS_DEFAULT_TIMEOUT_SEC 60
#define SETTINGS_MIN_TIMEOUT_SEC 15
#define SETTINGS_MAX_TIMEOUT_SEC 600

/* Settings screen labels */
#define SETTINGS_MAIN_ITEM_APPEARANCE "APPEARANCE"
#define SETTINGS_MAIN_ITEM_SECURITY "SECURITY"
#define SETTINGS_MAIN_ITEM_CONNECTIVITY "CONNECTIVITY"
#define SETTINGS_MAIN_ITEM_SYSTEM_INFO "SYSTEM INFO"
#define SETTINGS_MAIN_ITEM_COUNT 4

/* ═══════════════════════════════════════════════════════════════════════════
 *  ALARM OPTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

#define ALARM_MAX_COUNT 20
#define ALARM_SNOOZE_MIN 5

/* ═══════════════════════════════════════════════════════════════════════════
 *  APPEARANCE SYSTEM (THEMES)
 * ═══════════════════════════════════════════════════════════════════════════ */

#define UI_THEME_COUNT 12
#define UI_DEFAULT_THEME_INDEX 0

/* Theme labels shown in Settings -> Appearance -> Themes.
 * The authoritative token table lives in services/theme_service.c; these
 * labels mirror it for the settings list. */
#define UI_THEME_LABEL_0  "Amber Ledger"
#define UI_THEME_LABEL_1  "Phosphor Index"
#define UI_THEME_LABEL_2  "Redline Mono"
#define UI_THEME_LABEL_3  "Vale Signal"
#define UI_THEME_LABEL_4  "Rushnyk"
#define UI_THEME_LABEL_5  "Thermal Index"
#define UI_THEME_LABEL_6  "Instrument"
#define UI_THEME_LABEL_7  "Boot Rite"
#define UI_THEME_LABEL_8  "Blueprint"
#define UI_THEME_LABEL_9  "Dossier"
#define UI_THEME_LABEL_10 "One-Bit"
#define UI_THEME_LABEL_11 "Polar Night"

/* Light mode palettes */
#define UI_THEME_LIGHT_0_BG 0xffffff
#define UI_THEME_LIGHT_0_TEXT_PRIMARY 0x000000
#define UI_THEME_LIGHT_0_TEXT_MUTED 0x444444
#define UI_THEME_LIGHT_0_BORDER 0x000000
#define UI_THEME_LIGHT_0_SELECTION_BG 0x000000
#define UI_THEME_LIGHT_0_SELECTION_TEXT 0xffffff
#define UI_THEME_LIGHT_0_RULE "-"

#define UI_THEME_LIGHT_1_BG 0xe8f4fa
#define UI_THEME_LIGHT_1_TEXT_PRIMARY 0x0a1e2e
#define UI_THEME_LIGHT_1_TEXT_MUTED 0x3d6b82
#define UI_THEME_LIGHT_1_BORDER 0x00b4d8
#define UI_THEME_LIGHT_1_SELECTION_BG 0x0096c7
#define UI_THEME_LIGHT_1_SELECTION_TEXT 0xffffff
#define UI_THEME_LIGHT_1_RULE "~"

#define UI_THEME_LIGHT_2_BG 0xeaeff5
#define UI_THEME_LIGHT_2_TEXT_PRIMARY 0x151d2b
#define UI_THEME_LIGHT_2_TEXT_MUTED 0x546a87
#define UI_THEME_LIGHT_2_BORDER 0x3a6ea5
#define UI_THEME_LIGHT_2_SELECTION_BG 0x2c5f8a
#define UI_THEME_LIGHT_2_SELECTION_TEXT 0xf0f5ff
#define UI_THEME_LIGHT_2_RULE "-"

#define UI_THEME_LIGHT_3_BG 0xfff0e0
#define UI_THEME_LIGHT_3_TEXT_PRIMARY 0x2d1a08
#define UI_THEME_LIGHT_3_TEXT_MUTED 0x8f5c2a
#define UI_THEME_LIGHT_3_BORDER 0xd4700a
#define UI_THEME_LIGHT_3_SELECTION_BG 0xe87a12
#define UI_THEME_LIGHT_3_SELECTION_TEXT 0xfff8f0
#define UI_THEME_LIGHT_3_RULE "#"

#define UI_THEME_LIGHT_4_BG 0xf0f0f0
#define UI_THEME_LIGHT_4_TEXT_PRIMARY 0x121212
#define UI_THEME_LIGHT_4_TEXT_MUTED 0x606060
#define UI_THEME_LIGHT_4_BORDER 0x404040
#define UI_THEME_LIGHT_4_SELECTION_BG 0x2a2a2a
#define UI_THEME_LIGHT_4_SELECTION_TEXT 0xf0f0f0
#define UI_THEME_LIGHT_4_RULE "."

/* Dark mode palettes */
#define UI_THEME_DARK_0_BG 0x000000
#define UI_THEME_DARK_0_TEXT_PRIMARY 0xffffff
#define UI_THEME_DARK_0_TEXT_MUTED 0xb0b0b0
#define UI_THEME_DARK_0_BORDER 0xffffff
#define UI_THEME_DARK_0_SELECTION_BG 0xffffff
#define UI_THEME_DARK_0_SELECTION_TEXT 0x000000
#define UI_THEME_DARK_0_RULE "-"

#define UI_THEME_DARK_1_BG 0x060e18
#define UI_THEME_DARK_1_TEXT_PRIMARY 0xc8f0ff
#define UI_THEME_DARK_1_TEXT_MUTED 0x4da8c4
#define UI_THEME_DARK_1_BORDER 0x00a0c4
#define UI_THEME_DARK_1_SELECTION_BG 0x0d5a78
#define UI_THEME_DARK_1_SELECTION_TEXT 0xe0faff
#define UI_THEME_DARK_1_RULE "~"

#define UI_THEME_DARK_2_BG 0x0a1020
#define UI_THEME_DARK_2_TEXT_PRIMARY 0xd0dae8
#define UI_THEME_DARK_2_TEXT_MUTED 0x7090b0
#define UI_THEME_DARK_2_BORDER 0x3870a0
#define UI_THEME_DARK_2_SELECTION_BG 0x1e3f60
#define UI_THEME_DARK_2_SELECTION_TEXT 0xe0ebf5
#define UI_THEME_DARK_2_RULE "-"

#define UI_THEME_DARK_3_BG 0x160e04
#define UI_THEME_DARK_3_TEXT_PRIMARY 0xffd8a8
#define UI_THEME_DARK_3_TEXT_MUTED 0xc08040
#define UI_THEME_DARK_3_BORDER 0xb85e10
#define UI_THEME_DARK_3_SELECTION_BG 0x6b3810
#define UI_THEME_DARK_3_SELECTION_TEXT 0xffecd0
#define UI_THEME_DARK_3_RULE "#"

#define UI_THEME_DARK_4_BG 0x0c0c0c
#define UI_THEME_DARK_4_TEXT_PRIMARY 0xd8d8d8
#define UI_THEME_DARK_4_TEXT_MUTED 0x787878
#define UI_THEME_DARK_4_BORDER 0x505050
#define UI_THEME_DARK_4_SELECTION_BG 0x333333
#define UI_THEME_DARK_4_SELECTION_TEXT 0xe8e8e8
#define UI_THEME_DARK_4_RULE "."

#endif /* BLACKHAND_CONFIG_H */
