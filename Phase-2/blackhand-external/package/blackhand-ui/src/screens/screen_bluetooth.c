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
    BT_STATE_DEVICE,
    BT_STATE_CONNECTING,
} bt_screen_state_t;

static bt_screen_state_t bt_state       = BT_STATE_OFF;
static int               sel            = 0;
static int               scroll         = 0;
static int               scan_tick      = 0;
static int               bt_initialised = 0;
/* Tracks power state set through this UI so re-entry doesn't scan with BT off */
static int               s_bt_powered   = 1; /* rcS powers BT on at boot */

static char   dev_mac[24]   = {0};
static char   dev_name[96]  = {0};
static int    dev_connected = 0;
static int    dev_sel       = 0;

/* Filtered list — only devices that have a real resolved name */
static size_t s_named_idxs[BT_MAX_DEVICES];
static size_t s_named_count = 0;

static void build_named_device_list(void)
{
    size_t total = bluetooth_service_device_count();
    s_named_count = 0;
    for (size_t i = 0; i < total; i++)
    {
        const BtDevice *d = bluetooth_service_device_at(i);
        if (d && d->name[0] && !mac_is_safe(d->name))
            s_named_idxs[s_named_count++] = i;
    }
}

static const char *SPINNER[] = {"|", "/", "-", "\\"};
#define SPINNER_COUNT 4

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void draw_header(struct ncplane *p, unsigned cols, const char *sub)
{
    ncplane_set_fg_rgb(p, theme_text_primary());
    ncplane_set_bg_rgb(p, theme_bg());
    ncplane_putstr_yx(p, CONTENT_START_ROW, CONTENT_COL, "BLUETOOTH");
    if (sub && sub[0])
    {
        ncplane_set_fg_rgb(p, theme_text_muted());
        ncplane_putstr_yx(p, CONTENT_START_ROW, CONTENT_COL + 10, sub);
    }
    ncplane_set_fg_rgb(p, theme_border());
    ncplane_set_bg_rgb(p, theme_bg());
    const char *rule = theme_rule_glyph();
    int w = INNER_WIDTH(cols);
    for (int x = 0; x < w && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putstr_yx(p, CONTENT_START_ROW + 1, CONTENT_COL + x,
                          (rule && rule[0]) ? rule : "-");
}

/* Draws one full-width row with proper selection highlight */
static void draw_row(struct ncplane *p, int row, int cols,
                     int is_sel, const char *text)
{
    uint32_t fg = is_sel ? theme_selection_text() : theme_text_primary();
    uint32_t bg = is_sel ? theme_selection_bg()   : theme_bg();
    int      w  = INNER_WIDTH(cols);

    ncplane_set_fg_rgb(p, fg);
    ncplane_set_bg_rgb(p, bg);

    /* Fill entire row so the highlight block is solid */
    for (int x = 0; x < w && CONTENT_COL + x < (int)cols - 1; x++)
        ncplane_putchar_yx(p, row, CONTENT_COL + x, ' ');

    ncplane_putstr_yx(p, row, CONTENT_COL,     is_sel ? "> " : "  ");
    ncplane_putstr_yx(p, row, CONTENT_COL + 2, text);
}

/* ── OFF ─────────────────────────────────────────────────────────────── */

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

/* ── SCANNING ────────────────────────────────────────────────────────── */

static void draw_scanning(struct ncplane *p, unsigned rows, unsigned cols)
{
    (void)rows;
    draw_header(p, cols, "  [SCANNING]");
    char line[80];
    snprintf(line, sizeof(line), "%s  Scanning...",
             SPINNER[scan_tick % SPINNER_COUNT]);
    ghost_text(p, CONTENT_START_ROW + 3, CONTENT_COL, theme_text_primary(), line);
    ghost_text(p, CONTENT_START_ROW + 5, CONTENT_COL,
               theme_text_muted(), "~10 seconds");
    ghost_softkeys(p, "[Back]", "");
}

/* ── IDLE ────────────────────────────────────────────────────────────── */

static void draw_idle(struct ncplane *p, unsigned rows, unsigned cols)
{
    draw_header(p, cols, "  [ON]");

    int footer     = (int)rows - FOOTER_ROW_OFFSET;
    int list_start = CONTENT_START_ROW + 2;
    int max_rows   = footer - list_start - 2;
    if (max_rows < 1) max_rows = 1;

    build_named_device_list();
    size_t count = s_named_count;
    size_t total = count + 1;

    if (sel < 0)                  sel = 0;
    if (sel >= (int)total)        sel = (int)total - 1;
    if (sel < scroll)             scroll = sel;
    if (sel >= scroll + max_rows) scroll = sel - max_rows + 1;

    for (int i = 0; i < max_rows; i++)
    {
        int idx    = scroll + i;
        if (idx >= (int)total) break;
        int row    = list_start + i;
        int is_sel = (idx == sel);

        if (idx == 0)
        {
            draw_row(p, row, cols, is_sel, "Scan for devices");
        }
        else
        {
            const BtDevice *dev = bluetooth_service_device_at(
                                      s_named_idxs[(size_t)(idx - 1)]);
            if (!dev) continue;
            char line[128];
            snprintf(line, sizeof(line), "%.*s", INNER_WIDTH(cols) - 4, dev->name);
            draw_row(p, row, cols, is_sel, line);
        }
    }

    if (count == 0)
        ghost_text(p, footer - 2, CONTENT_COL, theme_text_muted(),
                   "No devices found. Press ENTER to scan.");
    else if (sel > 0)
        ghost_text(p, footer - 2, CONTENT_COL, theme_text_muted(),
                   "ENTER to connect");

    ghost_softkeys(p, "[Back]", "[Off]");
}

/* ── DEVICE DETAIL ───────────────────────────────────────────────────── */

static void draw_device(struct ncplane *p, unsigned rows, unsigned cols)
{
    (void)rows;
    draw_header(p, cols, "  [DEVICE]");

    int row = CONTENT_START_ROW + 2;

    ghost_text(p, row++, CONTENT_COL, theme_text_primary(),
               dev_name[0] ? dev_name : dev_mac);
    ghost_text(p, row++, CONTENT_COL, theme_text_muted(), dev_mac);
    ghost_text(p, row,   CONTENT_COL,
               dev_connected ? theme_text_primary() : theme_text_muted(),
               dev_connected ? "Connected" : "Not connected");
    row += 2;

    draw_row(p, row,     cols, (dev_sel == 0),
             dev_connected ? "Disconnect" : "Connect");
    draw_row(p, row + 1, cols, (dev_sel == 1), "Remove device");

    ghost_softkeys(p, "[Back]", "[Select]");
}

/* ── CONNECTING ──────────────────────────────────────────────────────── */

static void draw_connecting(struct ncplane *p, unsigned rows, unsigned cols)
{
    (void)rows;
    draw_header(p, cols, "  [CONNECTING]");
    char line[128];
    snprintf(line, sizeof(line), "%s  Connecting...",
             SPINNER[scan_tick % SPINNER_COUNT]);
    ghost_text(p, CONTENT_START_ROW + 3, CONTENT_COL, theme_text_primary(), line);
    ghost_text(p, CONTENT_START_ROW + 4, CONTENT_COL + 2,
               theme_text_muted(), dev_name[0] ? dev_name : dev_mac);
    ghost_text(p, CONTENT_START_ROW + 6, CONTENT_COL,
               theme_text_muted(), "Please wait (~15 s)");
    ghost_softkeys(p, "", "");
}

/* ── Main draw ───────────────────────────────────────────────────────── */

void screen_bluetooth_draw(struct ncplane *phone)
{
    unsigned rows, cols;
    ncplane_dim_yx(phone, &rows, &cols);

    if (!bt_initialised)
    {
        bt_initialised = 1;
        if (s_bt_powered && bluetooth_service_is_available())
        {
            /* Auto-scan on every entry. If a scan is already running
             * (e.g., user left and re-entered mid-scan) just show the spinner
             * and let the existing thread complete. */
            scan_tick = 0;
            bt_state  = BT_STATE_SCANNING;
            if (!bluetooth_service_scan_is_running())
                bluetooth_service_scan_start();
        }
        else
        {
            bt_state = BT_STATE_OFF;
        }
    }

    scan_tick++;

    if (bt_state == BT_STATE_SCANNING && !bluetooth_service_scan_is_running())
        bt_state = BT_STATE_IDLE;

    if (bt_state == BT_STATE_CONNECTING && !bluetooth_service_connect_is_running())
    {
        int rc    = bluetooth_service_refresh_device(dev_mac);
        dev_connected = (rc == 1) ? 1 : 0;

        /* Refresh the name in case it was resolved during connect */
        for (size_t i = 0; i < bluetooth_service_device_count(); i++)
        {
            const BtDevice *d = bluetooth_service_device_at(i);
            if (d && strcmp(d->mac, dev_mac) == 0 &&
                d->name[0] && !mac_is_safe(d->name))
            {
                strncpy(dev_name, d->name, sizeof(dev_name) - 1);
                break;
            }
        }
        bt_state = BT_STATE_DEVICE;
    }

    switch (bt_state)
    {
    case BT_STATE_OFF:        draw_off(phone, rows, cols);        break;
    case BT_STATE_SCANNING:   draw_scanning(phone, rows, cols);   break;
    case BT_STATE_IDLE:       draw_idle(phone, rows, cols);       break;
    case BT_STATE_DEVICE:     draw_device(phone, rows, cols);     break;
    case BT_STATE_CONNECTING: draw_connecting(phone, rows, cols); break;
    }
}

/* ── Input ───────────────────────────────────────────────────────────── */

screen_id screen_bluetooth_input(uint32_t key)
{
    if (bt_state == BT_STATE_CONNECTING)
        return SCREEN_BLUETOOTH; /* blocked during connect */

    /* SCANNING */
    if (bt_state == BT_STATE_SCANNING)
    {
        if (key == 'q' || key == 'Q' || key == NCKEY_LEFT ||
            key == KEY_SOFT_LEFT_ACTION)
        {
            bluetooth_service_scan_stop();
            bt_initialised = 0;
            return SCREEN_SETTINGS;
        }
        return SCREEN_BLUETOOTH;
    }

    /* OFF */
    if (bt_state == BT_STATE_OFF)
    {
        switch (key)
        {
        case NCKEY_ENTER: case '\n':
            bluetooth_service_set_power(1);
            s_bt_powered = 1;
            scan_tick    = 0;
            bt_state     = BT_STATE_SCANNING;
            bluetooth_service_scan_start();
            sel = 0; scroll = 0;
            return SCREEN_BLUETOOTH;
        case 'q': case 'Q': case NCKEY_LEFT: case KEY_SOFT_LEFT_ACTION:
            bt_initialised = 0;
            return SCREEN_SETTINGS;
        default:
            return SCREEN_BLUETOOTH;
        }
    }

    /* DEVICE DETAIL */
    if (bt_state == BT_STATE_DEVICE)
    {
        switch (key)
        {
        case NCKEY_UP:
            if (dev_sel > 0) dev_sel--;
            return SCREEN_BLUETOOTH;
        case NCKEY_DOWN:
            if (dev_sel < 1) dev_sel++;
            return SCREEN_BLUETOOTH;
        case NCKEY_ENTER: case '\n':
            if (dev_sel == 0)
            {
                if (dev_connected)
                {
                    bluetooth_service_disconnect(dev_mac);
                    dev_connected = 0;
                }
                else
                {
                    bluetooth_service_connect_async(dev_mac);
                    bt_state = BT_STATE_CONNECTING;
                }
            }
            else
            {
                bluetooth_service_remove(dev_mac);
                build_named_device_list();
                bt_state = BT_STATE_IDLE;
                if (sel > (int)s_named_count) sel = (int)s_named_count;
            }
            return SCREEN_BLUETOOTH;
        case 'q': case 'Q': case NCKEY_LEFT: case KEY_SOFT_LEFT_ACTION:
            bt_state = BT_STATE_IDLE;
            return SCREEN_BLUETOOTH;
        default:
            return SCREEN_BLUETOOTH;
        }
    }

    /* IDLE — named device list */
    build_named_device_list();
    size_t count = s_named_count;
    size_t total = count + 1;

    switch (key)
    {
    case NCKEY_UP:
        if (sel > 0) sel--;
        return SCREEN_BLUETOOTH;
    case NCKEY_DOWN:
        if (sel < (int)total - 1) sel++;
        return SCREEN_BLUETOOTH;

    case NCKEY_ENTER: case '\n':
        if (sel == 0)
        {
            scan_tick = 0;
            bt_state  = BT_STATE_SCANNING;
            bluetooth_service_scan_start();
        }
        else if ((size_t)(sel - 1) < s_named_count)
        {
            const BtDevice *dev = bluetooth_service_device_at(
                                      s_named_idxs[(size_t)(sel - 1)]);
            if (dev)
            {
                /* Stop scanning before inspecting/connecting — keeps audio clean */
                bluetooth_service_scan_stop();
                strncpy(dev_mac,  dev->mac,  sizeof(dev_mac)  - 1);
                strncpy(dev_name, dev->name, sizeof(dev_name) - 1);
                int rc        = bluetooth_service_refresh_device(dev_mac);
                dev_connected = (rc == 1) ? 1 : 0;
                dev_sel  = 0;
                bt_state = BT_STATE_DEVICE;
            }
        }
        return SCREEN_BLUETOOTH;

    /* LSK — back to settings (does NOT change BT power state) */
    case 'q': case 'Q': case NCKEY_LEFT: case KEY_SOFT_LEFT_ACTION:
        bluetooth_service_scan_stop();
        bt_initialised = 0;
        return SCREEN_SETTINGS;

    /* RSK — power off */
    case KEY_SOFT_RIGHT_ACTION:
        bluetooth_service_set_power(0);
        s_bt_powered = 0;
        bt_state     = BT_STATE_OFF;
        sel = 0; scroll = 0;
        return SCREEN_BLUETOOTH;

    default:
        return SCREEN_BLUETOOTH;
    }
}
