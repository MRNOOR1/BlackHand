#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "services/comm_service.h"
#include "services/contacts_service.h"
#include "services/theme_service.h"
#include "services/multitap_service.h"
#include "ui.h"

typedef enum {
    CONTACTS_MODE_LIST,
    CONTACTS_MODE_PROFILE,
    CONTACTS_MODE_EDIT,
} contacts_mode_t;

static int s_selected = 0;
static int s_scroll = 0;
static int s_action = 0; /* profile: 0=call 1=message */
static int s_delete_prompt = 0;
static contacts_mode_t s_mode = CONTACTS_MODE_LIST;

/* Edit form */
static int s_edit_existing = 0;
static int s_edit_field = 0; /* 0=name 1=number */
static char s_edit_id[128];
static char s_name[96];
static char s_number[48];
static multitap_state s_mt;
static int s_mt_ready = 0;

static void ensure_mt(void) {
    if (!s_mt_ready) {
        multitap_init(&s_mt);
        s_mt_ready = 1;
    }
}

static void draw_delete_popup(struct ncplane *phone) {
    ghost_confirm_popup(phone, "DELETE CONTACT?", 0);
}

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

static void begin_add_contact(void) {
    ensure_mt();
    s_edit_existing = 0;
    s_edit_field = 0;
    s_edit_id[0] = '\0';
    s_name[0] = '\0';
    s_number[0] = '\0';
    multitap_reset(&s_mt);
    s_mode = CONTACTS_MODE_EDIT;
}

static void begin_edit_contact(const Contact *c) {
    if (!c) return;
    ensure_mt();
    s_edit_existing = 1;
    snprintf(s_edit_id, sizeof(s_edit_id), "%s", c->id ? c->id : "");
    snprintf(s_name, sizeof(s_name), "%s", c->name ? c->name : "");
    snprintf(s_number, sizeof(s_number), "%s", c->phone_number ? c->phone_number : "");
    s_edit_field = 0;
    multitap_reset(&s_mt);
    s_mode = CONTACTS_MODE_EDIT;
}

static void save_contact(void) {
    if (s_name[0] == '\0') {
        snprintf(s_name, sizeof(s_name), "Unknown");
    }

    if (s_edit_existing) {
        const Contact *c = contact_service_get_by_id(s_edit_id);
        if (c) {
            contact_service_update((Contact *)c, s_name, s_number);
        }
    } else {
        contact_service_create(s_name, s_number);
        s_selected = 0;
        s_scroll = 0;
    }

    s_mode = CONTACTS_MODE_LIST;
}

void screen_contacts_draw(struct ncplane *phone) {
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width = INNER_WIDTH(cols);
    if (width < 10) return;

    size_t count = 0;
    const Contact **contacts = contact_service_list_all(&count);

    if (count == 0) {
        s_mode = (s_mode == CONTACTS_MODE_EDIT) ? CONTACTS_MODE_EDIT : CONTACTS_MODE_LIST;
    }

    if (s_mode == CONTACTS_MODE_EDIT) {
        int top = CONTENT_START_ROW + 3;
        ghost_text(phone, top, CONTENT_COL, theme_text_muted(),
                   s_edit_existing ? "EDIT CONTACT" : "NEW CONTACT");

        ncplane_set_fg_rgb(phone, (s_edit_field == 0) ? theme_selection_text() : theme_text_muted());
        ncplane_set_bg_rgb(phone, (s_edit_field == 0) ? theme_selection_bg() : theme_bg());
        ncplane_putstr_yx(phone, top + 2, CONTENT_COL, (s_edit_field == 0) ? MENU_CURSOR : MENU_CURSOR_BLANK);
        put_clipped(phone, top + 2, CONTENT_COL + 1, width - 2, s_name[0] ? s_name : "(name)");

        ncplane_set_fg_rgb(phone, (s_edit_field == 1) ? theme_selection_text() : theme_text_muted());
        ncplane_set_bg_rgb(phone, (s_edit_field == 1) ? theme_selection_bg() : theme_bg());
        ncplane_putstr_yx(phone, top + 4, CONTENT_COL, (s_edit_field == 1) ? MENU_CURSOR : MENU_CURSOR_BLANK);
        put_clipped(phone, top + 4, CONTENT_COL + 1, width - 2, s_number[0] ? s_number : "(number)");

        ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "2-9:ABC  #:Case  *:Punct");
        ghost_softkeys(phone, "[Cancel]", "[Save]");
        return;
    }

    if (s_mode == CONTACTS_MODE_PROFILE && count > 0) {
        if (s_selected < 0) s_selected = 0;
        if (s_selected >= (int)count) s_selected = (int)count - 1;

        const Contact *c = contacts[s_selected];
        const char *name = (c && c->name) ? c->name : "Unknown";
        const char *number = (c && c->phone_number) ? c->phone_number : "";

        int card_top = CONTENT_START_ROW + 1;
        int card_h = 8;
        if (card_top + card_h >= footer) card_h = footer - card_top - 1;
        if (card_h < 4) card_h = 4;

        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        for (int r = 0; r < card_h; r++) {
            for (int x = CONTENT_COL; x < CONTENT_COL + width; x++) {
                ncplane_putchar_yx(phone, card_top + r, x, ' ');
            }
        }

        const char *profile_rule = theme_rule_glyph();
        for (int x = 0; x < width; x++) {
            ncplane_putstr_yx(phone, card_top, CONTENT_COL + x, (profile_rule && profile_rule[0]) ? profile_rule : "-");
            ncplane_putstr_yx(phone, card_top + card_h - 1, CONTENT_COL + x, (profile_rule && profile_rule[0]) ? profile_rule : "-");
        }

        put_clipped(phone, card_top + 2, CONTENT_COL + 2, width - 4, "Name:");
        put_clipped(phone, card_top + 2, CONTENT_COL + 9, width - 11, name);
        put_clipped(phone, card_top + 3, CONTENT_COL + 2, width - 4, "Phone:");
        put_clipped(phone, card_top + 3, CONTENT_COL + 9, width - 11, number);

        ghost_softkeys(phone, NULL, NULL);
        ghost_text(phone, (int)footer - 1, CONTENT_COL, theme_text_muted(), "A:Call  D:SMS  E:Edit  Q:Back");
        return;
    }

    int list_start = CONTENT_START_ROW;
    int card_h = 3;
    int visible = (footer - list_start - 1) / card_h;
    if (visible < 1) visible = 1;

    if (count == 0) {
        s_selected = 0;
        s_scroll = 0;
    } else {
        if (s_selected < 0) s_selected = 0;
        if (s_selected >= (int)count) s_selected = (int)count - 1;
        if (s_selected < s_scroll) s_scroll = s_selected;
        if (s_selected >= s_scroll + visible) s_scroll = s_selected - visible + 1;
        if (s_scroll < 0) s_scroll = 0;
    }

    if (count == 0) {
        int mid = (list_start + footer - 1) / 2;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_putstr_yx(phone, mid, CONTENT_COL, "No contacts yet");
        ncplane_putstr_yx(phone, mid + 1, CONTENT_COL, "Press E to add");
    } else {
        for (int i = 0; i < visible; i++) {
            int idx = s_scroll + i;
            if (idx >= (int)count) break;

            const Contact *c = contacts[idx];
            int top = list_start + (i * card_h);
            int sel = (idx == s_selected);

            char nm[32]; snprintf(nm, sizeof(nm), "%.16s", c->name ? c->name : "");
            bh_list_item(phone, top, CONTENT_COL, width, nm, "", sel, idx);

            char ph[24]; snprintf(ph, sizeof(ph), "%.20s", c->phone_number ? c->phone_number : "");
            ghost_text(phone, top + 1, CONTENT_COL + 2, theme_dim(), ph);
        }
    }

    ghost_softkeys(phone, NULL, NULL);
    ghost_text(phone, (int)footer - 1, CONTENT_COL, theme_text_muted(), "A:Call  E:Details  D:Delete");
    if (s_delete_prompt && s_mode == CONTACTS_MODE_LIST) draw_delete_popup(phone);
}

screen_id screen_contacts_input(uint32_t key) {
    size_t count = 0;
    const Contact **contacts = contact_service_list_all(&count);

    if (s_mode == CONTACTS_MODE_EDIT) {
        ensure_mt();
        char *active = (s_edit_field == 0) ? s_name : s_number;
        size_t active_sz = (s_edit_field == 0) ? sizeof(s_name) : sizeof(s_number);

        switch (key) {
            case NCKEY_UP:
                s_edit_field = 0;
                multitap_set_field(&s_mt, s_edit_field);
                return SCREEN_CONTACTS;
            case NCKEY_DOWN:
                s_edit_field = 1;
                multitap_set_field(&s_mt, s_edit_field);
                return SCREEN_CONTACTS;
            case KEY_SOFT_LEFT_ACTION:
                multitap_reset(&s_mt);
                s_mode = CONTACTS_MODE_LIST;
                return SCREEN_CONTACTS;
            case KEY_SOFT_RIGHT_ACTION:
                multitap_reset(&s_mt);
                save_contact();
                return SCREEN_CONTACTS;
            case NCKEY_ENTER:
            case '\n':
                if (s_edit_field == 0) s_edit_field = 1;
                else save_contact();
                multitap_set_field(&s_mt, s_edit_field);
                return SCREEN_CONTACTS;
            case NCKEY_BACKSPACE:
            case 127:
                multitap_backspace(&s_mt, s_edit_field, active);
                return SCREEN_CONTACTS;
            case '#':
                multitap_toggle_case(&s_mt);
                return SCREEN_CONTACTS;
            default:
                if ((key >= '0' && key <= '9') || key == '*') {
                    multitap_apply_key(&s_mt, key, s_edit_field, active, active_sz);
                    return SCREEN_CONTACTS;
                }
                if (key >= 32 && key <= 126) {
                    multitap_reset(&s_mt);
                    size_t len = strlen(active);
                    if (len + 1 < active_sz) {
                        active[len] = (char)key;
                        active[len + 1] = '\0';
                    }
                }
                return SCREEN_CONTACTS;
        }
    }

    if (s_delete_prompt && s_mode == CONTACTS_MODE_LIST) {
        switch (key) {
            case NCKEY_ENTER:
            case '\n':
                /* A=Yes: delete contact. */
                if (count > 0) {
                    if (contacts && s_selected < (int)count && contacts[s_selected] && contacts[s_selected]->id) {
                        contact_service_delete(contacts[s_selected]->id);
                    }
                    contact_service_list_all(&count);
                    if (s_selected >= (int)count && s_selected > 0) s_selected--;
                }
                s_delete_prompt = 0;
                return SCREEN_CONTACTS;
            case KEY_SOFT_LEFT_ACTION:
                /* Q=No: cancel. */
                s_delete_prompt = 0;
                return SCREEN_CONTACTS;
            default:
                return SCREEN_CONTACTS;
        }
    }

    switch (key) {
        case NCKEY_UP:
            if (s_mode == CONTACTS_MODE_LIST && count > 0 && s_selected > 0)
                s_selected--;
            return SCREEN_CONTACTS;
        case NCKEY_DOWN:
            if (s_mode == CONTACTS_MODE_LIST && count > 0 && s_selected < (int)count - 1)
                s_selected++;
            return SCREEN_CONTACTS;
        case NCKEY_ENTER:
        case '\n':
            /* A: call in list; call in profile. */
            if (count > 0 && contacts && contacts[s_selected]) {
                const char *name = contacts[s_selected]->name ? contacts[s_selected]->name : "Unknown";
                comm_service_call_add(name, "outgoing");
                return SCREEN_CALLS;
            }
            return SCREEN_CONTACTS;
        case KEY_SOFT_RIGHT_ACTION:
            /* E: open profile from list; edit from profile; add new if empty. */
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (count > 0 && contacts && contacts[s_selected])
                    begin_edit_contact(contacts[s_selected]);
                return SCREEN_CONTACTS;
            }
            if (count > 0) {
                s_mode = CONTACTS_MODE_PROFILE;
            } else {
                begin_add_contact();
            }
            return SCREEN_CONTACTS;
        case NCKEY_LEFT:
            /* D: in profile — send SMS; in list — delete confirm. */
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (count > 0 && contacts && contacts[s_selected]) {
                    const char *name = contacts[s_selected]->name ? contacts[s_selected]->name : "Unknown";
                    char body[64];
                    snprintf(body, sizeof(body), "Hey %s", name);
                    comm_service_message_add(name, body);
                    return SCREEN_MESSAGES;
                }
                return SCREEN_CONTACTS;
            }
            if (count > 0) {
                s_delete_prompt = 1;
            }
            return SCREEN_CONTACTS;
        case KEY_SOFT_LEFT_ACTION:
            /* Q: back to list from profile; back to home from list. */
            if (s_mode == CONTACTS_MODE_PROFILE) {
                s_mode = CONTACTS_MODE_LIST;
                return SCREEN_CONTACTS;
            }
            return SCREEN_HOME;
        default:
            return SCREEN_CONTACTS;
    }
}

int screen_contacts_is_edit_mode(void) {
    return (s_mode == CONTACTS_MODE_EDIT) ? 1 : 0;
}
