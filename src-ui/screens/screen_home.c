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
typedef struct {
    const char *label;   /* Text shown in menu (pointer to string literal) */
    screen_id   target;  /* Screen to go to when selected (enum value) */
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
    { "CONTACTS",      SCREEN_CONTACTS   },
    { "MESSAGES",      SCREEN_MESSAGES   },
    { "CALLS",         SCREEN_CALLS      },
    { "NOTES",         SCREEN_NOTES      },
    { "VOICE MEMO",    SCREEN_VOICE_MEMO },
    { "MUSIC",         SCREEN_MP3        },
    { "ALARM",         SCREEN_ALARM      },
    { "SETTINGS",      SCREEN_SETTINGS   },
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
void screen_home_draw(struct ncplane *phone) {
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
    if (rows < HOME_MIN_ROWS || cols < HOME_MIN_COLS) {
        ncplane_putstr_yx(phone, 2, 2, TEXT_TOO_SMALL);
        return;  /* Early return - don't draw anything else */
    }

    if (settings_service_get_bool("hand_white")) {
        time_t now = time(NULL);
        struct tm tm_now;
        char tbuf[16] = "00:00";
        if (localtime_r(&now, &tm_now)) {
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        }

        ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), "HAND WHITE MODE");
        ghost_text(phone, CONTENT_START_ROW + 4, CONTENT_COL, theme_text_primary(), tbuf);
        ghost_text(phone, CONTENT_START_ROW + 6, CONTENT_COL, theme_text_muted(), "LAT: 35.6895 N");
        ghost_text(phone, CONTENT_START_ROW + 7, CONTENT_COL, theme_text_muted(), "LON: 51.3890 E");
        ghost_softkeys(phone, "[Quit]", "[Config]");
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
    ghost_text(phone, CONTENT_START_ROW, CONTENT_COL, theme_text_muted(), "MODE: READY");

    for (int i = 0; i < item_count; i++) {
        /*
         * Calculate which row to draw this item on.
         *
         * HOME_CONTENT_START_ROW = 3 (from config.h)
         * HOME_ROW_SPACING = 1 (items are 1 row apart)
         *
         * Item 0: row = 3 + (0 * 1) = 3
         * Item 1: row = 3 + (1 * 1) = 4
         * Item 2: row = 3 + (2 * 1) = 5
         * ... etc
         */
        int row = HOME_CONTENT_START_ROW + 1 + (i * HOME_ROW_SPACING);

        /*
         * Stop if we would draw into the footer area.
         * rows - 2 is the footer separator row.
         *
         * C CONCEPT: TYPE CASTING
         * -----------------------
         * (int)rows converts 'rows' from unsigned to signed int.
         * This is needed because comparing signed and unsigned can
         * give unexpected results (unsigned is always >= 0).
         */
        if (row >= (int)rows - 2) break;

        /*
         * Choose colors and cursor based on whether this item is selected.
         *
         * C CONCEPT: TERNARY OPERATOR
         * ---------------------------
         * condition ? value_if_true : value_if_false
         *
         * It's a compact way to choose between two values.
         *
         * EQUIVALENT IF/ELSE:
         *   uint32_t fg;
         *   if (i == selected) {
         *       fg = COL_MENU_SELECTED;
         *   } else {
         *       fg = COL_MENU_NORMAL;
         *   }
         */
        uint32_t fg = (i == selected) ? theme_text_primary() : theme_text_muted();
        const char *cursor = (i == selected) ? MENU_CURSOR : MENU_CURSOR_BLANK;

        /*
         * Draw the menu item.
         *
         * NOTCURSES: ncplane_set_fg_rgb(plane, color)
         *   Sets the foreground (text) color for subsequent drawing.
         *   Color is 0xRRGGBB (24-bit RGB).
         *
         * NOTCURSES: ncplane_set_bg_rgb(plane, color)
         *   Sets the background color for subsequent drawing.
         *
         * NOTCURSES: ncplane_putstr_yx(plane, row, col, string)
         *   Draws a string at the specified position.
         *   Row is the Y coordinate (vertical), Col is X (horizontal).
         */
        ncplane_set_fg_rgb(phone, fg);
        ncplane_set_bg_rgb(phone, theme_bg());

        /* Draw cursor (▸ if selected, blank spaces if not) */
        ncplane_putstr_yx(phone, row, HOME_CONTENT_COL, cursor);

        /* Draw the menu label, offset by 2 to leave room for cursor */
        ncplane_putstr_yx(phone, row, HOME_CONTENT_COL + 2, items[i].label);
    }

    if (mp3_service_get_state() == PLAYING) {
        ghost_text(phone, (int)rows - 4, CONTENT_COL, theme_text_muted(), "STATUS: AUDIO PLAYING");
    } else if (mp3_service_get_state() == PAUSED) {
        ghost_text(phone, (int)rows - 4, CONTENT_COL, theme_text_muted(), "STATUS: AUDIO PAUSED");
    } else {
        ghost_text(phone, (int)rows - 4, CONTENT_COL, theme_text_muted(), "STATUS: IDLE");
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
screen_id screen_home_input(uint32_t key) {
    if (settings_service_get_bool("hand_white")) {
        if (key == 'e' || key == 'E' || key == NCKEY_ENTER || key == '\n') return SCREEN_SETTINGS;
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
    switch (key) {
        /*
         * UP ARROW: Move selection up (toward index 0)
         *
         * We only decrement if selected > 0 to prevent going negative.
         * This is called "bounds checking."
         */
        case NCKEY_UP:
            if (selected > 0) selected--;
            return SCREEN_HOME;  /* Stay on home screen */

        /*
         * DOWN ARROW: Move selection down (toward item_count - 1)
         *
         * We only increment if we're not at the last item.
         * (item_count - 1) is the index of the last valid item.
         */
        case NCKEY_DOWN:
            if (selected < item_count - 1) selected++;
            return SCREEN_HOME;  /* Stay on home screen */

        /*
         * LEFT ARROW: Same as LSK (quit) — matches on-screen D-pad layout
         */
        case NCKEY_LEFT:
            return SCREEN_HOME;  /* No-op on home; quit is handled in main.c */

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
        case 'e':
        case 'E':
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
