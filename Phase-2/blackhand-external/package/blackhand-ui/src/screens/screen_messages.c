#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "bh_skin.h"
#include "services/comm_service.h"
#include "services/theme_service.h"
#include "services/multitap_service.h"
#include "ui-ipcs/modem_ipc.h"
#include "ui-ipcs/storage_ipc.h"
#include "country_codes.h"
#include "ui.h"

typedef enum {
    MSG_INBOX,     /* thread list: one row per conversation */
    MSG_THREAD,    /* one open conversation, oldest→newest  */
    MSG_COMPOSE,
} msg_state_t;

static msg_state_t s_state       = MSG_INBOX;
static int         s_selected    = 0;
static int         s_scroll      = 0;
static int         s_delete_prompt = 0;
static int         s_delete_yes  = 0;

/* Compose state */
static int    s_compose_field = 0;      /* 0 = phone number, 1 = body */
static char   s_compose_number[32] = "";
static char   s_compose_body[160]  = "";
static multitap_state s_mt;
static int    s_mt_ready = 0;
static int    s_send_feedback = 0; /* 1=sent ok, -1=error, 0=idle */
static int    s_send_ticks = 0;

/* Thread view scroll: index of the first visible message. */
static int    s_thread_scroll = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void ensure_mt(void) {
    if (!s_mt_ready) { multitap_init(&s_mt); s_mt_ready = 1; }
}

static void enter_compose(const char *preset_number) {
    ensure_mt();
    s_state = MSG_COMPOSE;
    s_compose_field = (preset_number && preset_number[0]) ? 1 : 0;
    snprintf(s_compose_number, sizeof(s_compose_number), "%s",
             preset_number ? preset_number : "");
    s_compose_body[0] = '\0';
    multitap_reset(&s_mt);
    multitap_set_field(&s_mt, s_compose_field);
    s_send_feedback = 0;
}

static void draw_delete_popup(struct ncplane *phone, unsigned rows, unsigned cols)
{
    (void)rows; (void)cols;
    ghost_confirm_popup(phone, "DELETE MESSAGE?", s_delete_yes);
}

/* ── public entry point ──────────────────────────────────────────────────── */

void screen_messages_start_compose(const char *phone)
{
    enter_compose(phone);
}

int screen_messages_is_compose_mode(void)
{
    return s_state == MSG_COMPOSE ? 1 : 0;
}

/* ── draw ─────────────────────────────────────────────────────────────────── */

void screen_messages_draw(struct ncplane *phone)
{
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);
    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int width  = INNER_WIDTH(cols);
    if (width < 10) return;

    ncplane_set_fg_rgb(phone, theme_text_primary());
    ncplane_set_bg_rgb(phone, theme_bg());
    ncplane_putstr_yx(phone, CONTENT_START_ROW, CONTENT_COL, "MESSAGES");

    ncplane_set_fg_rgb(phone, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(phone, CONTENT_START_ROW + 1, CONTENT_COL + x,
                          (rule && rule[0]) ? rule : "-");

    /* ── compose ── */
    if (s_state == MSG_COMPOSE) {
        int top = CONTENT_START_ROW + 3;
        ghost_text(phone, top, CONTENT_COL, theme_text_muted(), "NEW MESSAGE");

        /* Number field */
        ncplane_set_fg_rgb(phone, (s_compose_field == 0) ? theme_selection_text()
                                                           : theme_text_muted());
        ncplane_set_bg_rgb(phone, (s_compose_field == 0) ? theme_selection_bg()
                                                           : theme_bg());
        ncplane_putstr_yx(phone, top + 2, CONTENT_COL,
                          (s_compose_field == 0) ? MENU_CURSOR : MENU_CURSOR_BLANK);
        char disp_num[64];
        snprintf(disp_num, sizeof(disp_num), "To: %s",
                 s_compose_number[0] ? s_compose_number : "(number)");
        ncplane_putstr_yx(phone, top + 2, CONTENT_COL + 2, disp_num);

        /* Body field */
        ncplane_set_fg_rgb(phone, (s_compose_field == 1) ? theme_selection_text()
                                                           : theme_text_muted());
        ncplane_set_bg_rgb(phone, (s_compose_field == 1) ? theme_selection_bg()
                                                           : theme_bg());
        ncplane_putstr_yx(phone, top + 4, CONTENT_COL,
                          (s_compose_field == 1) ? MENU_CURSOR : MENU_CURSOR_BLANK);
        char disp_body[164];
        snprintf(disp_body, sizeof(disp_body), "%s",
                 s_compose_body[0] ? s_compose_body : "(message)");
        if ((int)strlen(disp_body) > width - 3)
            disp_body[width - 3] = '\0';
        ncplane_putstr_yx(phone, top + 4, CONTENT_COL + 2, disp_body);

        /* Send feedback */
        if (s_send_feedback != 0) {
            ghost_text(phone, top + 6, CONTENT_COL, theme_text_muted(),
                       s_send_feedback > 0 ? "Message sent!" : "Send failed");
        }

        /* char counter in the footer position (blueprint SEND) */
        char counter[16];
        snprintf(counter, sizeof(counter), "%d/160", (int)strlen(s_compose_body));
        ghost_text(phone, footer - 2, (int)cols - CONTENT_COL - (int)strlen(counter),
                   theme_border(), counter);

        bh_action_cells(phone, "SEND", "CLR", -1);
        return;
    }

    /* ── open conversation ── */
    if (s_state == MSG_THREAD) {
        size_t n = comm_service_thread_count();

        /* header: who we're talking to */
        char hdr[64];
        const CommMessage *first = comm_service_thread_at(0);
        snprintf(hdr, sizeof(hdr), "%s",
                 (first && !first->is_outgoing && first->sender[0])
                     ? first->sender : comm_service_thread_number());
        ghost_text(phone, CONTENT_START_ROW + 2, CONTENT_COL,
                   theme_text_primary(), hdr);

        int list_start = CONTENT_START_ROW + 4;
        int visible    = footer - list_start - 1;
        if (visible < 1) visible = 1;

        if (s_thread_scroll < 0) s_thread_scroll = 0;
        if ((int)n > visible && s_thread_scroll > (int)n - visible)
            s_thread_scroll = (int)n - visible;

        if (n == 0) {
            ghost_text(phone, list_start, CONTENT_COL, theme_text_muted(),
                       "No messages in this conversation");
        }

        for (int i = 0; i < visible; i++) {
            int idx = s_thread_scroll + i;
            if (idx >= (int)n) break;
            const CommMessage *m = comm_service_thread_at((size_t)idx);
            if (!m) continue;

            char line[256];
            snprintf(line, sizeof(line), "%s %s",
                     m->is_outgoing ? "→" : "←", m->body);
            if ((int)strlen(line) > width) line[width] = '\0';
            ncplane_set_fg_rgb(phone, m->is_outgoing ? theme_text_muted()
                                                     : theme_text_primary());
            ncplane_set_bg_rgb(phone, theme_bg());
            ncplane_putstr_yx(phone, list_start + i, CONTENT_COL, line);
        }

        bh_action_cells(phone, "REPLY", "", 0);
        return;
    }

    /* ── inbox ── */
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

    size_t count = comm_service_message_count();
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
        int mid = (list_start + footer) / 2;
        ncplane_set_fg_rgb(phone, theme_text_muted());
        ncplane_putstr_yx(phone, mid,     CONTENT_COL, "No messages yet");
        ncplane_putstr_yx(phone, mid + 1, CONTENT_COL, "Right soft: compose");
    } else {
        for (int i = 0; i < visible; i++) {
            int idx = s_scroll + i;
            if (idx >= (int)count) break;
            const CommMessage *msg = comm_service_message_at((size_t)idx);
            if (!msg) continue;

            int row = list_start + i;
            if (row >= footer - 1) break;
            int sel = (idx == s_selected);

            /* blueprint MSGS row: NAME (left, 10ch) · unread ● + HH:MM */
            char label[16], meta[20];
            snprintf(label, sizeof(label), "%.10s", msg->sender);
            snprintf(meta, sizeof(meta), "%s%s",
                     msg->read ? "" : "● ", msg->stamp);
            bh_list_item(phone, row, CONTENT_COL, width, label, meta, sel, idx);
        }
    }

    ghost_text(phone, footer - 1, CONTENT_COL, theme_text_muted(), "Left:Delete  Right:Compose");
    ghost_softkeys(phone, "[Back]", "[New]");
    if (s_delete_prompt) draw_delete_popup(phone, rows, cols);
}

/* ── input ────────────────────────────────────────────────────────────────── */

screen_id screen_messages_input(uint32_t key)
{
    /* ── compose ── */
    if (s_state == MSG_COMPOSE) {
        ensure_mt();
        char *active    = (s_compose_field == 0) ? s_compose_number : s_compose_body;
        size_t active_sz = (s_compose_field == 0) ? sizeof(s_compose_number)
                                                   : sizeof(s_compose_body);

        switch (key) {
            case NCKEY_UP:
                s_compose_field = 0;
                multitap_set_field(&s_mt, 0);
                return SCREEN_MESSAGES;
            case NCKEY_DOWN:
                s_compose_field = 1;
                multitap_set_field(&s_mt, 1);
                return SCREEN_MESSAGES;
            case KEY_SOFT_LEFT_ACTION:
                multitap_reset(&s_mt);
                s_state = MSG_INBOX;
                return SCREEN_MESSAGES;
            case KEY_SOFT_RIGHT_ACTION:
            case KEY_ACTION_PRIMARY: {     /* A = send */
                /* Send */
                if (s_compose_number[0] && s_compose_body[0]) {
                    /* Normalise to E.164 with the user's selected country so
                     * the modem dials/stores the same key as everything else. */
                    char e164[40];
                    {
                        const CountryCode *cc = NULL;
                        char iso[8];
                        if (storage_ipc_settings_get_str("country_iso", iso,
                                                         sizeof(iso)) == 0)
                            cc = country_by_iso(iso);
                        if (phone_normalize(s_compose_number, cc,
                                            e164, sizeof(e164)) != 0)
                            snprintf(e164, sizeof(e164), "%s", s_compose_number);
                    }
                    snprintf(s_compose_number, sizeof(s_compose_number), "%s", e164);
                    int rc = modem_ipc_send_sms(s_compose_number, s_compose_body);
                    if (rc == 0) {
                        comm_service_message_add_full(
                            "Me", s_compose_number, s_compose_body, 1);
                        s_send_feedback = 1;
                    } else {
                        s_send_feedback = -1;
                    }
                    s_send_ticks = 0;
                }
                /* Stay in compose briefly to show feedback, then return */
                s_state = MSG_INBOX;
                multitap_reset(&s_mt);
                return SCREEN_MESSAGES;
            }
            case NCKEY_ENTER: case '\n':
                if (s_compose_field == 0) {
                    s_compose_field = 1;
                    multitap_set_field(&s_mt, 1);
                }
                return SCREEN_MESSAGES;
            case KEY_ACTION_SECONDARY:     /* D = backspace */
            case NCKEY_BACKSPACE: case 127:
                multitap_backspace(&s_mt, s_compose_field, active);
                return SCREEN_MESSAGES;
            case '#':
                multitap_toggle_case(&s_mt);
                return SCREEN_MESSAGES;
            default:
                if (s_compose_field == 0 &&
                    ((key >= '0' && key <= '9') || key == '+' || key == '*')) {
                    /* Phone number: digits and + only */
                    size_t len = strlen(s_compose_number);
                    if (len + 1 < sizeof(s_compose_number)) {
                        s_compose_number[len]     = (char)key;
                        s_compose_number[len + 1] = '\0';
                    }
                } else if ((key >= '0' && key <= '9') || key == '*') {
                    multitap_apply_key(&s_mt, key, s_compose_field, active, active_sz);
                } else if (key >= 32 && key <= 126) {
                    multitap_reset(&s_mt);
                    size_t len = strlen(active);
                    if (len + 1 < active_sz) {
                        active[len]     = (char)key;
                        active[len + 1] = '\0';
                    }
                }
                return SCREEN_MESSAGES;
        }
    }

    /* ── open conversation ── */
    if (s_state == MSG_THREAD) {
        switch (key) {
            case NCKEY_UP:
                if (s_thread_scroll > 0) s_thread_scroll--;
                return SCREEN_MESSAGES;
            case NCKEY_DOWN:
                s_thread_scroll++;   /* clamped in draw */
                return SCREEN_MESSAGES;
            case KEY_SOFT_RIGHT_ACTION:
            case NCKEY_ENTER: case '\n':
            case KEY_ACTION_PRIMARY:       /* A = reply */
                enter_compose(comm_service_thread_number());
                return SCREEN_MESSAGES;
            case KEY_SOFT_LEFT_ACTION:
            case NCKEY_LEFT:
            case KEY_ACTION_SECONDARY:     /* D = back to inbox */
                comm_service_sync();
                s_state = MSG_INBOX;
                return SCREEN_MESSAGES;
            default:
                return SCREEN_MESSAGES;
        }
    }

    /* ── delete prompt ── */
    if (s_delete_prompt) {
        switch (key) {
            case NCKEY_LEFT: case NCKEY_UP:
                s_delete_yes = 0; return SCREEN_MESSAGES;
            case NCKEY_RIGHT: case NCKEY_DOWN:
                s_delete_yes = 1; return SCREEN_MESSAGES;
            case NCKEY_ENTER: case '\n': case KEY_SOFT_RIGHT_ACTION:
                if (s_delete_yes && comm_service_message_count() > 0) {
                    comm_service_message_delete((size_t)s_selected);
                    if (s_selected >= (int)comm_service_message_count() && s_selected > 0)
                        s_selected--;
                }
                s_delete_prompt = 0; s_delete_yes = 0;
                return SCREEN_MESSAGES;
            case KEY_SOFT_LEFT_ACTION:
                s_delete_prompt = 0; s_delete_yes = 0;
                return SCREEN_MESSAGES;
            default:
                return SCREEN_MESSAGES;
        }
    }

    /* ── inbox ── */
    switch (key) {
        case NCKEY_UP:
            if (s_selected > 0) s_selected--;
            return SCREEN_MESSAGES;
        case NCKEY_DOWN:
            if (s_selected < (int)comm_service_message_count() - 1) s_selected++;
            return SCREEN_MESSAGES;
        case NCKEY_RIGHT:
        case KEY_SOFT_RIGHT_ACTION:
            enter_compose(NULL);
            return SCREEN_MESSAGES;
        case NCKEY_ENTER: case '\n':
        case KEY_ACTION_PRIMARY: {         /* A = open conversation */
            const CommMessage *m = comm_service_message_at((size_t)s_selected);
            if (m && comm_service_thread_open(m->phone) == 0) {
                /* Open the conversation scrolled to the bottom (most recent
                   message). The draw loop clamps to [0, count - visible],
                   so a large value is fine, but it's clearer to compute
                   the real end of the list. */
                size_t n = comm_service_thread_count();
                s_thread_scroll = (n > 0) ? (int)n - 1 : 0;
                s_state = MSG_THREAD;
            }
            return SCREEN_MESSAGES;
        }
        case NCKEY_LEFT:
        case KEY_ACTION_SECONDARY:         /* D = delete prompt */
            if (comm_service_message_count() > 0) {
                s_delete_prompt = 1;
                s_delete_yes    = 0;
            }
            return SCREEN_MESSAGES;
        case KEY_SOFT_LEFT_ACTION:
            return SCREEN_HOME;
        default:
            return SCREEN_MESSAGES;
    }
}
