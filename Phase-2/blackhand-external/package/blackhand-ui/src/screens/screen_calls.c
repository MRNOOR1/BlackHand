#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "draw_utils.h"
#include "services/comm_service.h"
#include "services/contacts_service.h"
#include "services/theme_service.h"
#include "ui-ipcs/modem_ipc.h"
#include "ui-ipcs/storage_ipc.h"
#include "country_codes.h"
#include "ui.h"

typedef enum {
    CALLS_LOG,
    CALLS_DIAL,        /* entering a number on the dial pad */
    CALLS_INCOMING,
    CALLS_DIALING,
    CALLS_ACTIVE,
} calls_state_t;

static calls_state_t s_state      = CALLS_LOG;
static int           s_selected   = 0;
static int           s_scroll     = 0;
static int           s_delete_prompt = 0;
static int           s_delete_yes = 0;

static char    s_call_number[32]  = "";
static char    s_call_name[48]    = "";
static char    s_call_dir[12]     = "outgoing"; /* "outgoing" | "incoming" */
static time_t  s_call_start       = 0;

static char    s_dial_buffer[32]  = "";   /* digits being typed in CALLS_DIAL */

/* Action-cell focus on decision screens: 0 = affirm, 1 = destruct. */
static int s_act_focus = 0;


/* ── helpers ─────────────────────────────────────────────────────────────── */

static void enter_log(void)
{
    s_state = CALLS_LOG;
    s_call_number[0] = '\0';
    s_call_name[0]   = '\0';
    s_call_start     = 0;
}

static void enter_dial(char first_digit)
{
    s_state = CALLS_DIAL;
    s_act_focus = 0;
    s_dial_buffer[0] = '\0';
    if (first_digit) {
        s_dial_buffer[0] = first_digit;
        s_dial_buffer[1] = '\0';
    }
}

/* +61413805252 → "+61 4·· ··· ··2" — blueprints §1 rule 6.
 * Keeps country code + first subscriber digit + last digit; masks the rest
 * with U+00B7 (proper UTF-8, "·" is two bytes). */
static void mask_number(const char *num, char *out, size_t out_sz)
{
    out[0] = '\0';
    if (!num || !num[0]) return;
    size_t len = strlen(num), w = 0;
    for (size_t i = 0; i < len; i++) {
        if (i < 5 || i + 1 >= len) {
            if (w + 2 >= out_sz) break;
            out[w++] = num[i];
        } else {
            if (w + 3 >= out_sz) break;
            out[w++] = '\xc2';   /* U+00B7 · */
            out[w++] = '\xb7';
        }
        if ((i == 2 || i == 5 || i == 8) && w + 2 < out_sz)
            out[w++] = ' ';
    }
    out[w] = '\0';
}

static int is_dial_char(uint32_t key)
{
    return (key >= '0' && key <= '9') || key == '+' || key == '*' || key == '#';
}

int screen_calls_is_dial_mode(void)
{
    return s_state == CALLS_DIAL ? 1 : 0;
}

static void draw_delete_popup(struct ncplane *phone)
{
    ghost_confirm_popup(phone, "DELETE ENTRY?", s_delete_yes);
}

static void lookup_name_by_phone(const char *phone, char *out, size_t out_sz)
{
    out[0] = '\0';
    if (!phone || !phone[0] || out_sz == 0) return;
    size_t count = 0;
    const Contact **all = contact_service_list_all(&count);
    for (size_t i = 0; i < count; i++) {
        if (all && all[i] && all[i]->phone_number &&
            strcmp(all[i]->phone_number, phone) == 0) {
            snprintf(out, out_sz, "%s", all[i]->name ? all[i]->name : "");
            return;
        }
    }
}

/* ── public entry points (called from other screens / main loop) ─────────── */

/* Normalise whatever the user typed to E.164 using the country selected in
 * the contacts editor (persisted in settings.json). "0413 805 252" with AU
 * becomes "+61413805252" — directly dialable and the same key the storage
 * layer files history under. Already-international input passes through. */
static void normalize_dial(const char *raw, char *out, size_t out_sz)
{
    const CountryCode *cc = NULL;
    char iso[8];
    if (storage_ipc_settings_get_str("country_iso", iso, sizeof(iso)) == 0)
        cc = country_by_iso(iso);
    if (phone_normalize(raw, cc, out, out_sz) != 0)
        snprintf(out, out_sz, "%s", raw);
}

void screen_calls_start_outgoing(const char *name, const char *phone)
{
    if (!phone || !phone[0]) return;
    normalize_dial(phone, s_call_number, sizeof(s_call_number));
    if (name && name[0])
        snprintf(s_call_name, sizeof(s_call_name), "%s", name);
    else
        lookup_name_by_phone(phone, s_call_name, sizeof(s_call_name));

    if (modem_ipc_dial(s_call_number) == 0) {
        s_state      = CALLS_DIALING;
        s_call_start = 0;
        snprintf(s_call_dir, sizeof(s_call_dir), "outgoing");
    } else {
        // Dial failed — fall back to log so the user isn't stuck on a fake "calling" screen.
        enter_log();
    }
}

void screen_calls_set_incoming(const char *phone)
{
    // Already showing this (or another) live call — don't yank the UI out
    // from under the user. has_incoming_call stays true the whole time the
    // modem rings, so the main loop may call this repeatedly.
    if (s_state == CALLS_INCOMING || s_state == CALLS_ACTIVE) return;
    snprintf(s_call_number, sizeof(s_call_number), "%s", phone ? phone : "");
    lookup_name_by_phone(s_call_number, s_call_name, sizeof(s_call_name));
    snprintf(s_call_dir, sizeof(s_call_dir), "incoming");
    s_act_focus  = 0;   /* ACPT focused first */
    s_state      = CALLS_INCOMING;
    s_call_start = 0;
}

void screen_calls_tick(void)
{
    // Only the live-call states care about modem-driven transitions.
    // Throttle so we don't AT+CSQ on every frame — main loop runs ~30fps.
    static int sample_tick = 0;
    if (s_state != CALLS_DIALING && s_state != CALLS_ACTIVE &&
        s_state != CALLS_INCOMING) return;
    if (!modem_ipc_is_online()) return;
    if (++sample_tick < 30) return;
    sample_tick = 0;

    ModemStatus st;
    if (modem_ipc_get_status(&st) != 0) return;

    if (s_state == CALLS_INCOMING) {
        // Caller gave up before we answered — log a missed call and leave
        // the ringing screen instead of showing INCOMING forever.
        if (strcmp(st.call_state, "idle") == 0) {
            comm_service_call_add_full(s_call_name, s_call_number, "missed", 0);
            enter_log();
        }
        return;
    }

    if (s_state == CALLS_DIALING && strcmp(st.call_state, "active") == 0) {
        s_state      = CALLS_ACTIVE;
        s_call_start = time(NULL);
    } else if (strcmp(st.call_state, "idle") == 0) {
        // Remote hangup, BUSY, NO CARRIER — persist + return to log.
        int dur = (s_call_start > 0) ? (int)(time(NULL) - s_call_start) : 0;
        comm_service_call_add_full(
            s_call_name, s_call_number, s_call_dir, dur);
        enter_log();
    }
}

/* ── draw ─────────────────────────────────────────────────────────────────── */

void screen_calls_draw(struct ncplane *phone)
{
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width  = INNER_WIDTH(cols);
    if (width < 10) return;

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "CALLS");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x,
                          (rule && rule[0]) ? rule : "-");

    /* ── dialer (blueprint DIAL) ── */
    if (s_state == CALLS_DIAL) {
        int top = CONTENT_START_ROW + 2;

        /* label, dim, letterspaced, centered */
        const char *lbl = "E N T E R  N O .";
        ghost_text(phone, top, ((int)cols - 16) / 2, theme_border(), lbl);

        /* the number, bright, centered, trailing cursor */
        char numline[40];
        snprintf(numline, sizeof(numline), "%s_", s_dial_buffer);
        int ncol = ((int)cols - (int)strlen(numline)) / 2;
        if (ncol < CONTENT_COL) ncol = CONTENT_COL;
        ghost_text(phone, top + 3, ncol, theme_text_primary(), numline);

        /* live contact match, updates per digit */
        if (s_dial_buffer[0]) {
            size_t cc = 0;
            const Contact **all = contact_service_list_all(&cc);
            for (size_t i = 0; i < cc; i++) {
                if (all && all[i] && all[i]->phone_number &&
                    strstr(all[i]->phone_number, s_dial_buffer)) {
                    char match[40];
                    snprintf(match, sizeof(match), "→ %.20s", all[i]->name);
                    ghost_text(phone, top + 6,
                               ((int)cols - (int)strlen(match)) / 2,
                               theme_text_muted(), match);
                    break;
                }
            }
        }

        ghost_action_cells(phone, "CALL", "CLR", s_act_focus);
        return;
    }

    /* ── incoming call (blueprint CALL — overrides any screen) ── */
    if (s_state == CALLS_INCOMING) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;

        const char *lbl = "I N C O M I N G";
        ghost_text(phone, mid - 4, ((int)cols - 15) / 2, theme_text_muted(), lbl);

        const char *who = s_call_name[0] ? s_call_name : "UNKNOWN";
        ghost_text(phone, mid - 2, ((int)cols - (int)strlen(who)) / 2,
                   theme_text_primary(), who);

        char masked[40];
        mask_number(s_call_number, masked, sizeof(masked));
        if (masked[0])
            ghost_text(phone, mid, ((int)cols - (int)strlen(masked)) / 2,
                       theme_border(), masked);

        ghost_action_cells(phone, "ACPT", "REJ", s_act_focus);
        return;
    }

    /* ── dialing / live call (blueprint LIVE) ── */
    if (s_state == CALLS_DIALING || s_state == CALLS_ACTIVE) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;

        char lbl[24];
        if (s_state == CALLS_ACTIVE && s_call_start > 0) {
            time_t el = time(NULL) - s_call_start;
            snprintf(lbl, sizeof(lbl), "LIVE · %02d:%02d",
                     (int)(el / 60), (int)(el % 60));
        } else {
            snprintf(lbl, sizeof(lbl), "CALLING···");
        }
        ghost_text(phone, mid - 4, ((int)cols - (int)strlen(lbl)) / 2,
                   theme_text_muted(), lbl);

        const char *who = s_call_name[0] ? s_call_name : "UNKNOWN";
        ghost_text(phone, mid - 2, ((int)cols - (int)strlen(who)) / 2,
                   theme_text_primary(), who);

        char masked[40];
        mask_number(s_call_number, masked, sizeof(masked));
        if (masked[0])
            ghost_text(phone, mid, ((int)cols - (int)strlen(masked)) / 2,
                       theme_border(), masked);

        /* single destructive cell — END is the only decision here */
        ghost_action_cells(phone, "", "END", 1);
        return;
    }

    /* ── call log ── */
    if (!comm_service_storage_ok()) {
        ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL,
                   theme_text_primary(), "! STORAGE OFFLINE");
        ghost_text(phone, CONTENT_START_ROW + 3, CONTENT_COL,
                   theme_text_muted(), "history unavailable (bh-storage down)");
    } else if (!modem_ipc_is_online()) {
        ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL,
                   theme_text_primary(), "! MODEM OFFLINE");
        ghost_text(phone, CONTENT_START_ROW + 3, CONTENT_COL,
                   theme_text_muted(), modem_ipc_health_error());
    }

    size_t count = comm_service_call_count();
    if (s_selected < 0) s_selected = 0;
    if (count > 0 && s_selected >= (int)count) s_selected = (int)count - 1;

    int list_start = CONTENT_START_ROW + 4;
    int visible    = footer - list_start - 1;
    if (visible < 1) visible = 1;

    if (count == 0) { s_selected = 0; s_scroll = 0; }
    if (s_selected < s_scroll) s_scroll = s_selected;
    if (s_selected >= s_scroll + visible) s_scroll = s_selected - visible + 1;
    if (s_scroll < 0) s_scroll = 0;

    if (count == 0) {
        ghost_text(phone, list_start,     CONTENT_COL, theme_text_muted(), "No call history yet");
        ghost_text(phone, list_start + 1, CONTENT_COL, theme_text_muted(), "Open a contact to call");
        ghost_softkeys(phone, "[Back]", "");
        return;
    }

    for (int i = 0; i < visible; i++) {
        int idx = s_scroll + i;
        if (idx >= (int)count) break;
        const CommCall *call = comm_service_call_at((size_t)idx);
        if (!call) continue;

        int row = list_start + i;
        if (row >= footer) break;

        /* NAME (icon-prefixed, truncated) · time — themed selection style */
        char label[24];
        snprintf(label, sizeof(label), "%s %.10s", call->icon, call->name);
        ghost_list_row_meta(phone, row, (int)cols, idx,
                            (idx == s_selected), label, call->time);
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(),
               "Left:Del  Enter:Call");
    ghost_softkeys(phone, "[Back]", "");
    if (s_delete_prompt) draw_delete_popup(phone);
}

/* ── input ────────────────────────────────────────────────────────────────── */

screen_id screen_calls_input(uint32_t key)
{
    /* ── dialer ── */
    if (s_state == CALLS_DIAL) {
        switch (key) {
            case KEY_SOFT_LEFT_ACTION:
                enter_log();
                return SCREEN_CALLS;
            case NCKEY_LEFT:
                s_act_focus = 0;            /* CALL */
                return SCREEN_CALLS;
            case NCKEY_RIGHT:
                s_act_focus = 1;            /* CLR  */
                return SCREEN_CALLS;
            case NCKEY_ENTER: case '\n':
                if (s_act_focus == 1) {     /* CLR clears all */
                    s_dial_buffer[0] = '\0';
                    s_act_focus = 0;
                } else if (s_dial_buffer[0]) {
                    screen_calls_start_outgoing("", s_dial_buffer);
                }
                return SCREEN_CALLS;
            case KEY_SOFT_RIGHT_ACTION:     /* E = call, always */
                if (s_dial_buffer[0]) {
                    screen_calls_start_outgoing("", s_dial_buffer);
                }
                return SCREEN_CALLS;
            case NCKEY_BACKSPACE: case 127: case '\b': {
                size_t len = strlen(s_dial_buffer);
                if (len > 0) s_dial_buffer[len - 1] = '\0';
                return SCREEN_CALLS;
            }
            default:
                if (is_dial_char(key)) {
                    size_t len = strlen(s_dial_buffer);
                    if (len + 1 < sizeof(s_dial_buffer)) {
                        s_dial_buffer[len]     = (char)key;
                        s_dial_buffer[len + 1] = '\0';
                    }
                }
                return SCREEN_CALLS;
        }
    }

    /* ── incoming call: A/D move focus between ACPT/REJ, CENTER commits.
     *    Q = reject and E = accept remain direct shortcuts. ── */
    if (s_state == CALLS_INCOMING) {
        switch (key) {
            case NCKEY_LEFT:
                s_act_focus = 0;            /* ACPT */
                return SCREEN_CALLS;
            case NCKEY_RIGHT:
                s_act_focus = 1;            /* REJ  */
                return SCREEN_CALLS;
            case NCKEY_ENTER: case '\n':
                if (s_act_focus == 1) {
                    modem_ipc_reject();
                    enter_log();
                } else if (modem_ipc_answer() == 0) {
                    s_state      = CALLS_ACTIVE;
                    s_call_start = time(NULL);
                }
                return SCREEN_CALLS;
            case KEY_SOFT_LEFT_ACTION:
                modem_ipc_reject();
                enter_log();
                return SCREEN_CALLS;
            case KEY_SOFT_RIGHT_ACTION:
                if (modem_ipc_answer() == 0) {
                    s_state      = CALLS_ACTIVE;
                    s_call_start = time(NULL);
                }
                return SCREEN_CALLS;
            default:
                return SCREEN_CALLS;
        }
    }

    /* ── dialing / active call ── */
    if (s_state == CALLS_DIALING || s_state == CALLS_ACTIVE) {
        switch (key) {
            case KEY_SOFT_LEFT_ACTION:
            case NCKEY_LEFT:
            case NCKEY_ENTER: case '\n': {
                int dur = (s_call_start > 0) ? (int)(time(NULL) - s_call_start) : 0;
                modem_ipc_hangup();
                comm_service_call_add_full(
                    s_call_name, s_call_number, s_call_dir, dur);
                enter_log();
                return SCREEN_CALLS;
            }
            default:
                // screen_calls_tick() handles auto-transitions; ignore other keys.
                return SCREEN_CALLS;
        }
    }

    /* ── delete prompt ── */
    if (s_delete_prompt) {
        switch (key) {
            case NCKEY_LEFT: case NCKEY_UP:
                s_delete_yes = 0; return SCREEN_CALLS;
            case NCKEY_RIGHT: case NCKEY_DOWN:
                s_delete_yes = 1; return SCREEN_CALLS;
            case NCKEY_ENTER: case '\n': case KEY_SOFT_RIGHT_ACTION:
                if (s_delete_yes && comm_service_call_count() > 0) {
                    comm_service_call_delete((size_t)s_selected);
                    if (s_selected >= (int)comm_service_call_count() && s_selected > 0)
                        s_selected--;
                }
                s_delete_prompt = 0; s_delete_yes = 0;
                return SCREEN_CALLS;
            case KEY_SOFT_LEFT_ACTION:
                s_delete_prompt = 0; s_delete_yes = 0;
                return SCREEN_CALLS;
            default:
                return SCREEN_CALLS;
        }
    }

    /* ── call log ── */
    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_CALLS;
        case NCKEY_DOWN:
            if (s_selected < (int)comm_service_call_count() - 1) s_selected++;
            return SCREEN_CALLS;
        case NCKEY_ENTER: case '\n': {
            /* Dial the selected log entry. */
            const CommCall *c = comm_service_call_at((size_t)s_selected);
            if (c && c->phone[0]) {
                screen_calls_start_outgoing(c->name, c->phone);
            }
            return SCREEN_CALLS;
        }
        case NCKEY_LEFT:
            if (comm_service_call_count() > 0) {
                s_delete_prompt = 1;
                s_delete_yes    = 0;
            }
            return SCREEN_CALLS;
        case KEY_SOFT_LEFT_ACTION:
            return SCREEN_HOME;
        case KEY_SOFT_RIGHT_ACTION:
            /* Right soft from the log opens the dial pad with an empty buffer. */
            enter_dial(0);
            return SCREEN_CALLS;
        default:
            /* Typing any digit / + / * / # from the log auto-enters the dial
             * pad with that character as the first digit — dumbphone idiom.
             * (Main loop polls modem_status for incoming calls.) */
            if (is_dial_char(key)) {
                enter_dial((char)key);
            }
            return SCREEN_CALLS;
    }
}
