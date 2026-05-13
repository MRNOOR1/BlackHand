#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "draw_utils.h"
#include "services/bluetooth_service.h"
#include "services/theme_service.h"
#include "ui.h"

typedef enum
{
    BT_STATE_OFF,
    BT_STATE_IDLE,
    BT_STATE_SCANNING,
} bt_screen_state_t;

static bt_screen_state_t bt_state = BT_STATE_OFF;
static int sel = 0;
static int scroll = 0;
static int scan_tick = 0;

/* Spinner frames */
static const char *SPINNER[] = {"◐", "◓", "◑", "◒"};
#define SPINNER_COUNT 4

/* ── Draw helpers ────────────────────────────────────────────────────── */

static void draw_header(struct ncplane *p, unsigned cols, const char *subtitle)
{
    int width = INNER_WIDTH(cols);
    ncplane_set_fg_rgb(p, theme_text_primary());
    ncplane_set_bg_rgb(p, theme_bg());
    ncplane_putstr_yx(p, CONTENT_START_ROW, CONTENT_COL, "BLUETOOTH");

    if (subtitle && subtitle[0])
    {
        ncplane_set_fg_rgb(p, theme_text_muted());
        ncplane_putstr_yx(p, CONTENT_START_ROW, CONTENT_COL + 10, subtitle);
    }

    ncplane_set_fg_rgb(p, theme_text_muted());
    const char *rule = theme_rule_glyph();
    for (int x = 0; x < width && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(p, CONTENT_START_ROW + 1, CONTENT_COL + x,
                          (rule && rule[0]) ? rule : "-");
}

/* ── Draw: powered off ───────────────────────────────────────────────── */

static void draw_off(struct ncplane *p, unsigned rows, unsigned cols)
{
    (void)rows;
    draw_header(p, cols, "  [OFF]");
    ghost_text(p, CONTENT_START_ROW + 3, CONTENT_COL,
               theme_text_muted(), "Bluetooth is off");
    ghost_text(p, CONTENT_START_ROW + 5, CONTENT_COL,
               theme_text_primary(), "Press ENTER to power on");
    ghost_softkeys(p, "[Back]", "[On]");
}

/* ── Draw: scanning ──────────────────────────────────────────────────── */

static void draw_scanning(struct ncplane *p, unsigned rows, unsigned cols)
{
    (void)rows;
    draw_header(p, cols, "  [ON]");

    char spin_line[32];
    snprintf(spin_line, sizeof(spin_line), "%s  Scanning...",
             SPINNER[scan_tick % SPINNER_COUNT]);
    ghost_text(p, CONTENT_START_ROW + 3, CONTENT_COL,
               theme_text_primary(), spin_line);
    ghost_text(p, CONTENT_START_ROW + 5, CONTENT_COL,
               theme_text_muted(), "This takes about 6 seconds");
    ghost_softkeys(p, "[Back]", "");
}

/* ── Draw: idle — device list ────────────────────────────────────────── */

static void draw_idle(struct ncplane *p, unsigned rows, unsigned cols)
{
    draw_header(p, cols, "  [ON]");

    int footer = (int)rows - FOOTER_ROW_OFFSET;
    int list_start = CONTENT_START_ROW + 2;
    int max_rows = footer - list_start - 1;
    if (max_rows < 1)
        max_rows = 1;

    size_t count = bluetooth_service_device_count();

    /* First entry is always "Scan for devices" */
    size_t total = count + 1; /* +1 for scan row */

    if (sel < 0)
        sel = 0;
    if (sel >= (int)total)
        sel = (int)total - 1;
    if (sel < scroll)
        scroll = sel;
    if (sel >= scroll + max_rows)
        scroll = sel - max_rows + 1;

    for (int i = 0; i < max_rows; i++)
    {
        int idx = scroll + i;
        if (idx >= (int)total)
            break;

        int row = list_start + i;
        int is_sel = (idx == sel);

        ncplane_set_fg_rgb(p, is_sel ? theme_text_primary() : theme_text_muted());
        ncplane_set_bg_rgb(p, theme_bg());
        ncplane_putstr_yx(p, row, CONTENT_COL,
                          is_sel ? MENU_CURSOR : MENU_CURSOR_BLANK);

        if (idx == 0)
        {
            /* Scan row */
            ncplane_putstr_yx(p, row, CONTENT_COL + 2, "⟳  Scan for devices");
        }
        else
        {
            const BtDevice *dev = bluetooth_service_device_at((size_t)(idx - 1));
            if (!dev)
                continue;

            char status = dev->connected ? '●' : (dev->paired ? '○' : ' ');
            char line[128];
            snprintf(line, sizeof(line), "%c  %s",
                     status, dev->name[0] ? dev->name : dev->mac);

            int width = INNER_WIDTH(cols) - 4;
            if ((int)strlen(line) > width)
                line[width] = '\0';
            ncplane_putstr_yx(p, row, CONTENT_COL + 2, line);

            /* Show MAC in muted colour below name when selected */
            if (is_sel && row + 1 < footer - 1)
            {
                ghost_text(p, row + 1, CONTENT_COL + 4,
                           theme_text_muted(), dev->mac);
            }
        }
    }

    /* Hint line */
    if (sel > 0)
    {
        const BtDevice *dev = bluetooth_service_device_at((size_t)(sel - 1));
        if (dev)
        {
            ghost_text(p, footer - 2, CONTENT_COL, theme_text_muted(),
                       dev->connected ? "ENTER=Disconnect  DEL=Remove"
                                      : "ENTER=Connect     DEL=Remove");
        }
    }

    ghost_softkeys(p, "[Back]", "[Off]");
}

/* ── Main draw ───────────────────────────────────────────────────────── */

void screen_bluetooth_draw(struct ncplane *phone)
{
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    /* Auto-detect power state on first draw */
    static int initialised = 0;
    if (!initialised)
    {
        bt_state = bluetooth_service_get_power() ? BT_STATE_IDLE : BT_STATE_OFF;
        initialised = 1;
    }

    /* Advance spinner / detect scan completion */
    if (bt_state == BT_STATE_SCANNING)
    {
        scan_tick++;
        if (!bluetooth_service_scan_is_running())
            bt_state = BT_STATE_IDLE;
    }

    switch (bt_state)
    {
    case BT_STATE_OFF:
        draw_off(phone, rows, cols);
        break;
    case BT_STATE_SCANNING:
        draw_scanning(phone, rows, cols);
        break;
    case BT_STATE_IDLE:
        draw_idle(phone, rows, cols);
        break;
    }
}

/* ── Input handling ──────────────────────────────────────────────────── */

screen_id screen_bluetooth_input(uint32_t key)
{

    if (bt_state == BT_STATE_SCANNING)
    {
        /* Only allow back while scanning */
        if (key == 'q' || key == 'Q' || key == NCKEY_LEFT)
            return SCREEN_SETTINGS;
        return SCREEN_BLUETOOTH;
    }

    if (bt_state == BT_STATE_OFF)
    {
        switch (key)
        {
        case NCKEY_ENTER:
        case '\n':
        case 'e':
        case 'E':
            bluetooth_service_set_power(1);
            bt_state = BT_STATE_IDLE;
            sel = 0;
            scroll = 0;
            return SCREEN_BLUETOOTH;
        case 'q':
        case 'Q':
        case NCKEY_LEFT:
            return SCREEN_SETTINGS;
        default:
            return SCREEN_BLUETOOTH;
        }
    }

    /* BT_STATE_IDLE */
    size_t count = bluetooth_service_device_count();
    size_t total = count + 1;

    switch (key)
    {
    case NCKEY_UP:
        if (sel > 0)
            sel--;
        return SCREEN_BLUETOOTH;

    case NCKEY_DOWN:
        if (sel < (int)total - 1)
            sel++;
        return SCREEN_BLUETOOTH;

    case NCKEY_ENTER:
    case '\n':
        if (sel == 0)
        {
            /* Scan row — kick off background scan so the spinner can animate */
            scan_tick = 0;
            bt_state = BT_STATE_SCANNING;
            bluetooth_service_scan_start();
        }
        else
        {
            const BtDevice *dev = bluetooth_service_device_at((size_t)(sel - 1));
            if (dev)
            {
                if (dev->connected)
                    bluetooth_service_disconnect(dev->mac);
                else
                    bluetooth_service_connect(dev->mac);
            }
        }
        return SCREEN_BLUETOOTH;

    case NCKEY_DEL:
    case 127: /* backspace on some terminals */
        if (sel > 0)
        {
            const BtDevice *dev = bluetooth_service_device_at((size_t)(sel - 1));
            if (dev)
                bluetooth_service_remove(dev->mac);
            if (sel >= (int)bluetooth_service_device_count())
                sel--;
        }
        return SCREEN_BLUETOOTH;

    case 'e':
    case 'E':
        /* RSK — power off */
        bluetooth_service_set_power(0);
        bt_state = BT_STATE_OFF;
        sel = 0;
        scroll = 0;
        return SCREEN_BLUETOOTH;

    case 'q':
    case 'Q':
    case NCKEY_LEFT:
        return SCREEN_SETTINGS;

    default:
        return SCREEN_BLUETOOTH;
    }
}