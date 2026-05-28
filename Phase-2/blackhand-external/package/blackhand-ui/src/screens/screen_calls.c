#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "draw_utils.h"
#include "services/comm_service.h"
#include "services/theme_service.h"
#include "ui-ipcs/modem_ipc.h"
#include "ui.h"

typedef enum {
    CALLS_LOG,
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
static time_t  s_call_start       = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void enter_log(void)
{
    s_state = CALLS_LOG;
    s_call_number[0] = '\0';
    s_call_name[0]   = '\0';
    s_call_start     = 0;
}

static void draw_delete_popup(struct ncplane *phone)
{
    ghost_confirm_popup(phone, "DELETE ENTRY?", s_delete_yes);
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

    /* ── incoming call overlay ── */
    if (s_state == CALLS_INCOMING) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;
        ghost_text(phone, mid - 3, CONTENT_COL, theme_text_primary(), "INCOMING CALL");
        ghost_text(phone, mid - 1, CONTENT_COL, theme_text_primary(), s_call_name[0]
                   ? s_call_name : "Unknown");
        ghost_text(phone, mid,     CONTENT_COL, theme_text_muted(),   s_call_number);
        ghost_text(phone, mid + 2, CONTENT_COL, theme_text_muted(),   "↙ Ringing...");
        ghost_softkeys(phone, "[Reject]", "[Answer]");
        return;
    }

    /* ── dialing / active call overlay ── */
    if (s_state == CALLS_DIALING || s_state == CALLS_ACTIVE) {
        int mid = (CONTENT_START_ROW + 2 + footer) / 2;
        ghost_text(phone, mid - 3, CONTENT_COL, theme_text_primary(),
                   s_state == CALLS_DIALING ? "CALLING..." : "ON CALL");
        ghost_text(phone, mid - 1, CONTENT_COL, theme_text_primary(),
                   s_call_name[0] ? s_call_name : "Unknown");
        ghost_text(phone, mid,     CONTENT_COL, theme_text_muted(), s_call_number);

        if (s_state == CALLS_ACTIVE && s_call_start > 0) {
            time_t elapsed = time(NULL) - s_call_start;
            char timer[16];
            snprintf(timer, sizeof(timer), "%02d:%02d",
                     (int)(elapsed / 60), (int)(elapsed % 60));
            ghost_text(phone, mid + 2, CONTENT_COL, theme_text_muted(), timer);
        }

        ghost_softkeys(phone, "[Hang Up]", "");
        return;
    }

    /* ── call log ── */
    ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL, theme_text_muted(), "STATE: LOG");

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
        int sel = (idx == s_selected);

        ncplane_set_fg_rgb(phone, sel ? theme_text_primary() : theme_text_muted());
        ncplane_set_bg_rgb(phone, theme_bg());

        char line[256];
        snprintf(line, sizeof(line), "%s%s  %-10s  %s",
                 sel ? MENU_CURSOR : MENU_CURSOR_BLANK,
                 call->icon, call->name, call->time);
        if ((int)strlen(line) > width) line[width] = '\0';
        ncplane_putstr_yx(phone, row, CONTENT_COL, line);
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "Left:Delete  Enter:Call");
    ghost_softkeys(phone, "[Back]", "");
    if (s_delete_prompt) draw_delete_popup(phone);
}

/* ── input ────────────────────────────────────────────────────────────────── */

screen_id screen_calls_input(uint32_t key)
{
    /* ── incoming call ── */
    if (s_state == CALLS_INCOMING) {
        switch (key) {
            case KEY_SOFT_LEFT_ACTION:
            case NCKEY_LEFT:
                modem_ipc_reject();
                enter_log();
                return SCREEN_CALLS;
            case KEY_SOFT_RIGHT_ACTION:
            case NCKEY_ENTER: case '\n':
            case NCKEY_RIGHT:
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
                /* Persist call to log */
                comm_service_call_add_full(
                    s_call_name, s_call_number,
                    s_state == CALLS_DIALING ? "outgoing" : "outgoing",
                    dur);
                enter_log();
                return SCREEN_CALLS;
            }
            default: {
                /* Check if call was answered (dialing → active) */
                if (s_state == CALLS_DIALING) {
                    ModemStatus st;
                    if (modem_ipc_get_status(&st) == 0 && !st.has_incoming_call)
                        s_state = CALLS_ACTIVE;
                }
                return SCREEN_CALLS;
            }
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
            /* Dial the selected contact */
            const CommCall *c = comm_service_call_at((size_t)s_selected);
            if (c && c->phone[0]) {
                snprintf(s_call_number, sizeof(s_call_number), "%s", c->phone);
                snprintf(s_call_name,   sizeof(s_call_name),   "%s", c->name);
                if (modem_ipc_dial(s_call_number) == 0) {
                    s_state      = CALLS_DIALING;
                    s_call_start = 0;
                }
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
        default: {
            /* Check for incoming call */
            ModemStatus st;
            if (modem_ipc_get_status(&st) == 0 && st.has_incoming_call) {
                snprintf(s_call_number, sizeof(s_call_number), "%s", st.incoming_number);
                s_call_name[0] = '\0';
                s_state = CALLS_INCOMING;
            }
            return SCREEN_CALLS;
        }
    }
}
