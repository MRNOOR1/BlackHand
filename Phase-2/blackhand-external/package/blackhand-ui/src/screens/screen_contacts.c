#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "services/comm_service.h"
#include "services/contacts_service.h"
#include "services/theme_service.h"
#include "services/multitap_service.h"
#include "ui-ipcs/storage_ipc.h"
#include "country_codes.h"
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
static int s_delete_yes = 0;
static contacts_mode_t s_mode = CONTACTS_MODE_LIST;

/* Edit form — fields: 0=name, 1=country (left/right cycles), 2=number */
static int s_edit_existing = 0;
static int s_edit_field = 0;
static char s_edit_id[128];
static char s_name[96];
static char s_number[48];
static multitap_state s_mt;
static int s_mt_ready = 0;

/* Country for number normalisation. Persisted in settings.json so the
 * last-used country is the default next time (and across reboots). */
static int s_country_idx = -1;

static void ensure_country(void)
{
    if (s_country_idx >= 0) return;
    size_t n = 0;
    const CountryCode *tab = country_table(&n);
    s_country_idx = 0;
    char iso[8];
    if (storage_ipc_settings_get_str("country_iso", iso, sizeof(iso)) == 0) {
        for (size_t i = 0; i < n; i++)
            if (strcmp(tab[i].iso, iso) == 0) { s_country_idx = (int)i; break; }
    }
}

static const CountryCode *current_country(void)
{
    ensure_country();
    size_t n = 0;
    const CountryCode *tab = country_table(&n);
    if (s_country_idx < 0 || (size_t)s_country_idx >= n) s_country_idx = 0;
    return &tab[s_country_idx];
}

static void cycle_country(int delta)
{
    size_t n = 0;
    country_table(&n);
    ensure_country();
    s_country_idx = (s_country_idx + delta + (int)n) % (int)n;
    storage_ipc_settings_set_str("country_iso", current_country()->iso);
}

static void ensure_mt(void) {
    if (!s_mt_ready) {
        multitap_init(&s_mt);
        s_mt_ready = 1;
    }
}

static void draw_delete_popup(struct ncplane *phone) {
    ghost_confirm_popup(phone, "DELETE CONTACT?", s_delete_yes);
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

    /* Normalise to E.164 with the selected country: "0413 805 252" + AU
     * → "+61413805252". ATD can dial the stored form directly, and the
     * history folder under /data/numbers/ uses the same key. */
    char e164[48];
    if (phone_normalize(s_number, current_country(), e164, sizeof(e164)) != 0)
        snprintf(e164, sizeof(e164), "%s", s_number);   /* keep raw on failure */

    if (s_edit_existing) {
        const Contact *c = contact_service_get_by_id(s_edit_id);
        if (c) {
            contact_service_update((Contact *)c, s_name, e164);
        }
    } else {
        contact_service_create(s_name, e164);
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

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "CONTACTS");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");

    if (count == 0) {
        s_mode = (s_mode == CONTACTS_MODE_EDIT) ? CONTACTS_MODE_EDIT : CONTACTS_MODE_LIST;
    }

    if (s_mode == CONTACTS_MODE_EDIT) {
        int top = CONTENT_START_ROW + 3;
        ghost_text(phone, top, CONTENT_COL, theme_text_muted(),
                   s_edit_existing ? "EDIT CONTACT" : "NEW CONTACT");

        /* field 0: name */
        ncplane_set_fg_rgb(phone, (s_edit_field == 0) ? theme_selection_text() : theme_text_muted());
        ncplane_set_bg_rgb(phone, (s_edit_field == 0) ? theme_selection_bg() : theme_bg());
        ncplane_putstr_yx(phone, top + 2, CONTENT_COL, (s_edit_field == 0) ? MENU_CURSOR : MENU_CURSOR_BLANK);
        put_clipped(phone, top + 2, CONTENT_COL + 1, width - 2, s_name[0] ? s_name : "(name)");

        /* field 1: country — Left/Right cycles, defines normalisation */
        {
            const CountryCode *cc = current_country();
            char cline[80];
            snprintf(cline, sizeof(cline), "< %s %s >", cc->name, cc->code);
            ncplane_set_fg_rgb(phone, (s_edit_field == 1) ? theme_selection_text() : theme_text_muted());
            ncplane_set_bg_rgb(phone, (s_edit_field == 1) ? theme_selection_bg() : theme_bg());
            ncplane_putstr_yx(phone, top + 4, CONTENT_COL, (s_edit_field == 1) ? MENU_CURSOR : MENU_CURSOR_BLANK);
            put_clipped(phone, top + 4, CONTENT_COL + 1, width - 2, cline);
        }

        /* field 2: number (raw — normalised on save) */
        ncplane_set_fg_rgb(phone, (s_edit_field == 2) ? theme_selection_text() : theme_text_muted());
        ncplane_set_bg_rgb(phone, (s_edit_field == 2) ? theme_selection_bg() : theme_bg());
        ncplane_putstr_yx(phone, top + 6, CONTENT_COL, (s_edit_field == 2) ? MENU_CURSOR : MENU_CURSOR_BLANK);
        put_clipped(phone, top + 6, CONTENT_COL + 1, width - 2, s_number[0] ? s_number : "(number)");

        /* live preview of what will be stored */
        if (s_number[0]) {
            char e164[48];
            if (phone_normalize(s_number, current_country(), e164, sizeof(e164)) == 0) {
                char prev[64];
                snprintf(prev, sizeof(prev), "Saves as: %s", e164);
                ghost_text(phone, top + 8, CONTENT_COL + 1, theme_text_muted(), prev);
            }
        }

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

        int card_top = CONTENT_START_ROW + 4;
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

        for (int x = 0; x < width; x++) {
            ncplane_putstr_yx(phone, card_top, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");
            ncplane_putstr_yx(phone, card_top + card_h - 1, CONTENT_COL + x, (rule && rule[0]) ? rule : "-");
        }

        put_clipped(phone, card_top + 2, CONTENT_COL + 2, width - 4, "Name:");
        put_clipped(phone, card_top + 2, CONTENT_COL + 9, width - 11, name);
        put_clipped(phone, card_top + 3, CONTENT_COL + 2, width - 4, "Phone:");
        put_clipped(phone, card_top + 3, CONTENT_COL + 9, width - 11, number);

        ghost_text(phone, card_top + 4, CONTENT_COL + 2, theme_text_muted(),
                   s_action == 0 ? "> CALL" : "  CALL");
        ghost_text(phone, card_top + 5, CONTENT_COL + 2, theme_text_muted(),
                   s_action == 1 ? "> MESSAGE" : "  MESSAGE");

        ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "Up/Down:Action  Enter:Send");
        ghost_softkeys(phone, "[Back]", "[Edit]");
        return;
    }

    int list_start = CONTENT_START_ROW + 4;
    int card_h = 3;
    int visible = (footer - list_start) / card_h;
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
        int mid = (list_start + footer) / 2;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());
        ncplane_putstr_yx(phone, mid, CONTENT_COL, "No contacts yet");
        ncplane_putstr_yx(phone, mid + 1, CONTENT_COL, "Press Right Soft to add");
    } else {
        for (int i = 0; i < visible; i++) {
            int idx = s_scroll + i;
            if (idx >= (int)count) break;

            const Contact *c = contacts[idx];
            int top = list_start + (i * card_h);
            int sel = (idx == s_selected);

            /* blueprint CNTC: name rows with first-letter group marker on
             * group change (right column). Themed selection style. */
            char label[20], meta[4] = "";
            snprintf(label, sizeof(label), "%.14s", c->name ? c->name : "");
            char first = (c->name && c->name[0]) ? c->name[0] : ' ';
            if (first >= 'a' && first <= 'z') first -= 32;
            if (idx == 0) {
                meta[0] = first; meta[1] = '\0';
            } else {
                const Contact *prev = contacts[idx - 1];
                char pf = (prev && prev->name && prev->name[0]) ? prev->name[0] : ' ';
                if (pf >= 'a' && pf <= 'z') pf -= 32;
                if (pf != first) { meta[0] = first; meta[1] = '\0'; }
            }
            ghost_list_row_meta(phone, top, (int)cols, idx, sel, label, meta);
            (void)card_h;
        }
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "Left:Delete  Enter:Open");
    ghost_softkeys(phone, "[Back]", "[Add]");
    if (s_delete_prompt && s_mode == CONTACTS_MODE_LIST) draw_delete_popup(phone);
}

screen_id screen_contacts_input(uint32_t key) {
    size_t count = 0;
    const Contact **contacts = contact_service_list_all(&count);

    if (s_mode == CONTACTS_MODE_EDIT) {
        ensure_mt();
        /* fields: 0=name (text), 1=country (selector), 2=number (text).
         * multitap only knows the two text fields: slot 0=name, slot 1=number. */
        int    is_text    = (s_edit_field != 1);
        int    mt_slot    = (s_edit_field == 0) ? 0 : 1;
        char  *active     = (s_edit_field == 0) ? s_name : s_number;
        size_t active_sz  = (s_edit_field == 0) ? sizeof(s_name) : sizeof(s_number);

        switch (key) {
            case NCKEY_UP:
                if (s_edit_field > 0) s_edit_field--;
                multitap_set_field(&s_mt, (s_edit_field == 0) ? 0 : 1);
                return SCREEN_CONTACTS;
            case NCKEY_DOWN:
                if (s_edit_field < 2) s_edit_field++;
                multitap_set_field(&s_mt, (s_edit_field == 0) ? 0 : 1);
                return SCREEN_CONTACTS;
            case NCKEY_LEFT:
                if (s_edit_field == 1) cycle_country(-1);
                return SCREEN_CONTACTS;
            case NCKEY_RIGHT:
                if (s_edit_field == 1) cycle_country(+1);
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
                if (s_edit_field < 2) s_edit_field++;
                else save_contact();
                multitap_set_field(&s_mt, (s_edit_field == 0) ? 0 : 1);
                return SCREEN_CONTACTS;
            case NCKEY_BACKSPACE:
            case 127:
                if (is_text) multitap_backspace(&s_mt, mt_slot, active);
                return SCREEN_CONTACTS;
            case '#':
                if (is_text) multitap_toggle_case(&s_mt);
                return SCREEN_CONTACTS;
            default:
                if (!is_text) return SCREEN_CONTACTS;
                if ((key >= '0' && key <= '9') || key == '*') {
                    multitap_apply_key(&s_mt, key, mt_slot, active, active_sz);
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
            case NCKEY_LEFT:
            case NCKEY_UP:
                s_delete_yes = 0;
                return SCREEN_CONTACTS;
            case NCKEY_RIGHT:
            case NCKEY_DOWN:
                s_delete_yes = 1;
                return SCREEN_CONTACTS;
            case NCKEY_ENTER:
            case '\n':
            case KEY_SOFT_RIGHT_ACTION:
                if (s_delete_yes && count > 0) {
                    if (contacts && s_selected < (int)count && contacts[s_selected] && contacts[s_selected]->id) {
                        contact_service_delete(contacts[s_selected]->id);
                    }
                    contact_service_list_all(&count);
                    if (s_selected >= (int)count && s_selected > 0) s_selected--;
                }
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_CONTACTS;
            case KEY_SOFT_LEFT_ACTION:
                s_delete_prompt = 0;
                s_delete_yes = 0;
                return SCREEN_CONTACTS;
            default:
                return SCREEN_CONTACTS;
        }
    }

    switch (key) {
        case NCKEY_UP:
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (s_action > 0) s_action--;
            } else if (count > 0 && s_selected > 0) {
                s_selected--;
            }
            return SCREEN_CONTACTS;
        case NCKEY_DOWN:
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (s_action < 1) s_action++;
            } else if (count > 0 && s_selected < (int)count - 1) {
                s_selected++;
            }
            return SCREEN_CONTACTS;
        case NCKEY_RIGHT:
        case KEY_SOFT_RIGHT_ACTION: {
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (count > 0 && contacts && contacts[s_selected]) {
                    begin_edit_contact(contacts[s_selected]);
                }
                return SCREEN_CONTACTS;
            }
            begin_add_contact();
            return SCREEN_CONTACTS;
        }
        case NCKEY_ENTER:
        case '\n': {
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (count > 0 && contacts && contacts[s_selected]) {
                    const char *name  = contacts[s_selected]->name         ? contacts[s_selected]->name         : "Unknown";
                    const char *phone = contacts[s_selected]->phone_number ? contacts[s_selected]->phone_number : "";
                    if (s_action == 0) {
                        // Dial via modem; screen_calls handles the rest.
                        screen_calls_start_outgoing(name, phone);
                        return SCREEN_CALLS;
                    }
                    // Message action: open compose with number pre-filled.
                    screen_messages_start_compose(phone);
                    return SCREEN_MESSAGES;
                }
                return SCREEN_CONTACTS;
            }
            if (count > 0) {
                s_mode = CONTACTS_MODE_PROFILE;
                s_action = 0;
            }
            return SCREEN_CONTACTS;
        }
        case NCKEY_LEFT:
            if (s_mode == CONTACTS_MODE_PROFILE) {
                if (s_action > 0) s_action--;
                return SCREEN_CONTACTS;
            }
            if (count == 0) return SCREEN_CONTACTS;
            s_delete_prompt = 1;
            s_delete_yes = 0;
            return SCREEN_CONTACTS;
        case KEY_SOFT_LEFT_ACTION:
            if (s_mode == CONTACTS_MODE_PROFILE) {
                s_mode = CONTACTS_MODE_LIST;
                s_action = 0;
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
