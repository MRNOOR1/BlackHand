/*
 * config.h — single source of truth for every UI constant and key binding.
 *
 * Edit this file to change colors, sizes, paths, settings keys, or controls.
 * Nothing else in the tree should #define a configuration value.
 *
 *   1. Phone dimensions
 *   2. Frame / status / popup layout
 *   3. Colors used by frame_renderer / placeholders
 *   4. Text strings
 *   5. Storage paths
 *   6. Settings keys / defaults / ranges
 *   7. Theme registry
 *   8. Alarm limits
 *   9. CONTROLS — logical actions and physical key bindings
 *
 * Control rule: F-keys and arrow/Enter cluster are the only controls.
 * Letters and digits are reserved for text input (multitap, dial, PIN).
 */

#ifndef BLACKHAND_CONFIG_H
#define BLACKHAND_CONFIG_H

#include <notcurses/notcurses.h>

/* ── 1. Phone dimensions ───────────────────────────────────────────────── */
#define PHONE_COLS        40
#define PHONE_SCREEN_ROWS 15
#define KEYPAD_ROWS       0
#define PHONE_ROWS        (PHONE_SCREEN_ROWS + KEYPAD_ROWS)

/* ── 2. Layout ─────────────────────────────────────────────────────────── */
#define FRAME_MIN_ROWS    3
#define FRAME_MIN_COLS    10

#define STATUS_ROW         1
#define STATUS_BATTERY_COL 2

#define CONTENT_START_ROW  3
#define CONTENT_COL        2
#define FOOTER_ROW_OFFSET  2
#define INNER_WIDTH(cols)  ((int)(cols) - 2 * CONTENT_COL)

#define HOME_CONTENT_START_ROW 3
#define HOME_MIN_ROWS          6
#define HOME_MIN_COLS          20

#define UI_POPUP_MIN_TOP          3
#define UI_POPUP_MIN_LEFT         1
#define UI_POPUP_TEXT_INSET_X     2
#define UI_POPUP_TITLE_ROW_OFFSET 1
#define UI_POPUP_INPUT_ROW_OFFSET 3
#define UI_POPUP_HINT_ROW_OFFSET  5

#define SETTINGS_PIN_POPUP_WIDTH    28
#define SETTINGS_PIN_POPUP_HEIGHT   7
#define HOME_PIN_POPUP_WIDTH        26
#define HOME_PIN_POPUP_HEIGHT       6
#define NOTES_SAVE_POPUP_HEIGHT     7
#define VOICE_MEMO_NAME_POPUP_HEIGHT 7
#define ALARM_TIME_POPUP_WIDTH      32
#define ALARM_TIME_POPUP_HEIGHT     7
#define ALARM_TIME_POPUP_MIN_LEFT   2
#define ALARM_RING_POPUP_WIDTH      34
#define ALARM_RING_POPUP_HEIGHT     8

/* ── 3. Colors used outside the theme engine ───────────────────────────── */
#define COL_GHOST_LOW   0x7F1D1D  /* low-battery deep red */
#define COL_PLACEHOLDER 0x555555  /* "Coming soon..." */
#define COL_HINT        0xa5a58d  /* "[h] go Home"   */

/* ── 4. Text strings ───────────────────────────────────────────────────── */
#define TEXT_COMING_SOON  "Coming soon..."
#define TEXT_GO_HOME      "[h] go Home"
#define TEXT_TOO_SMALL    "Too small"
#define MENU_CURSOR       ">"
#define MENU_CURSOR_BLANK ""

/* ── 5. Storage paths (FAT-safe / read from any host) ──────────────────── */
#define APP_PATH_SETTINGS_FILE   "/data/settings.conf"
#define APP_PATH_NOTES_DIR       "/data/notes"
#define APP_PATH_CONTACTS_DIR    "/data/contacts"
#define APP_PATH_ALARMS_DIR      "/data/alarms"
#define APP_PATH_VOICE_MEMOS_DIR "/data/voice-memos"
#define APP_PATH_MUSIC_DIR       "/data/music"

/* ── 6. Settings: keys / labels / defaults / ranges ────────────────────── */
#define SETTINGS_KEY_NIGHT_MODE  "night_mode"
#define SETTINGS_KEY_BLUETOOTH   "bluetooth"
#define SETTINGS_KEY_HAND_WHITE  "hand_white"
#define SETTINGS_KEY_AUX_INPUT   "aux_input"
#define SETTINGS_KEY_LIGHT_THEME "light_theme"
#define SETTINGS_KEY_VOLUME      "volume"
#define SETTINGS_KEY_BRIGHTNESS  "brightness"
#define SETTINGS_KEY_TIMEOUT_SEC "timeout_sec"

#define SETTINGS_LABEL_NIGHT_MODE "Night Mode"
#define SETTINGS_LABEL_BLUETOOTH  "Bluetooth"
#define SETTINGS_LABEL_HAND_WHITE "Hand White"
#define SETTINGS_LABEL_AUX_INPUT  "Aux Input"

#define SETTINGS_DEFAULT_NIGHT_MODE  0
#define SETTINGS_DEFAULT_BLUETOOTH   0
#define SETTINGS_DEFAULT_HAND_WHITE  0
#define SETTINGS_DEFAULT_AUX_INPUT   1
#define SETTINGS_DEFAULT_THEME_INDEX UI_DEFAULT_THEME_INDEX
#define SETTINGS_DEFAULT_VOLUME      7
#define SETTINGS_MIN_VOLUME          0
#define SETTINGS_MAX_VOLUME          10
#define SETTINGS_DEFAULT_BRIGHTNESS  8
#define SETTINGS_MIN_BRIGHTNESS      1
#define SETTINGS_MAX_BRIGHTNESS      10
#define SETTINGS_DEFAULT_TIMEOUT_SEC 60
#define SETTINGS_MIN_TIMEOUT_SEC     15
#define SETTINGS_MAX_TIMEOUT_SEC     600

#define SETTINGS_MAIN_ITEM_APPEARANCE   "APPEARANCE"
#define SETTINGS_MAIN_ITEM_SECURITY     "SECURITY"
#define SETTINGS_MAIN_ITEM_CONNECTIVITY "CONNECTIVITY"
#define SETTINGS_MAIN_ITEM_SYSTEM_INFO  "SYSTEM INFO"
#define SETTINGS_MAIN_ITEM_COUNT        4

/* ── 7. Theme registry ─────────────────────────────────────────────────── */
#define UI_THEME_COUNT         12
#define UI_DEFAULT_THEME_INDEX 0

/* ── 8. Alarms ─────────────────────────────────────────────────────────── */
#define ALARM_MAX_COUNT  20
#define ALARM_SNOOZE_MIN 5

/* ═══════════════════════════════════════════════════════════════════════════
 *  9. CONTROLS
 *
 *  Rules:
 *    - Letter keys are NEVER controls. Reserved for text input.
 *    - Digit keys are NEVER controls. Reserved for dial / multitap.
 *    - Controls = F-keys + arrow / Enter cluster.
 *
 *  Logical actions (KEY_*_ACTION) live in Unicode Private Use Area so they
 *  cannot collide with any real keystroke a user can produce. The normalize
 *  layer in main.c only rewrites the physical bindings below into these
 *  logical codes — every other key (digits, letters, *, #) passes through
 *  to the active screen unchanged.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* 9a. Logical softkey actions — what screens compare against. */
#define KEY_SOFT_LEFT_ACTION  0xE001u  /* F1 — back / cancel              */
#define KEY_SOFT_RIGHT_ACTION 0xE002u  /* F3 — open / save / options      */
#define KEY_ACTION_PRIMARY    0xE003u  /* F4 — call, play, send, answer   */
#define KEY_ACTION_SECONDARY  0xE004u  /* F6 — reject, delete, stop, hang */

/* 9b. Physical softkey bindings — F-keys only. HOME / PGUP are kept as */
/*     alternates because some terminals deliver F1 / F3 as those codes. */
#define KEY_BIND_SOFT_LEFT_ALT_1  NCKEY_HOME
#define KEY_BIND_SOFT_LEFT_ALT_2  NCKEY_F01
#define KEY_BIND_SOFT_RIGHT_ALT_1 NCKEY_PGUP
#define KEY_BIND_SOFT_RIGHT_ALT_2 NCKEY_F03

#define KEY_BIND_ACTION_PRIMARY_ALT   NCKEY_F04
#define KEY_BIND_ACTION_SECONDARY_ALT NCKEY_F06

/* 9c. Navigation — arrow cluster only. NO number-as-arrow fallback. */
#define KEY_BIND_UP     NCKEY_UP
#define KEY_BIND_DOWN   NCKEY_DOWN
#define KEY_BIND_LEFT   NCKEY_LEFT
#define KEY_BIND_RIGHT  NCKEY_RIGHT
#define KEY_BIND_SELECT NCKEY_ENTER
#define KEY_BIND_UP_ALT   NCKEY_F02
#define KEY_BIND_DOWN_ALT NCKEY_F05

#endif /* BLACKHAND_CONFIG_H */
