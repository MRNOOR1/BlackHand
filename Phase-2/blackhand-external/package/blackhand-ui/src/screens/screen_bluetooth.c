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
static int    dev_state_needs_refresh = 0;

/* Display list. Prefer resolved names, but show MAC-only devices too so scan
 * results never look empty while BlueZ is still resolving remote names. */
static size_t s_named_idxs[BT_MAX_DEVICES];
static size_t s_named_count = 0;

/* Separate tracking for paired devices so they persist across scans */
static size_t s_paired_idxs[BT_MAX_DEVICES];
static size_t s_paired_count = 0;
static size_t s_scan_idxs[BT_MAX_DEVICES];
static size_t s_scan_count = 0;

/* Debounce device list rebuilds - only rebuild if count changed */
static size_t s_last_device_count = 0;
static int    s_list_dirty = 1;  /* Force rebuild on first draw */

/* Precomputed display offsets — always computed via compute_display_offsets() */
static int s_paired_hdr   = -1;
static int s_paired_first = -1;
static int s_new_hdr      = -1;
static int s_new_first    = -1;
static int s_total_items  =  1;

static void compute_display_offsets(void)
{
    int pos = 1;
    if (s_paired_count > 0)
    {
        s_paired_hdr   = pos++;
        s_paired_first = pos;
        pos += (int)s_paired_count;
    }
    else { s_paired_hdr = -1; s_paired_first = -1; }
    if (s_scan_count > 0)
    {
        s_new_hdr   = pos++;
        s_new_first = pos;
        pos += (int)s_scan_count;
    }
    else { s_new_hdr = -1; s_new_first = -1; }
    s_total_items = pos;
}

static void build_named_device_list(void)
{
    /* Only rebuild if device count changed (debouncing).
     * This prevents expensive rebuilds on every RSSI/Name change event. */
    size_t total = bluetooth_service_device_count();
    if (!s_list_dirty && total == s_last_device_count)
        return;
    
    s_last_device_count = total;
    s_list_dirty = 0;

    /* Build separate lists: paired devices first, then new scan results.
     * Paired devices persist across scans and are always shown at the top. */
    s_paired_count = 0;
    s_scan_count = 0;
    s_named_count = 0;

    /* First pass: collect paired devices */
    for (size_t i = 0; i < total; i++)
    {
        const BtDevice *d = bluetooth_service_device_at(i);
        if (d && d->mac[0] && d->paired)
        {
            s_paired_idxs[s_paired_count++] = i;
            s_named_idxs[s_named_count++] = i;
        }
    }

    /* Second pass: collect scan results (non-paired) */
    for (size_t i = 0; i < total; i++)
    {
        const BtDevice *d = bluetooth_service_device_at(i);
        if (d && d->mac[0] && !d->paired)
        {
            s_scan_idxs[s_scan_count++] = i;
            s_named_idxs[s_named_count++] = i;
        }
    }

    compute_display_offsets();
}

static int cached_device_connected(const char *mac)
{
    if (!mac || !mac[0]) return 0;
    
    /* Cache the last lookup to avoid repeatedly scanning the device list */
    static char s_last_cached_mac[24] = {0};
    static int  s_last_cached_connected = 0;
    
    if (strcmp(mac, s_last_cached_mac) == 0)
        return s_last_cached_connected;
    
    strncpy(s_last_cached_mac, mac, sizeof(s_last_cached_mac) - 1);
    
    for (size_t i = 0; i < bluetooth_service_device_count(); i++)
    {
        const BtDevice *d = bluetooth_service_device_at(i);
        if (d && strcmp(d->mac, mac) == 0)
        {
            s_last_cached_connected = d->connected ? 1 : 0;
            return s_last_cached_connected;
        }
    }
    
    s_last_cached_connected = 0;
    return 0;
}

static const BtDevice *get_device_at_display_index(int idx)
{
    if (idx == 0 || idx == s_paired_hdr || idx == s_new_hdr) return NULL;
    if (s_paired_first >= 0 && idx >= s_paired_first &&
        idx < s_paired_first + (int)s_paired_count)
        return bluetooth_service_device_at(s_paired_idxs[idx - s_paired_first]);
    if (s_new_first >= 0 && idx >= s_new_first &&
        idx < s_new_first + (int)s_scan_count)
        return bluetooth_service_device_at(s_scan_idxs[idx - s_new_first]);
    return NULL;
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
    ghost_softkeys(p, "[Back]", "[Power On]");
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
    /* Surface the live link in the header — "can't see what's connected"
     * should never be a thing on this screen. */
    char sub[80] = "  [ON]";
    {
        char mac[24] = "";
        bluetooth_service_get_connected_mac(mac, sizeof(mac));
        if (mac[0]) {
            const char *label = mac;
            size_t n = bluetooth_service_device_count();
            for (size_t i = 0; i < n; i++) {
                const BtDevice *d = bluetooth_service_device_at(i);
                if (d && strcmp(d->mac, mac) == 0 &&
                    d->name[0] && !mac_is_safe(d->name)) { label = d->name; break; }
            }
            snprintf(sub, sizeof(sub), "  [ON] ▸ %.40s", label);
        }
    }
    draw_header(p, cols, sub);

    int footer     = (int)rows - FOOTER_ROW_OFFSET;
    int list_start = CONTENT_START_ROW + 2;
    int max_rows   = footer - list_start - 2;
    if (max_rows < 1) max_rows = 1;

    build_named_device_list(); /* also calls compute_display_offsets() */

    if (sel < 0)                      sel = 0;
    if (sel >= s_total_items)         sel = s_total_items - 1;
    if (sel < scroll)                 scroll = sel;
    if (sel >= scroll + max_rows)     scroll = sel - max_rows + 1;

    for (int i = 0; i < max_rows; i++)
    {
        int idx    = scroll + i;
        if (idx >= s_total_items) break;
        int row    = list_start + i;
        int is_sel = (idx == sel);

        if (idx == 0)
        {
            draw_row(p, row, cols, is_sel, "Scan for devices");
        }
        else if (idx == s_paired_hdr)
        {
            ncplane_set_fg_rgb(p, theme_text_muted());
            ncplane_set_bg_rgb(p, theme_bg());
            ncplane_putstr_yx(p, row, CONTENT_COL, "  ─ Paired ─");
        }
        else if (s_paired_first >= 0 && idx >= s_paired_first &&
                 idx < s_paired_first + (int)s_paired_count)
        {
            const BtDevice *dev = bluetooth_service_device_at(
                s_paired_idxs[idx - s_paired_first]);
            if (dev)
            {
                char line[128];
                const char *label = (dev->name[0] && !mac_is_safe(dev->name))
                                    ? dev->name : dev->mac;
                if (dev->connected)
                    snprintf(line, sizeof(line), "● %.*s",
                             INNER_WIDTH(cols) - 6, label);
                else
                    snprintf(line, sizeof(line), "%.*s",
                             INNER_WIDTH(cols) - 4, label);
                draw_row(p, row, cols, is_sel, line);
            }
        }
        else if (idx == s_new_hdr)
        {
            ncplane_set_fg_rgb(p, theme_text_muted());
            ncplane_set_bg_rgb(p, theme_bg());
            ncplane_putstr_yx(p, row, CONTENT_COL, "  ─ Nearby ─");
        }
        else if (s_new_first >= 0 && idx >= s_new_first &&
                 idx < s_new_first + (int)s_scan_count)
        {
            const BtDevice *dev = bluetooth_service_device_at(
                s_scan_idxs[idx - s_new_first]);
            if (dev)
            {
                char line[128];
                const char *label = (dev->name[0] && !mac_is_safe(dev->name))
                                    ? dev->name : dev->mac;
                snprintf(line, sizeof(line), "%.*s", INNER_WIDTH(cols) - 4, label);
                draw_row(p, row, cols, is_sel, line);
            }
        }
    }

    if (s_paired_count == 0 && s_scan_count == 0)
        ghost_text(p, footer - 2, CONTENT_COL, theme_text_muted(),
                   "No devices found. Press ENTER to scan.");
    else if (sel > 0)
        ghost_text(p, footer - 2, CONTENT_COL, theme_text_muted(),
                   "ENTER to connect");

    ghost_softkeys(p, "[Back]", "[Power Off]");
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
        dev_connected = cached_device_connected(dev_mac);

        /* Refresh the name in case it was resolved during connect */
        for (size_t i = 0; i < bluetooth_service_device_count(); i++)
        {
            const BtDevice *d = bluetooth_service_device_at(i);
            if (d && strcmp(d->mac, dev_mac) == 0 && d->name[0])
            {
                const char *label = !mac_is_safe(d->name) ? d->name : d->mac;
                strncpy(dev_name, label, sizeof(dev_name) - 1);
                break;
            }
        }
        bt_state = BT_STATE_DEVICE;
    }

    /* Check if async device state refresh completed and update UI */
    if (dev_state_needs_refresh && bt_state == BT_STATE_DEVICE)
    {
        dev_connected = cached_device_connected(dev_mac);
        dev_state_needs_refresh = 0;  /* Refresh complete */
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
    {
        if (key == KEY_SOFT_LEFT_ACTION || key == NCKEY_LEFT ||
            key == KEY_ACTION_SECONDARY)
        {
            bt_state = BT_STATE_IDLE;
            return SCREEN_SETTINGS;
        }
        return SCREEN_BLUETOOTH;
    }

    /* SCANNING */
    if (bt_state == BT_STATE_SCANNING)
    {
        if (key == KEY_SOFT_LEFT_ACTION || key == NCKEY_LEFT ||
            key == KEY_ACTION_SECONDARY)
        {
            bluetooth_service_scan_stop();
            bt_state = BT_STATE_IDLE;
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
        case KEY_SOFT_RIGHT_ACTION:
        case KEY_ACTION_PRIMARY:           /* A = power on */
            bluetooth_service_set_power(1);
            s_bt_powered = 1;
            scan_tick    = 0;
            bt_state     = BT_STATE_SCANNING;
            bluetooth_service_scan_start();
            sel = 0; scroll = 0;
            return SCREEN_BLUETOOTH;
        case NCKEY_LEFT: case KEY_SOFT_LEFT_ACTION: case KEY_ACTION_SECONDARY:
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
                /* After removal the list is shorter. Clamp sel into the
                   new range — previously this used `>` instead of `>=`
                   so sel could land on count (off-by-one) and the next
                   draw would index past the end. */
                if (s_named_count == 0) {
                    sel = 0;
                } else if (sel >= (int)s_named_count) {
                    sel = (int)s_named_count - 1;
                }
            }
            return SCREEN_BLUETOOTH;
        case KEY_ACTION_PRIMARY:           /* A = connect/disconnect */
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
            return SCREEN_BLUETOOTH;
        case KEY_ACTION_SECONDARY:         /* D = remove device */
            bluetooth_service_remove(dev_mac);
            build_named_device_list();
            bt_state = BT_STATE_IDLE;
            if (sel > (int)s_named_count) sel = (int)s_named_count;
            return SCREEN_BLUETOOTH;
        case NCKEY_LEFT: case KEY_SOFT_LEFT_ACTION:
            bt_state = BT_STATE_IDLE;
            return SCREEN_BLUETOOTH;
        default:
            return SCREEN_BLUETOOTH;
        }
    }

    /* IDLE — named device list */
    build_named_device_list(); /* also calls compute_display_offsets() */

    switch (key)
    {
    case NCKEY_UP:
        if (sel > 0) sel--;
        return SCREEN_BLUETOOTH;
    case NCKEY_DOWN:
        if (sel < s_total_items - 1) sel++;
        return SCREEN_BLUETOOTH;

    case NCKEY_ENTER: case '\n':
    case KEY_ACTION_PRIMARY:           /* A = scan / open device */
        if (sel == 0)
        {
            scan_tick = 0;
            bt_state  = BT_STATE_SCANNING;
            bluetooth_service_scan_start();
        }
        else
        {
            const BtDevice *dev = get_device_at_display_index(sel);
            if (dev)
            {
                /* Stop scanning before inspecting/connecting — keeps audio clean */
                bluetooth_service_scan_stop();
                strncpy(dev_mac,  dev->mac,  sizeof(dev_mac)  - 1);
                {
                    const char *label = (dev->name[0] && !mac_is_safe(dev->name)) ? dev->name : dev->mac;
                    strncpy(dev_name, label, sizeof(dev_name) - 1);
                }
                dev_connected = dev->connected ? 1 : 0;
                dev_sel  = 0;
                bt_state = BT_STATE_DEVICE;
            }
        }
        return SCREEN_BLUETOOTH;

     /* LSK — back to settings (does NOT change BT power state) */
    case NCKEY_LEFT: case KEY_SOFT_LEFT_ACTION:
        bluetooth_service_scan_stop();
        /* Reset initialization flag so paired devices are reloaded on re-entry */
        bt_initialised = 0;
        return SCREEN_SETTINGS;

    /* RSK / D — power off */
    case KEY_SOFT_RIGHT_ACTION:
    case KEY_ACTION_SECONDARY:         /* D = power off */
        bluetooth_service_set_power(0);
        s_bt_powered = 0;
        bt_state     = BT_STATE_OFF;
        sel = 0; scroll = 0;
        return SCREEN_BLUETOOTH;

    default:
        return SCREEN_BLUETOOTH;
    }
}
