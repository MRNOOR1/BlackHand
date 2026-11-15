#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "ui.h"
#include "services/settings_service.h"
#include "services/theme_service.h"
#include "services/contacts_service.h"
#include "services/notes_service.h"
#include "services/voice_memo_service.h"
#include "services/comm_service.h"
#include "services/pin_service.h"

typedef enum {
    SETT_MODE_MAIN,
    SETT_MODE_APPEARANCE,
    SETT_MODE_SECURITY,
    SETT_MODE_CONNECTIVITY,
    SETT_MODE_INFO,
    SETT_MODE_PIN_ENTRY,
    SETT_MODE_PIN_CHANGE,
    SETT_MODE_WIPE_CONFIRM,
    SETT_MODE_WIPE_RESULT,
    SETT_MODE_POWEROFF_CONFIRM,
} settings_mode_t;

#define MAIN_ITEM_COUNT 5
static const char *main_items[] = {
    SETTINGS_MAIN_ITEM_APPEARANCE,
    SETTINGS_MAIN_ITEM_SECURITY,
    SETTINGS_MAIN_ITEM_CONNECTIVITY,
    SETTINGS_MAIN_ITEM_SYSTEM_INFO,
    "POWER OFF",
};

typedef enum {
    APP_NIGHT_MODE,
    APP_THEME,
} appearance_item_t;

typedef enum {
    SEC_HAND_WHITE,
    SEC_WHITE_WIPE,
    SEC_UPDATE_PIN,
} security_item_t;

static settings_mode_t mode = SETT_MODE_MAIN;
static int s_selected = 0;
static int s_sub_selected = 0;

static char pin_buf[8] = {0};
static int pin_len = 0;
static int pin_purpose = 0;
static char pin_new_buf[8] = {0};
static char pin_old_buf[8] = {0};
static char pin_error[64] = {0};

static int s_wipe_yes = 0;
static int s_poweroff_requested = 0;
static int s_poweroff_error = 0;

/* Set by wipe_phone_data(): distinguishes "some data survived the wipe"
 * (s_wipe_failed) from "everything was erased, but contacts.json had been
 * corrupt" (s_wipe_corrupt_found) — the wipe result popup needs to say
 * which happened; a corrupt file being removed is not a failed wipe. */
static int s_wipe_failed = 0;
static int s_wipe_corrupt_found = 0;

static void put_clipped(struct ncplane *p, int row, int col, int max_w, const char *text) {
    if (!text || max_w <= 0) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", text);
    if ((int)strlen(buf) > max_w) {
        if (max_w > 3) { buf[max_w-3]='.'; buf[max_w-2]='.'; buf[max_w-1]='.'; }
        buf[max_w] = '\0';
    }
    ncplane_putstr_yx(p, row, col, buf);
}

/* Returns 1 if every deletion succeeded, 0 if any item was left behind.
 * White Wipe is a data-destruction feature: a caller MUST check this and
 * tell the user, rather than reporting a clean wipe that half-happened. */
static int wipe_phone_data(void) {
    int all_ok = 1;
    size_t count = 0;
    s_wipe_corrupt_found = 0;

    const Contact **contacts = contact_service_list_all(&count);
    while (count > 0 && contacts && contacts[0] && contacts[0]->id) {
        /* contact_service_delete() only drops the cache entry on IPC
         * success (see contacts_service.c). If the storage daemon is
         * unreachable/slow, this call keeps failing on the SAME first
         * contact forever — bail out rather than hang the UI thread. */
        if (contact_service_delete(contacts[0]->id) != 0) { all_ok = 0; break; }
        contacts = contact_service_list_all(&count);
    }
    /* Guaranteed final pass: this loop enumerates via contacts.list, which
     * (correctly) refuses to fake data for a corrupt contacts.json — so a
     * corrupt file is invisible to it and would survive the wipe. Clobber
     * it unconditionally now; that is what an explicit factory reset is for. */
    int contacts_rc = contact_service_wipe_all();
    if (contacts_rc < 0) all_ok = 0;
    else if (contacts_rc == 1) s_wipe_corrupt_found = 1;

    Note **notes = notes_service_list_all(&count);
    while (count > 0 && notes && notes[0]) {
        if (notes_service_delete_note(notes[0]) != 0) { all_ok = 0; break; }
        notes = notes_service_list_all(&count);
    }

    const VoiceMemo **memos = voice_memo_service_list_all(&count);
    while (count > 0 && memos && memos[0] && memos[0]->filename) {
        if (voice_memo_service_delete(memos[0]->filename) != 0) { all_ok = 0; break; }
        memos = voice_memo_service_list_all(&count);
    }

    if (comm_service_reset() != 0) all_ok = 0;
    if (screen_messages_reset_draft() != 0) all_ok = 0;
    if (notes_service_note_count() == 0) screen_notes_reset_editor();
    if (all_ok) {
        settings_service_reset_defaults();
        if (pin_service_reset() != 0) all_ok = 0;
    }
    theme_service_sync_from_settings();
    return all_ok;
}

static void reset_pin_entry(void) {
    pin_buf[0] = '\0';
    pin_len = 0;
    pin_error[0] = '\0';
}

static void draw_main(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "SETTINGS");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(),
               settings_service_get_bool(SETTINGS_KEY_NIGHT_MODE) ? "Mode: Dark" : "Mode: Light");

    if (s_selected < 0) s_selected = 0;
    if (s_selected >= MAIN_ITEM_COUNT) s_selected = MAIN_ITEM_COUNT - 1;

    for (int i = 0; i < MAIN_ITEM_COUNT; i++) {
        int row = CONTENT_START_ROW + 3 + i;
        if (row >= footer) break;
        bh_list_item(phone, row, CONTENT_COL, width, main_items[i], "", i == s_selected, i);
    }

    ghost_softkeys(phone, "[Back]", "[Open]");
}

static void draw_appearance(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "APPEARANCE");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    int total = 2;
    if (s_sub_selected >= total) s_sub_selected = total - 1;
    if (s_sub_selected < 0) s_sub_selected = 0;

    int row = CONTENT_START_ROW + 4;
    if (row < footer)
        bh_list_item(phone, row, CONTENT_COL, width, "DARK MODE",
                     settings_service_get_bool(SETTINGS_KEY_NIGHT_MODE) ? "ON" : "OFF",
                     s_sub_selected == 0, 0);

    row = CONTENT_START_ROW + 5;
    if (row < footer)
        bh_list_item(phone, row, CONTENT_COL, width, "THEMES", "OPEN",
                     s_sub_selected == 1, 1);

    ghost_softkeys(phone, "[Back]", "[Select]");
}

static void draw_security(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "SECURITY");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    int total = 3;
    if (s_sub_selected >= total) s_sub_selected = total - 1;
    if (s_sub_selected < 0) s_sub_selected = 0;

    const char *sec_labels[] = { "HAND WHITE", "WHITE WIPE", "UPDATE PIN" };
    const char *sec_meta[]   = {
        settings_service_get_bool(SETTINGS_KEY_HAND_WHITE) ? "ON" : "OFF",
        "RUN", "OPEN",
    };

    for (int i = 0; i < total; i++) {
        int row = CONTENT_START_ROW + 3 + i;
        if (row >= footer) break;
        bh_list_item(phone, row, CONTENT_COL, width, sec_labels[i], sec_meta[i],
                     i == s_sub_selected, i);
    }

    ghost_softkeys(phone, "[Back]", "[Select]");
}

static void draw_connectivity(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int width = INNER_WIDTH(cols);
    (void)rows;

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "CONNECTIVITY");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    s_sub_selected = 0;
    int row = CONTENT_START_ROW + 3;
    bh_list_item(phone, row, CONTENT_COL, width, "BLUETOOTH",
                 settings_service_get_bool(SETTINGS_KEY_BLUETOOTH) ? "ON" : "OFF", 1, 0);

    ghost_softkeys(phone, "[Back]", "[Open]");
}

static void draw_info(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int width = INNER_WIDTH(cols);
    (void)rows;

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "SYSTEM INFO");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    int row = CONTENT_START_ROW + 3;
    ghost_label_value(phone, row,     CONTENT_COL, CONTENT_COL + 12, "MODEL", "BlackHand OS");
    ghost_label_value(phone, row + 1, CONTENT_COL, CONTENT_COL + 12, "VERSION", "Phase 2.0");
    ghost_label_value(phone, row + 2, CONTENT_COL, CONTENT_COL + 12, "PLATFORM", "RPi4");
    ghost_label_value(phone, row + 3, CONTENT_COL, CONTENT_COL + 12, "DISPLAY", "240x304");

    ghost_softkeys(phone, "[Back]", "");
}

static void draw_pin_entry(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    int w = SETTINGS_PIN_POPUP_WIDTH;
    int h = SETTINGS_PIN_POPUP_HEIGHT;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
    if (left < UI_POPUP_MIN_LEFT) left = UI_POPUP_MIN_LEFT;

    ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());

    const char *title = "ENTER PIN";
    if (pin_purpose == 1) title = "CURRENT PIN";
    else if (pin_purpose == 2) title = "NEW PIN";
    else if (pin_purpose == 3) title = "CONFIRM PIN";

    ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X, theme_text_primary(), title);

    char dots[8] = "    ";
    for (int i = 0; i < pin_len && i < 4; i++) dots[i] = '*';
    ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + 10, theme_text_primary(), dots);

    if (pin_error[0] != '\0') {
        ghost_text(phone, top + UI_POPUP_HINT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X, theme_border(), pin_error);
    }

    ghost_softkeys(phone, "[Cancel]", "[OK]");
}

static void draw_wipe_confirm(struct ncplane *phone) {
    ghost_text(phone, CONTENT_START_ROW, CONTENT_COL, theme_text_primary(), "WIPE PRIVATE DATA");
    ghost_text(phone, CONTENT_START_ROW + 1, CONTENT_COL, theme_text_muted(), "Contacts, SMS, calls");
    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), "Notes, memos, prefs");
    ghost_text(phone, CONTENT_START_ROW + 3, CONTENT_COL, theme_text_muted(), "Music/BT kept");
    ghost_text(phone, CONTENT_START_ROW + 5, CONTENT_COL, theme_text_primary(),
               s_wipe_yes ? "Cancel   > ERASE" : "> Cancel   Erase");
    ghost_softkeys(phone, "[Back]", "[Select]");
}

/* Shown whenever the wipe result needs explaining rather than just
 * silently returning home: either it stopped partway (storage IPC failure
 * mid-delete, s_wipe_failed) or it fully succeeded but found a corrupt
 * contacts.json along the way (s_wipe_corrupt_found). Either way, say so
 * explicitly rather than letting the caller assume a clean, unremarkable
 * wipe happened. */
static void draw_wipe_result(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    int w = SETTINGS_PIN_POPUP_WIDTH;
    int h = SETTINGS_PIN_POPUP_HEIGHT;
    int top = ((int)rows - h) / 2;
    int left = ((int)cols - w) / 2;
    if (top < UI_POPUP_MIN_TOP) top = UI_POPUP_MIN_TOP;
    if (left < UI_POPUP_MIN_LEFT) left = UI_POPUP_MIN_LEFT;

    ghost_fill_rect(phone, top, left, h, w, ' ', theme_text_primary(), theme_bg());
    if (s_wipe_failed) {
        ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_primary(), "WIPE INCOMPLETE");
        ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_muted(), "some data remains");
    } else {
        /* Wipe fully succeeded, but contacts.json had been corrupt — the
         * per-contact loop above could never see it, so the guaranteed
         * final pass erased it directly. Tell the user, don't just erase
         * it silently: they should know their old contacts were unreadable. */
        ghost_text(phone, top + UI_POPUP_TITLE_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_primary(), "WIPE COMPLETE");
        ghost_text(phone, top + UI_POPUP_INPUT_ROW_OFFSET, left + UI_POPUP_TEXT_INSET_X,
                   theme_text_muted(), "bad data removed");
    }
    ghost_softkeys(phone, "", "[OK]");
}

void screen_settings_draw(struct ncplane *phone) {
    switch (mode) {
        case SETT_MODE_MAIN:         draw_main(phone); break;
        case SETT_MODE_APPEARANCE:   draw_appearance(phone); break;
        case SETT_MODE_SECURITY:     draw_security(phone); break;
        case SETT_MODE_CONNECTIVITY: draw_connectivity(phone); break;
        case SETT_MODE_INFO:         draw_info(phone); break;
        case SETT_MODE_PIN_ENTRY:
        case SETT_MODE_PIN_CHANGE:
            draw_security(phone);
            draw_pin_entry(phone);
            break;
        case SETT_MODE_POWEROFF_CONFIRM:
            ghost_confirm_popup(phone, s_poweroff_error ? "SAVE FAILED; RETRY?" : "POWER OFF?", 1);
            break;
        case SETT_MODE_WIPE_CONFIRM:
            draw_security(phone);
            draw_wipe_confirm(phone);
            break;
        case SETT_MODE_WIPE_RESULT:
            draw_security(phone);
            draw_wipe_result(phone);
            break;
    }
}

static screen_id handle_pin_input(uint32_t key) {
    switch (key) {
        case KEY_SOFT_LEFT_ACTION:          /* Q = cancel */
            reset_pin_entry();
            mode = SETT_MODE_SECURITY;
            return SCREEN_SETTINGS;
        case KEY_ACTION_SECONDARY:          /* D = backspace */
        case NCKEY_BACKSPACE:
        case 127:
            if (pin_len > 0) {
                pin_len--;
                pin_buf[pin_len] = '\0';
            }
            pin_error[0] = '\0';
            return SCREEN_SETTINGS;
        default:
            if (key >= '0' && key <= '9') {
                if (pin_len < 4) {
                    pin_buf[pin_len++] = (char)key;
                    pin_buf[pin_len] = '\0';
                }
                if (pin_len == 4) {
                    if (pin_purpose == 0) {
                        if (pin_service_verify(pin_buf)) {
                            reset_pin_entry();
                            s_wipe_yes = 0;
                            mode = SETT_MODE_WIPE_CONFIRM;
                        } else {
                            int lockout = pin_service_lockout_remaining_sec();
                            if (lockout > 0)
                                snprintf(pin_error, sizeof(pin_error), "LOCKED %ds", lockout);
                            else
                                snprintf(pin_error, sizeof(pin_error), "WRONG PIN");
                            pin_len = 0;
                            pin_buf[0] = '\0';
                        }
                    } else if (pin_purpose == 1) {
                        if (pin_service_verify(pin_buf)) {
                            memcpy(pin_old_buf, pin_buf, sizeof(pin_old_buf));
                            reset_pin_entry();
                            pin_purpose = 2;
                        } else {
                            int lockout = pin_service_lockout_remaining_sec();
                            if (lockout > 0)
                                snprintf(pin_error, sizeof(pin_error), "LOCKED %ds", lockout);
                            else
                                snprintf(pin_error, sizeof(pin_error), "WRONG PIN");
                            pin_len = 0;
                            pin_buf[0] = '\0';
                        }
                    } else if (pin_purpose == 2) {
                        memcpy(pin_new_buf, pin_buf, 5);
                        reset_pin_entry();
                        pin_purpose = 3;
                    } else if (pin_purpose == 3) {
                        if (strcmp(pin_buf, pin_new_buf) == 0) {
                            if (pin_service_update(pin_old_buf, pin_new_buf) != 0) {
                                snprintf(pin_error, sizeof(pin_error), "PIN SAVE FAILED");
                                pin_len = 0; pin_buf[0] = '\0';
                                return SCREEN_SETTINGS;
                            }
                            memset(pin_old_buf, 0, sizeof(pin_old_buf));
                            memset(pin_new_buf, 0, sizeof(pin_new_buf));
                            reset_pin_entry();
                            mode = SETT_MODE_SECURITY;
                        } else {
                            snprintf(pin_error, sizeof(pin_error), "PINS DON'T MATCH");
                            pin_len = 0;
                            pin_buf[0] = '\0';
                            pin_purpose = 2;
                        }
                    }
                }
            }
            return SCREEN_SETTINGS;
    }
}

screen_id screen_settings_input(uint32_t key) {
    if (mode == SETT_MODE_POWEROFF_CONFIRM) {
        if (key == KEY_ACTION_PRIMARY || key == KEY_SOFT_RIGHT_ACTION || key == NCKEY_ENTER)
            s_poweroff_requested = 1;
        if (key == KEY_SOFT_LEFT_ACTION) mode = SETT_MODE_MAIN;
        return SCREEN_SETTINGS;
    }


    if (mode == SETT_MODE_PIN_ENTRY || mode == SETT_MODE_PIN_CHANGE) {
        return handle_pin_input(key);
    }

    if (mode == SETT_MODE_WIPE_RESULT) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_LEFT_ACTION:
            case KEY_SOFT_RIGHT_ACTION:
            case KEY_ACTION_PRIMARY:
            case KEY_ACTION_SECONDARY:
                mode = SETT_MODE_MAIN;
                s_selected = 0;
                return SCREEN_HOME;
            default:
                return SCREEN_SETTINGS;
        }
    }

    if (mode == SETT_MODE_WIPE_CONFIRM) {
        switch (key) {
            case NCKEY_LEFT:
            case NCKEY_UP:
                s_wipe_yes = 0;
                return SCREEN_SETTINGS;
            case NCKEY_RIGHT:
            case NCKEY_DOWN:
                s_wipe_yes = 1;
                return SCREEN_SETTINGS;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
            case KEY_ACTION_PRIMARY:         /* A = confirm the highlighted choice */
                if (s_wipe_yes) {
                    s_wipe_failed = (wipe_phone_data() == 0);
                    if (s_wipe_failed || s_wipe_corrupt_found) {
                        mode = SETT_MODE_WIPE_RESULT;
                        return SCREEN_SETTINGS;
                    }
                    mode = SETT_MODE_MAIN;
                    s_selected = 0;
                    return SCREEN_HOME;
                }
                mode = SETT_MODE_SECURITY;
                return SCREEN_SETTINGS;
            case KEY_SOFT_LEFT_ACTION:       /* Q = cancel */
                mode = SETT_MODE_SECURITY;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    if (mode == SETT_MODE_APPEARANCE) {
        int total = 2;
        switch (key) {
            case NCKEY_UP:
                if (s_sub_selected > 0) s_sub_selected--;
                return SCREEN_SETTINGS;
            case NCKEY_DOWN:
                if (s_sub_selected < total - 1) s_sub_selected++;
                return SCREEN_SETTINGS;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:      /* E = enter submenu (Themes) */
            case KEY_ACTION_PRIMARY:         /* A = select / toggle */
                if (s_sub_selected == APP_NIGHT_MODE) {
                    settings_service_toggle_by_key(SETTINGS_KEY_NIGHT_MODE);
                    theme_service_sync_from_settings();
                } else if (s_sub_selected == APP_THEME) {
                    return SCREEN_THEME;
                }
                return SCREEN_SETTINGS;
            case KEY_ACTION_SECONDARY:       /* D = reset to default (where applicable) */
                if (s_sub_selected == APP_NIGHT_MODE) {
                    settings_service_set_bool(SETTINGS_KEY_NIGHT_MODE,
                                              SETTINGS_DEFAULT_NIGHT_MODE);
                    theme_service_sync_from_settings();
                }
                return SCREEN_SETTINGS;
            case KEY_SOFT_LEFT_ACTION:       /* Q = back a level */
                mode = SETT_MODE_MAIN;
                s_sub_selected = 0;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    if (mode == SETT_MODE_SECURITY) {
        switch (key) {
            case NCKEY_UP:
                if (s_sub_selected > 0) s_sub_selected--;
                return SCREEN_SETTINGS;
            case NCKEY_DOWN:
                if (s_sub_selected < 2) s_sub_selected++;
                return SCREEN_SETTINGS;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:      /* E = enter submenu / select */
            case KEY_ACTION_PRIMARY:         /* A = select / toggle */
                if (s_sub_selected == SEC_HAND_WHITE) {
                    settings_service_toggle_by_key(SETTINGS_KEY_HAND_WHITE);
                    theme_service_sync_from_settings();
                } else if (s_sub_selected == SEC_WHITE_WIPE) {
                    reset_pin_entry();
                    pin_purpose = 0;
                    mode = SETT_MODE_PIN_ENTRY;
                } else if (s_sub_selected == SEC_UPDATE_PIN) {
                    reset_pin_entry();
                    pin_purpose = 1;
                    mode = SETT_MODE_PIN_CHANGE;
                }
                return SCREEN_SETTINGS;
            case KEY_ACTION_SECONDARY:       /* D = reset to default (where applicable) */
                if (s_sub_selected == SEC_HAND_WHITE) {
                    settings_service_set_bool(SETTINGS_KEY_HAND_WHITE,
                                              SETTINGS_DEFAULT_HAND_WHITE);
                    theme_service_sync_from_settings();
                }
                return SCREEN_SETTINGS;
            case KEY_SOFT_LEFT_ACTION:       /* Q = back a level */
                mode = SETT_MODE_MAIN;
                s_sub_selected = 0;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    if (mode == SETT_MODE_CONNECTIVITY) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:      /* E = enter submenu (Bluetooth) */
            case KEY_ACTION_PRIMARY:         /* A = select */
                return SCREEN_BLUETOOTH;
            case KEY_SOFT_LEFT_ACTION:       /* Q = back a level */
                mode = SETT_MODE_MAIN;
                return SCREEN_SETTINGS;
            /* D = reset has nothing applicable here (Bluetooth toggles live in
             * the Bluetooth screen), so it is a no-op. */
            default:
                return SCREEN_SETTINGS;
        }
    }

    if (mode == SETT_MODE_INFO) {
        switch (key) {
            case KEY_SOFT_LEFT_ACTION:
            case KEY_SOFT_RIGHT_ACTION:
            case KEY_ACTION_SECONDARY:
                mode = SETT_MODE_MAIN;
                return SCREEN_SETTINGS;
            default:
                return SCREEN_SETTINGS;
        }
    }

    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_SETTINGS;
        case NCKEY_DOWN:
            if (s_selected < MAIN_ITEM_COUNT - 1) s_selected++;
            return SCREEN_SETTINGS;
        case NCKEY_ENTER:
        case '\n':
        case KEY_SOFT_RIGHT_ACTION:      /* E = enter submenu */
        case KEY_ACTION_PRIMARY:         /* A = select */
            s_sub_selected = 0;
            switch (s_selected) {
                case 0: mode = SETT_MODE_APPEARANCE; break;
                case 1: mode = SETT_MODE_SECURITY; break;
                case 2: mode = SETT_MODE_CONNECTIVITY; break;
                case 3: mode = SETT_MODE_INFO; break;
                case 4: mode = SETT_MODE_POWEROFF_CONFIRM; s_poweroff_error = 0; break;
            }
            return SCREEN_SETTINGS;
        case KEY_SOFT_LEFT_ACTION:       /* Q = back a level (to Home) */
            return SCREEN_HOME;
        /* D = "reset to default" has no target on the top menu, so it does
         * nothing here (spec: "where applicable"). */
        default:
            return SCREEN_SETTINGS;
    }
}

int screen_settings_is_pin_entry_mode(void) {
    return (mode == SETT_MODE_PIN_ENTRY || mode == SETT_MODE_PIN_CHANGE) ? 1 : 0;
}

int screen_settings_take_poweroff_request(void) {
    int requested = s_poweroff_requested; s_poweroff_requested = 0; return requested;
}
void screen_settings_poweroff_failed(void) { s_poweroff_error = 1; }
