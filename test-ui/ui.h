/*
 * ============================================================================
 *  ui.h — User Interface Header
 * ============================================================================
 *
 *  WHAT IS THIS FILE?
 *  ------------------
 *  This header declares all the types and functions related to screens
 *  and user interface. It's the "contract" between main.c and the
 *  individual screen implementation files.
 *
 *  CONTENTS:
 *    - screen_id enum: Identifiers for each screen
 *    - Function declarations: Draw and input functions for each screen
 *
 *
 *  C CONCEPTS DEMONSTRATED:
 *  ------------------------
 *  1. Header guards - preventing double inclusion
 *  2. enums - named integer constants
 *  3. typedef - creating type aliases
 *  4. Function declarations - telling compiler what functions exist
 *
 *
 *  WHY HAVE A SEPARATE HEADER?
 *  ---------------------------
 *  In C, code is organized into:
 *    - Headers (.h): DECLARATIONS - what exists, not how it works
 *    - Source (.c): DEFINITIONS - the actual implementation code
 *
 *  This separation allows:
 *    - main.c to call screen_home_draw() without seeing its code
 *    - Changing screen_home.c without recompiling main.c
 *    - Multiple files to use the same functions
 *
 *
 *  HOW TO ADD A NEW SCREEN:
 *  ------------------------
 *  1. Add a new enum value below (e.g., SCREEN_CALCULATOR)
 *  2. Declare the draw function (e.g., void screen_calculator_draw(...))
 *  3. Optionally declare an input function
 *  4. Create screen_calculator.c with the implementations
 *  5. Add screen_calculator.c to CMakeLists.txt
 *  6. Add the case to the switch in main.c
 *
 * ============================================================================
 */

#ifndef BLACKHAND_UI_H
#define BLACKHAND_UI_H

/* ═══════════════════════════════════════════════════════════════════════════
 *  INCLUDES
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * <notcurses/notcurses.h> - Notcurses Library
 *
 * We need this for:
 *   - struct ncplane: The drawing surface type used in function parameters
 *
 * Including it here means any file that includes ui.h automatically
 * gets access to Notcurses types too.
 */
#include <notcurses/notcurses.h>

/*
 * <stdint.h> - Fixed-width Integer Types
 *
 * We need uint32_t for key codes in input handler functions.
 */
#include <stdint.h>


/* ═══════════════════════════════════════════════════════════════════════════
 *  SCREEN IDENTIFIERS (enum)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  C CONCEPT: ENUMS
 *  ----------------
 *  An enum (enumeration) creates a set of named integer constants.
 *  It's a way to give meaningful names to numbers.
 *
 *  SYNTAX:
 *    enum name {
 *        VALUE_A,    // = 0 (first value is 0 by default)
 *        VALUE_B,    // = 1 (each subsequent is +1)
 *        VALUE_C = 10, // = 10 (can specify explicit values)
 *        VALUE_D     // = 11 (continues from last explicit)
 *    };
 *
 *  WHY USE ENUMS?
 *    - Self-documenting code (SCREEN_HOME vs. 0)
 *    - Compiler can warn about missing switch cases
 *    - Prevents typos (SCREEN_HONE would be a compile error)
 *    - IDE autocomplete shows valid options
 *
 *  WITHOUT ENUM:
 *    int current_screen = 0;  // What does 0 mean?
 *    if (current_screen == 1) { ... }  // Magic number!
 *
 *  WITH ENUM:
 *    screen_id current_screen = SCREEN_HOME;  // Clear meaning
 *    if (current_screen == SCREEN_SETTINGS) { ... }  // Obvious!
 *
 *
 *  C CONCEPT: TYPEDEF ENUM
 *  -----------------------
 *  Without typedef, you'd have to write "enum screen_id" everywhere:
 *    enum screen_id current = SCREEN_HOME;
 *
 *  With typedef, you can just write "screen_id":
 *    typedef enum { ... } screen_id;
 *    screen_id current = SCREEN_HOME;  // Cleaner!
 */

/*
 * screen_id — Identifies which screen to display
 *
 * Each value represents a different screen in the application.
 * Used by the main loop to know which draw/input functions to call.
 *
 * HOW TO ADD A NEW SCREEN:
 *   1. Add a new value here (before the closing brace)
 *   2. The value will automatically be one more than the previous
 *   3. Implement the screen's draw function
 *   4. Add it to the switch statements in main.c
 */
typedef enum {
    SCREEN_HOME = 0,    /* Main menu with app list */
    SCREEN_SETTINGS,    /* Settings/configuration (= 1) */
    SCREEN_CALLS,       /* Phone calls screen (= 2) */
    SCREEN_MESSAGES,    /* Text messages screen (= 3) */
    SCREEN_CONTACTS,    /* Contact list screen (= 4) */
    SCREEN_MP3,         /* Music player screen (= 5) */
    SCREEN_VOICE_MEMO,  /* Voice recording screen (= 6) */
    SCREEN_NOTES,       /* Notes/text editor screen (= 7) */
    SCREEN_THEME,       /* Theme selection (= 8) */
    SCREEN_BLUETOOTH    /* Bluetooth manager (= 9) */
} screen_id;


/* ═══════════════════════════════════════════════════════════════════════════
 *  FUNCTION DECLARATIONS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  C CONCEPT: FUNCTION DECLARATIONS
 *  --------------------------------
 *  A declaration tells the compiler about a function without providing
 *  the code. It specifies:
 *    - Return type
 *    - Function name
 *    - Parameter types (and optionally names)
 *
 *  The compiler uses this to:
 *    - Check that calls use the correct types
 *    - Know what type the function returns
 *
 *  The actual code (definition) is in the .c file.
 *
 *  SYNTAX:
 *    return_type function_name(param_type param_name, ...);
 *                                                        ^ semicolon!
 *
 *  EXAMPLE:
 *    int add(int a, int b);           // Declaration in .h
 *    int add(int a, int b) { return a + b; }  // Definition in .c
 */

/* ─── Screen Draw Functions ────────────────────────────────────────────── */

/*
 * screen_home_draw() — Draw the home screen (main menu)
 *
 * DECLARED IN: ui.h (this file)
 * DEFINED IN: screen_home.c
 *
 * PARAMETERS:
 *   phone - Pointer to the ncplane (drawing surface) to draw on
 *
 * WHAT IT DOES:
 *   Draws the main menu showing all available apps (Calls, Messages, etc.)
 *   with a cursor indicating the selected item.
 *
 * CALLED BY:
 *   The main event loop in main.c, when current_screen == SCREEN_HOME
 */
void screen_home_draw(struct ncplane *phone);
screen_id screen_home_input(uint32_t key);

/*
 * screen_settings_draw() — Draw the settings screen
 *
 * DECLARED IN: ui.h (this file)
 * DEFINED IN: screen_settings.c
 *
 * Currently displays static placeholder content.
 * Could be extended to show editable settings.
 */
void screen_settings_draw(struct ncplane *phone);
screen_id screen_settings_input(uint32_t key);

void screen_calls_draw(struct ncplane *phone);
screen_id screen_calls_input(uint32_t key);

void screen_messages_draw(struct ncplane *phone);
screen_id screen_messages_input(uint32_t key);

void screen_contacts_draw(struct ncplane *phone);
screen_id screen_contacts_input(uint32_t key);
int screen_contacts_is_edit_mode(void);

void screen_mp3_draw(struct ncplane *phone);
screen_id screen_mp3_input(uint32_t key);

void screen_voice_memo_draw(struct ncplane *phone);
screen_id screen_voice_memo_input(uint32_t key);
int screen_voice_memo_is_text_entry_mode(void);

void screen_notes_draw(struct ncplane *phone);
screen_id screen_notes_input(uint32_t key);
int screen_notes_is_edit_mode(void);

void screen_theme_draw(struct ncplane *phone);
screen_id screen_theme_input(uint32_t key);

void screen_bluetooth_draw(struct ncplane *phone);
screen_id screen_bluetooth_input(uint32_t key);

int screen_settings_is_pin_entry_mode(void);

/* ─── Screen Input Handlers ────────────────────────────────────────────── */

/*
 * screen_home_input() — Handle input on the home screen
 *
 * DECLARED IN: ui.h (this file)
 * DEFINED IN: screen_home.c
 *
 * PARAMETERS:
 *   key - The key code from Notcurses
 *         Regular characters: 'a', '1', ' '
 *         Special keys: NCKEY_UP, NCKEY_DOWN, NCKEY_ENTER
 *
 * RETURNS:
 *   screen_id - The screen to display next
 *               Usually SCREEN_HOME (stay here)
 *               Or the target screen when Enter is pressed
 *
 * WHAT IT DOES:
 *   - Up/Down arrows: Move selection cursor
 *   - Enter: Navigate to the selected screen
 *   - Other keys: Ignored (returns SCREEN_HOME)
 */
/*
 * ADDING INPUT HANDLERS FOR OTHER SCREENS:
 *
 * If you want a screen to handle input (not just display), declare
 * an input function here:
 *
 *   screen_id screen_settings_input(uint32_t key);
 *
 * Then implement it in screen_settings.c, and add it to the input
 * switch in main.c:
 *
 *   case SCREEN_SETTINGS:
 *       current_screen = screen_settings_input(key);
 *       break;
 */

#endif /* BLACKHAND_UI_H */
