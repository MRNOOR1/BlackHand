/*
 * ============================================================================
 *  screen_home.c — Home Screen with Main Menu
 * ============================================================================
 *
 *  WHAT IS THIS FILE?
 *  ------------------
 *  This file implements the home screen - the main menu that users see
 *  when they start the app. It displays a list of menu items (Calls,
 *  Messages, Settings, etc.) and allows navigation with arrow keys.
 *
 *  This file demonstrates:
 *    - Creating a data-driven menu system
 *    - Handling keyboard input
 *    - Maintaining state (which item is selected)
 *    - Drawing dynamic content based on state
 *
 *
 *  C CONCEPTS YOU'LL LEARN:
 *  ------------------------
 *  1. typedef struct - Creating custom data types
 *  2. Static variables - Keeping state between function calls
 *  3. Arrays - Storing multiple items of the same type
 *  4. sizeof operator - Getting the size of data
 *  5. const keyword - Marking data as read-only
 *
 *
 *  NOTCURSES CONCEPTS:
 *  -------------------
 *  1. Drawing text at specific positions
 *  2. Setting foreground/background colors
 *  3. Handling special keys (arrows, enter)
 *
 *
 *  HOW TO MODIFY:
 *  --------------
 *  - Add a menu item: Add an entry to the 'items' array below
 *  - Change colors: Edit COL_MENU_* in config.h
 *  - Change cursor symbol: Edit MENU_CURSOR in config.h
 *  - Change layout: Edit HOME_* constants in config.h
 *
 * ============================================================================
 */

/* ═══════════════════════════════════════════════════════════════════════════
 *  HEADER INCLUDES
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * <notcurses/notcurses.h> - Notcurses Library
 *
 * We need this for:
 *   - struct ncplane (the drawing surface type)
 *   - ncplane_* functions (drawing, colors)
 *   - NCKEY_* constants (special key codes like arrow keys)
 */
#include <notcurses/notcurses.h>
#include <time.h>
#include <stdio.h>

/*
 * <stdint.h> - Fixed-width Integer Types
 *
 * We need uint32_t for:
 *   - Key codes returned by Notcurses
 *   - Color values (0xRRGGBB format)
 */
#include <stdint.h>

/*
 * "ui.h" - Our UI Header
 *
 * We need this for:
 *   - screen_id enum (SCREEN_HOME, SCREEN_SETTINGS, etc.)
 *   - Function declarations (so main.c can call our functions)
 */
#include "ui.h"

/*
 * "config.h" - Configuration Constants
 *
 * We need this for:
 *   - COL_MENU_NORMAL, COL_MENU_SELECTED (menu colors)
 *   - HOME_CONTENT_START_ROW, HOME_CONTENT_COL (layout)
 *   - MENU_CURSOR, MENU_CURSOR_BLANK (selection indicator)
 */
#include "config.h"
#include "draw_utils.h"
#include "services/theme_service.h"
#include "services/mp3_service.h"
#include "services/settings_service.h"
#include "services/pin_service.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  C CONCEPT: STRUCTS
 *  ------------------
 *  A struct (structure) groups related variables together into a single unit.
 *  It's like a simple class in object-oriented languages, but without methods.
 *
 *  SYNTAX:
 *    struct person {
 *        char *name;     // A string (pointer to characters)
 *        int age;        // An integer
 *    };
 *
 *    struct person alice = { .name = "Alice", .age = 30 };
 *    printf("%s is %d years old\n", alice.name, alice.age);
 *
 *
 *  C CONCEPT: TYPEDEF
 *  ------------------
 *  'typedef' creates an alias (new name) for a type. It makes code cleaner:
 *
 *  WITHOUT typedef:
 *    struct menu_item my_item;   // Must say "struct" every time
 *
 *  WITH typedef:
 *    typedef struct { ... } menu_item;
 *    menu_item my_item;          // Cleaner, no "struct" needed
 */

/*
 * menu_item - Represents one entry in the menu
 *
 * FIELDS:
 *   label  - The text displayed to the user (e.g., "Calls")
 *   target - The screen to navigate to when selected
 *
 * EXAMPLE:
 *   menu_item calls = { .label = "Calls", .target = SCREEN_CALLS };
 */
typedef struct
{
    const char *label; /* Text shown in menu (pointer to string literal) */
    screen_id target;  /* Screen to go to when selected (enum value) */
} menu_item;

/* ═══════════════════════════════════════════════════════════════════════════
 *  STATIC DATA (Menu Items and Selection State)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  C CONCEPT: STATIC VARIABLES
 *  ---------------------------
 *  Variables declared 'static' at file scope have two properties:
 *    1. They persist between function calls (not reset each time)
 *    2. They're only visible within this file (private to screen_home.c)
 *
 *  This is how we maintain STATE without global variables.
 *  The 'selected' variable remembers which item is highlighted even
 *  after screen_home_draw() returns.
 *
 *
 *  C CONCEPT: CONST
 *  ----------------
 *  'const' means "read-only" - the compiler prevents modifications.
 *
 *  EXAMPLES:
 *    const int x = 5;    // x cannot be changed
 *    x = 10;             // ERROR: assignment of read-only variable
 *
 *    const char *s = "Hello";  // Pointer to constant data (can't modify string)
 *    s = "World";              // OK: can change what s points to
 *    s[0] = 'X';               // ERROR: can't modify the string content
 *
 *  WHY USE CONST?
 *    - Prevents accidental modification
 *    - Allows compiler optimizations
 *    - Documents intent ("this shouldn't change")
 */

/*
 * items - The menu entries
 *
 * This is a static const array - it's:
 *   - static: only accessible in this file
 *   - const: the data cannot be modified
 *   - array: multiple menu_item structs in a row
 *
 * ARRAY INITIALIZATION:
 *   { item1, item2, item3 }
 *
 * Each item is initialized with { .field = value } syntax.
 *
 * HOW TO ADD A MENU ITEM:
 *   1. Add the screen_id to the enum in ui.h (e.g., SCREEN_CALCULATOR)
 *   2. Add an entry here: { "Calculator", SCREEN_CALCULATOR }
 *   3. Implement screen_calculator_draw() in a new file
 *   4. Add the case to the switch in main.c
 */
static const menu_item items[] = {
    {"CALLS",      SCREEN_CALLS},
    {"MESSAGES",   SCREEN_MESSAGES},
    {"CONTACTS",   SCREEN_CONTACTS},
    {"MUSIC",      SCREEN_MP3},
    {"VOICE MEMO", SCREEN_VOICE_MEMO},
    {"NOTES",      SCREEN_NOTES},
    {"ALARM",      SCREEN_ALARM},
    {"GPS",        SCREEN_GPS},
    {"SETTINGS",   SCREEN_SETTINGS},
};

/*
 * item_count - Number of items in the menu
 *
 * C CONCEPT: sizeof OPERATOR
 * --------------------------
 * sizeof(x) returns the size in bytes of x.
 *
 * COMMON PATTERN TO GET ARRAY LENGTH:
 *   sizeof(array) / sizeof(array[0])
 *
 * Example: If items is 6 menu_items, and each menu_item is 16 bytes:
 *   sizeof(items) = 96 bytes (total array size)
 *   sizeof(items[0]) = 16 bytes (one element size)
 *   item_count = 96 / 16 = 6
 *
 * WHY NOT JUST WRITE 6?
 *   - If you add/remove items, the count updates automatically
 *   - No risk of forgetting to update a magic number
 *   - The compiler calculates this at compile time (no runtime cost)
 */
static const int item_count = sizeof(items) / sizeof(items[0]);

/*
 * selected - Index of the currently highlighted menu item
 *
 * This is static (not const) because it CHANGES when the user presses
 * up/down arrows. It starts at 0 (first item).
 *
 * VALID VALUES: 0 to item_count-1
 */
static int selected = 0;
static int hw_exit_armed = 0;
static time_t hw_exit_arm_ts = 0;
static int hw_pin_prompt = 0;
static char hw_pin_buf[8] = {0};
static int hw_pin_len = 0;
static char hw_pin_error[32] = {0};

/* ═══════════════════════════════════════════════════════════════════════════
 *  DRAW FUNCTION
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * screen_home_draw() - Draw the home screen menu
 *
 * WHAT IT DOES:
 * Draws all menu items in a vertical list. The selected item is shown
 * in a highlight color with a cursor (▸) next to it.
 *
 * PARAMETERS:
 *   phone - The ncplane to draw on (passed from main.c)
 *
 * CALLED BY:
 *   The main event loop in main.c, every frame when current_screen == SCREEN_HOME
 *
 * LAYOUT:
 *   Row 3: ▸ Calls        (if selected == 0)
 *   Row 4:   Messages
 *   Row 5:   Settings
 *   ... etc
 *
 * HOW TO MODIFY:
 *   - Change starting row: Edit HOME_CONTENT_START_ROW in config.h
 *   - Change left margin: Edit HOME_CONTENT_COL in config.h
 *   - Change spacing: Edit HOME_ROW_SPACING in config.h
 *   - Change cursor: Edit MENU_CURSOR in config.h
 */
void screen_home_draw(struct ncplane *phone)
{
    /*
     * Get the plane dimensions so we know where to stop drawing.
     * We don't want to draw outside the visible area or into the footer.
     */
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    /*
     * Safety check: If the plane is too small, show an error message
     * instead of trying to draw a garbled menu.
     */
    if (rows < HOME_MIN_ROWS || cols < HOME_MIN_COLS)
    {
        ncplane_putstr_yx(phone, 2, 2, TEXT_TOO_SMALL);
        return; /* Early return - don't draw anything else */
    }

    if (settings_service_get_bool(SETTINGS_KEY_HAND_WHITE))
    {
        time_t now = time(NULL);
        struct tm tm_now;
        char tbuf[16] = "00:00";
        if (localtime_r(&now, &tm_now))
        {
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        }

        ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), "HAND WHITE MODE");
        ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_primary(), tbuf);
        ghost_text(phone, CONTENT_START_ROW + 6, CONTENT_COL, theme_text_muted(), "LAT: 35.6895 N");
        ghost_text(phone, CONTENT_START_ROW + 7, CONTENT_COL, theme_text_muted(), "LON: 51.3890 E");
        if (hw_pin_prompt)
        {
            int w = HOME_PIN_POPUP_WIDTH;
            int h = HOME_PIN_POPUP_HEIGHT;
            int top = ((int)rows - h) / 2;
            int left = ((int)cols - w) / 2;
            if (top < UI_POPUP_MIN_TOP)
                top = UI_POPUP_MIN_TOP;
            if (left < UI_POPUP_MIN_LEFT)
                left = UI_POPUP_MIN_LEFT;
            ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());
            ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                       theme_text_primary(), "ENTER PIN");
            char dots[8] = "    ";
            for (int i = 0; i < hw_pin_len && i < 4; i++)
                dots[i] = '*';
            ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                       theme_text_primary(), dots);
            if (hw_pin_error[0] != '\0')
            {
                ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET + 1,
                           left + UI_POPUP_TEXT_INSET_X, theme_border(), hw_pin_error);
            }
            ghost_softkeys(phone, "[Cancel]", "[OK]");
        }
        else if (hw_exit_armed)
        {
            ghost_text(phone, CONTENT_START_ROW + 9, CONTENT_COL, theme_text_muted(), "PRESS RIGHT SOFT NOW");
            ghost_softkeys(phone, "[Exit 1/2]", "[Exit 2/2]");
        }
        else
        {
            ghost_text(phone, CONTENT_START_ROW + 9, CONTENT_COL, theme_text_muted(), "HOLD BOTH SOFT KEYS TO EXIT");
            ghost_softkeys(phone, "[Exit 1/2]", "[Exit 2/2]");
        }
        return;
    }

    /*
     * Loop through each menu item and draw it.
     *
     * C CONCEPT: FOR LOOP
     * -------------------
     * for (initialization; condition; update) { body }
     *
     *   initialization: runs once before the loop starts
     *   condition: checked before each iteration; loop continues while true
     *   update: runs after each iteration
     *
     * EXAMPLE:
     *   for (int i = 0; i < 10; i++) {
     *       printf("%d\n", i);  // Prints 0, 1, 2, ..., 9
     *   }
     */

    int list_width = INNER_WIDTH(cols);

    for (int i = 0; i < item_count; i++)
    {
        int row = HOME_CONTENT_START_ROW + 1 + (i * HOME_ROW_SPACING);

        if (row >= (int)rows - 3)
            break;

        int is_sel = (i == selected);
        uint32_t fg = is_sel ? theme_selection_text() : theme_text_muted();
        uint32_t bg = is_sel ? theme_selection_bg()   : theme_bg();

        /* Full-width row fill so the highlight block is solid */
        ncplane_set_fg_rgb(phone, fg);
        ncplane_set_bg_rgb(phone, bg);
        for (int x = 0; x < list_width && HOME_CONTENT_COL + x < (int)cols - 1; x++)
            ncplane_putchar_yx(phone, row, HOME_CONTENT_COL + x, ' ');

        /* Cursor dot + label */
        ncplane_putstr_yx(phone, row, HOME_CONTENT_COL,     is_sel ? MENU_CURSOR : MENU_CURSOR_BLANK);
        ncplane_putstr_yx(phone, row, HOME_CONTENT_COL + 2, items[i].label);
    }

    /* Minimal music status — only when playing or paused */
    mp3_playback_state mp3st = mp3_service_get_state();
    if (mp3st == MP3_PLAYING || mp3st == MP3_PAUSED) {
        const char *track = mp3_service_current_track_name();
        char status_line[64];
        snprintf(status_line, sizeof(status_line), "%s %s",
                 mp3st == MP3_PLAYING ? "\xe2\x96\xb6" : "\xe2\x96\x8c\xe2\x96\x8c",
                 track ? track : "");
        ghost_text(phone, (int)rows - 3, CONTENT_COL, theme_text_muted(), status_line);
    }

    ghost_softkeys(phone, "[Quit]", "[Open]");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  INPUT HANDLER
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * screen_home_input() - Handle keyboard input on the home screen
 *
 * WHAT IT DOES:
 * Processes key presses and either:
 *   - Updates the selection (up/down arrows)
 *   - Navigates to a new screen (Enter key)
 *   - Does nothing (other keys)
 *
 * PARAMETERS:
 *   key - The key code from Notcurses (uint32_t)
 *         Can be a regular character ('a', '1', etc.) or
 *         a special key (NCKEY_UP, NCKEY_DOWN, NCKEY_ENTER)
 *
 * RETURNS:
 *   screen_id - The screen to display next
 *               Usually SCREEN_HOME (stay here)
 *               Or items[selected].target (navigate to selected item)
 *
 * CALLED BY:
 *   The main event loop in main.c after receiving keyboard input
 *
 * HOW IT WORKS:
 *   1. Check if key is UP arrow → move selection up
 *   2. Check if key is DOWN arrow → move selection down
 *   3. Check if key is ENTER → return the target screen
 *   4. Otherwise → return SCREEN_HOME (no change)
 *
 *
 * NOTCURSES: KEY CODES
 * --------------------
 * Regular ASCII characters are returned as their character values:
 *   'a' = 97, 'A' = 65, '1' = 49, ' ' = 32, '\n' = 10
 *
 * Special keys have constants defined in notcurses.h:
 *   NCKEY_UP, NCKEY_DOWN, NCKEY_LEFT, NCKEY_RIGHT
 *   NCKEY_ENTER, NCKEY_TAB, NCKEY_BACKSPACE
 *   NCKEY_F1 through NCKEY_F12
 *   NCKEY_HOME, NCKEY_END, NCKEY_PGUP, NCKEY_PGDOWN
 *
 * These special keys have values above 0x100000 to distinguish them
 * from regular Unicode codepoints.
 */
screen_id screen_home_input(uint32_t key)
{
    if (settings_service_get_bool(SETTINGS_KEY_HAND_WHITE))
    {
        time_t now = time(NULL);

        if (hw_pin_prompt)
        {
            if (key == KEY_SOFT_LEFT_ACTION)
            {
                hw_pin_prompt = 0;
                hw_pin_len = 0;
                hw_pin_buf[0] = '\0';
                hw_pin_error[0] = '\0';
                return SCREEN_HOME;
            }
            if (key == NCKEY_BACKSPACE || key == 127)
            {
                if (hw_pin_len > 0)
                {
                    hw_pin_len--;
                    hw_pin_buf[hw_pin_len] = '\0';
                }
                return SCREEN_HOME;
            }
            if (key >= '0' && key <= '9')
            {
                if (hw_pin_len < 4)
                {
                    hw_pin_buf[hw_pin_len++] = (char)key;
                    hw_pin_buf[hw_pin_len] = '\0';
                }
                if (hw_pin_len == 4)
                {
                    if (pin_service_verify(hw_pin_buf))
                    {
                        settings_service_toggle_by_key(SETTINGS_KEY_HAND_WHITE);
                        theme_service_sync_from_settings();
                        hw_pin_prompt = 0;
                        hw_pin_len = 0;
                        hw_pin_buf[0] = '\0';
                        hw_pin_error[0] = '\0';
                    }
                    else
                    {
                        snprintf(hw_pin_error, sizeof(hw_pin_error), "WRONG PIN");
                        hw_pin_len = 0;
                        hw_pin_buf[0] = '\0';
                    }
                }
            }
            return SCREEN_HOME;
        }

        if (hw_exit_armed && difftime(now, hw_exit_arm_ts) > 2.0)
        {
            hw_exit_armed = 0;
        }

        if (key == KEY_SOFT_LEFT_ACTION)
        {
            hw_exit_armed = 1;
            hw_exit_arm_ts = now;
            return SCREEN_HOME;
        }
        if ((key == KEY_SOFT_RIGHT_ACTION) && hw_exit_armed)
        {
            hw_pin_prompt = 1;
            hw_exit_armed = 0;
            hw_pin_len = 0;
            hw_pin_buf[0] = '\0';
            hw_pin_error[0] = '\0';
            return SCREEN_HOME;
        }
        return SCREEN_HOME;
    }

    /*
     * C CONCEPT: SWITCH STATEMENT
     * ---------------------------
     * Switch compares 'key' against multiple case values.
     * When a match is found, that case's code runs.
     *
     * IMPORTANT: 'break' is needed to prevent "fall through"!
     * Without break, execution continues into the next case.
     *
     * Fall-through is sometimes intentional (see NCKEY_ENTER and '\n' below).
     */
    switch (key)
    {
    /*
     * UP ARROW: Move selection up (toward index 0)
     *
     * We only decrement if selected > 0 to prevent going negative.
     * This is called "bounds checking."
     */
    case NCKEY_UP:
        if (selected > 0)
            selected--;
        return SCREEN_HOME; /* Stay on home screen */

    /*
     * DOWN ARROW: Move selection down (toward item_count - 1)
     *
     * We only increment if we're not at the last item.
     * (item_count - 1) is the index of the last valid item.
     */
    case NCKEY_DOWN:
        if (selected < item_count - 1)
            selected++;
        return SCREEN_HOME; /* Stay on home screen */

    /*
     * LEFT ARROW: Same as LSK (quit) — matches on-screen D-pad layout
     */
    case NCKEY_LEFT:
        return SCREEN_HOME; /* No-op on home; quit is handled in main.c */

    /*
     * RIGHT ARROW: Same as ENTER/RSK — open selected item
     */
    case NCKEY_RIGHT:
        return items[selected].target;

    /*
     * ENTER KEY: Navigate to the selected item's target screen
     *
     * NOTE: We handle both NCKEY_ENTER and '\n' (newline character).
     * Some terminals send '\n' instead of NCKEY_ENTER.
     *
     * This is an example of INTENTIONAL fall-through:
     *   case NCKEY_ENTER:
     *   case '\n':
     *       // This code runs for either key
     *
     * No 'break' after NCKEY_ENTER means execution continues to '\n' case.
     */
    case NCKEY_ENTER:
    case '\n':
    case KEY_SOFT_RIGHT_ACTION:
        /*
         * items[selected].target accesses:
         *   1. items - our menu_item array
         *   2. [selected] - the currently selected index
         *   3. .target - the screen_id field of that item
         */
        return items[selected].target;

    /*
     * DEFAULT: Any other key does nothing
     *
     * We stay on the home screen and ignore the key.
     * The 'default' case catches any value not explicitly handled.
     */
    default:
        return SCREEN_HOME;
    }
}
