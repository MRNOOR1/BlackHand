# Black Hand OS Phase 2 Roadmap: Embedded Linux Mastery

**Target hardware**: Raspberry Pi 5 + HyperPixel 4.0 (800×480 DPI display) with future NXP migration. This roadmap transforms an RTOS developer into an embedded Linux phone OS architect through 12 progressive levels, each building modular, portable services.

The Phase 2 journey begins with Linux fundamentals and culminates in a fully functional phone operating system featuring voice calls, SMS, Bluetooth audio, touch UI, and offline speech recognition. Every component follows Service-Oriented Architecture principles ensuring clean separation for the Phase 3 NXP hardware port.

---

## Level 1: Embedded Linux environment and Buildroot foundation

### What this level achieves

You establish the complete development environment, configure Buildroot for Raspberry Pi 5, and create your first bootable image. This foundation ensures every subsequent component integrates cleanly into the build system.

### Learning prerequisites

- Linux command line proficiency (file manipulation, permissions, process management)
- Cross-compilation concepts (toolchains, sysroots, host vs target)
- Understanding of Buildroot's role as a build system (not a distribution)
- Git version control fundamentals

### Completion prerequisites

- Phase 1 complete (bare-metal RTOS experience on STM32)

### Incremental tasks

**Task 1.1**: Install Buildroot and create Pi 5 base configuration

```bash
git clone https://github.com/buildroot/buildroot.git
cd buildroot && make raspberrypi5_defconfig
make menuconfig  # Explore the interface
```

**Task 1.2**: Create br2-external tree structure for Black Hand OS

```
blackhand-external/
├── external.desc          # BR2_EXTERNAL_BLACKHAND_PATH
├── external.mk
├── Config.in
├── configs/blackhand_pi5_defconfig
├── package/               # Custom packages
└── board/blackhand/
    ├── overlay/           # Root filesystem overlay
    ├── post-build.sh
    └── genimage.cfg
```

**Task 1.3**: Configure essential Buildroot options

- Set `BR2_aarch64=y` and `BR2_cortex_a76=y` for Pi 5
- Enable `BR2_PACKAGE_HOST_DOSFSTOOLS=y` for boot partition
- Configure kernel with `BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION` pointing to Raspberry Pi kernel

**Task 1.4**: Build first image and verify boot

```bash
make BR2_EXTERNAL=../blackhand-external blackhand_pi5_defconfig
make -j$(nproc)
# Flash output/images/sdcard.img to SD card
```

**Task 1.5**: Establish serial console debugging

- Connect USB-to-serial adapter to GPIO 14/15
- Configure minicom/picocom at **115200** baud
- Verify kernel boot messages and login prompt

### Completion checklist

- [ ] Buildroot configured with br2-external for Black Hand OS
- [ ] Pi 5 boots to serial console login
- [ ] Kernel version 6.6+ with Pi 5 support confirmed
- [ ] Build time documented (baseline for optimization)
- [ ] Git repository initialized with .gitignore for Buildroot outputs

---

## Level 2: Display and touch input bring-up

### What this level achieves

HyperPixel 4.0 displays graphics via DPI interface, touch input works through the Goodix GT911 controller, and you have a working framebuffer for LVGL integration.

### Learning prerequisites

- Device Tree concepts (nodes, properties, overlays, phandles)
- Linux framebuffer subsystem (`/dev/fb0`, fbset, mmap)
- I2C protocol basics (addressing, transactions)
- Linux input subsystem (evdev, `/dev/input/eventX`)
- Understanding of Pi 5's RP1 southbridge chip architecture

### Completion prerequisites

- Level 1 complete (bootable Buildroot image)

### Incremental tasks

**Task 2.1**: Configure device tree overlay for HyperPixel 4.0

```ini
# config.txt additions
dtoverlay=vc4-kms-v3d
dtoverlay=vc4-kms-dpi-hyperpixel4
dtparam=i2c_arm=off  # Required - HyperPixel uses these GPIOs
dtparam=spi=off
```

**Task 2.2**: Enable kernel drivers for DPI and touch

```
CONFIG_DRM=y
CONFIG_DRM_VC4=y
CONFIG_INPUT_TOUCHSCREEN=y
CONFIG_TOUCHSCREEN_GOODIX=y
CONFIG_I2C_GPIO=y  # Software I2C for touch
```

**Task 2.3**: Verify framebuffer operation

```bash
fbset -fb /dev/fb0  # Should show 800x480
cat /dev/urandom > /dev/fb0  # Visual noise test
```

**Task 2.4**: Test touch input with evtest

```bash
evtest /dev/input/event0
# Touch screen and verify X,Y coordinate events
```

**Task 2.5**: Calibrate touch coordinates

- Record corner coordinates (0,0) and (799,479)
- Calculate transformation matrix if rotation needed
- Document calibration matrix: `0 -1 1 1 0 0 0 0 1` for 90° rotation

**Task 2.6**: Create display HAL abstraction header

```c
// hal/display_hal.h
typedef struct {
    int (*init)(void);
    int (*set_brightness)(int level);  // 0-100
    int (*power_on)(void);
    int (*power_off)(void);
} display_hal_t;
```

### Completion checklist

- [ ] HyperPixel displays framebuffer content at 800×480
- [ ] Touch events appear in evtest with correct coordinates
- [ ] Backlight controllable via `/sys/class/backlight/`
- [ ] Display HAL interface defined for NXP portability
- [ ] Device tree modifications documented

---

## Level 3: Custom init system and service manager

### What this level achieves

You replace BusyBox init with a custom PID 1 written in C, implement a service manager with dependency ordering, and establish the Unix socket IPC foundation for all inter-service communication.

### Learning prerequisites

- Linux init process responsibilities (PID 1 semantics, zombie reaping, signal handling)
- POSIX signal handling (`sigaction`, `SIGCHLD`, `SIGTERM`)
- Process creation (`fork`, `exec`, `setsid`, `waitpid`)
- Unix domain sockets (`AF_UNIX`, `SOCK_SEQPACKET`)
- JSON-RPC 2.0 protocol specification

### Completion prerequisites

- Level 1 complete (working Buildroot environment)

### Incremental tasks

**Task 3.1**: Write minimal init skeleton

```c
// blackhand_init.c
int main(int argc, char *argv[]) {
    if (getpid() != 1) exit(1);

    setup_signals();      // SIGCHLD handler for zombie reaping
    mount_vfs();          // proc, sysfs, devtmpfs, tmpfs
    spawn_service("/usr/bin/blackhand-svc-mgr", NULL);

    for (;;) {
        if (got_sigchld) reap_zombies();
        pause();
    }
}
```

**Task 3.2**: Implement virtual filesystem mounting

- Mount `/proc`, `/sys`, `/dev` (devtmpfs), `/run`, `/tmp`
- Handle mount failures gracefully with fallbacks

**Task 3.3**: Create service definition format

```yaml
# /etc/blackhand/services/example.service
name: example
exec: /usr/bin/example-daemon
user: root
dependencies: [audio, display]
restart_policy: always
health_check:
  type: socket
  path: /run/blackhand/example.sock
```

**Task 3.4**: Implement service manager with dependency resolution

- Topological sort for dependency ordering
- Parallel start of independent services
- Health check via socket connectivity
- Exponential backoff restart policy

**Task 3.5**: Create Unix socket server framework

```c
int create_service_socket(const char *path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 16);
    return fd;
}
```

**Task 3.6**: Integrate cJSON for JSON-RPC messaging

```c
// JSON-RPC request: {"jsonrpc":"2.0","method":"service.status","id":1}
// Response: {"jsonrpc":"2.0","result":{"state":"running"},"id":1}
```

**Task 3.7**: Configure Buildroot to use custom init

```makefile
BR2_INIT_NONE=y
BR2_PACKAGE_BLACKHAND_INIT=y
```

### Completion checklist

- [ ] Custom init boots successfully as PID 1
- [ ] Service manager starts/stops services correctly
- [ ] Dependencies resolved with topological sort
- [ ] JSON-RPC over Unix sockets functional
- [ ] Zombie processes properly reaped
- [ ] Boot time logged (target: identify baseline)

---

## Level 4: Read-only rootfs and persistent storage

### What this level achieves

Root filesystem becomes read-only SquashFS with OverlayFS for runtime modifications, a separate ext4 partition stores user data, and factory reset works by clearing the overlay.

### Learning prerequisites

- Linux filesystem hierarchy and mount points
- SquashFS (compressed, read-only filesystem)
- OverlayFS (union mount: lower read-only + upper read-write)
- Partition layout design for embedded systems
- ext4 mount options for SD card longevity (`noatime`, `commit=`)

### Completion prerequisites

- Level 3 complete (custom init functional)

### Incremental tasks

**Task 4.1**: Design partition layout

```
/dev/mmcblk0p1 - boot     (FAT32, 256MB) - kernel, DTB, config.txt
/dev/mmcblk0p2 - rootfs   (SquashFS, 128MB) - read-only system
/dev/mmcblk0p3 - overlay  (ext4, 64MB) - rootfs modifications
/dev/mmcblk0p4 - data     (ext4, remaining) - user data
```

**Task 4.2**: Configure Buildroot for SquashFS

```makefile
BR2_TARGET_ROOTFS_SQUASHFS=y
BR2_TARGET_ROOTFS_SQUASHFS_4K=y
BR2_TARGET_GENERIC_REMOUNT_ROOTFS_RW=n
```

**Task 4.3**: Implement early init OverlayFS setup

```sh
# Mount read-only rootfs
mount -o ro /dev/mmcblk0p2 /media/rfs/ro

# Mount overlay partition
mount -o rw /dev/mmcblk0p3 /media/rfs/rw
mkdir -p /media/rfs/rw/{upper,work}

# Create union mount
mount -t overlay overlay -o \
    lowerdir=/media/rfs/ro,\
    upperdir=/media/rfs/rw/upper,\
    workdir=/media/rfs/rw/work \
    /mnt/merged

pivot_root /mnt/merged /mnt/merged/mnt
```

**Task 4.4**: Create data partition directory structure

```
/mnt/data/
├── db/           # SQLite databases
├── media/        # Voice memos, photos
├── config/       # App configurations
└── cache/        # Temporary files
```

**Task 4.5**: Implement factory reset function

```c
int factory_reset(void) {
    sync();
    system("rm -rf /media/rfs/rw/upper/*");
    system("rm -rf /mnt/data/user/*");  // Optional: clear user data
    reboot(RB_AUTOBOOT);
}
```

**Task 4.6**: Configure genimage for multi-partition image

```
image sdcard.img {
    hdimage { partition-table-type = "msdos" }
    partition boot { partition-type = 0xC, image = "boot.vfat" }
    partition rootfs { partition-type = 0x83, image = "rootfs.squashfs" }
    partition overlay { partition-type = 0x83, size = 64M }
    partition data { partition-type = 0x83, size = 0 }  # Fill remaining
}
```

### Completion checklist

- [ ] Root filesystem mounts read-only
- [ ] OverlayFS provides transparent write capability
- [ ] User data persists across reboots in `/mnt/data`
- [ ] Factory reset clears overlay and optional user data
- [ ] Boot succeeds after intentional power loss (corruption test)

---

## Level 5: Audio subsystem and Bluetooth integration

### What this level achieves

USB audio adapter provides speaker/microphone capability, Bluetooth A2DP streams music to headphones, HFP enables hands-free calling, and a central audio service manages routing and mixing.

### Learning prerequisites

- ALSA architecture (cards, devices, PCM, mixer elements)
- ALSA C API (`snd_pcm_*`, `snd_mixer_*`)
- BlueZ stack architecture (bluetoothd, D-Bus API, profiles)
- A2DP (Advanced Audio Distribution Profile) for stereo streaming
- HFP (Hands-Free Profile) for bidirectional call audio

### Completion prerequisites

- Level 3 complete (service manager and IPC)
- Level 4 complete (persistent storage for paired device database)

### Incremental tasks

**Task 5.1**: Enable kernel USB audio support

```
CONFIG_SOUND=y
CONFIG_SND=y
CONFIG_SND_USB_AUDIO=y
```

**Task 5.2**: Configure ALSA for USB audio

```conf
# /etc/asound.conf
pcm.!default { type hw; card 0; device 0; }
ctl.!default { type hw; card 0; }
```

**Task 5.3**: Implement ALSA playback wrapper

```c
int audio_play_buffer(const int16_t *samples, size_t count) {
    snd_pcm_t *handle;
    snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    // Configure: 16-bit, 44100Hz, stereo, period 1024 frames
    snd_pcm_hw_params_set_*(...);
    snd_pcm_writei(handle, samples, count);
    snd_pcm_drain(handle);
    snd_pcm_close(handle);
}
```

**Task 5.4**: Implement ALSA mixer volume control

```c
int volume_set_percent(int percent) {
    snd_mixer_t *handle;
    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, "default");
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);
    // Find "Master" or "PCM" element, set volume
}
```

**Task 5.5**: Enable BlueZ-ALSA in Buildroot

```makefile
BR2_PACKAGE_BLUEZ5_UTILS=y
BR2_PACKAGE_BLUEZ_ALSA=y
```

**Task 5.6**: Implement Bluetooth pairing flow

```bash
# bluetoothctl sequence
power on
agent on
default-agent
scan on
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
connect XX:XX:XX:XX:XX:XX
```

**Task 5.7**: Create audio routing service

```c
typedef enum {
    ROUTE_SPEAKER,
    ROUTE_USB_AUDIO,
    ROUTE_BLUETOOTH_A2DP,
    ROUTE_BLUETOOTH_HFP,
    ROUTE_MODEM
} audio_route_t;

int audio_set_route(audio_route_t route);
```

**Task 5.8**: Implement MP3 decoding with libmpg123

```c
mpg123_handle *mh = mpg123_new(NULL, &err);
mpg123_open(mh, "file.mp3");
mpg123_getformat(mh, &rate, &channels, &encoding);
while (mpg123_read(mh, buffer, size, &done) == MPG123_OK) {
    audio_play_buffer(buffer, done / frame_size);
}
```

### Completion checklist

- [ ] USB audio plays WAV/MP3 files
- [ ] ALSA mixer controls volume (0-100%)
- [ ] Bluetooth device pairing successful
- [ ] A2DP streams audio to Bluetooth headphones
- [ ] Audio service exposes JSON-RPC API
- [ ] Audio HAL defined for NXP portability

---

## Level 6: LVGL user interface framework

### What this level achieves

LVGL renders on the Linux framebuffer, touch input integrates via evdev, the Vertu-inspired dark luxury theme is implemented, and basic phone screens (home, dialer pad) are functional.

### Learning prerequisites

- LVGL architecture (objects, styles, events, animations)
- Framebuffer rendering concepts (double buffering, partial updates)
- evdev input handling
- Color theory and typography for luxury UI design
- State management patterns for UI applications

### Completion prerequisites

- Level 2 complete (display and touch working)
- Level 5 complete (audio for UI feedback sounds)

### Incremental tasks

**Task 6.1**: Integrate LVGL 9.x into Buildroot

```makefile
LVGL_VERSION = 9.5.0
LVGL_CONF_OPTS = -DLV_USE_LINUX_FBDEV=1
```

**Task 6.2**: Initialize LVGL display driver

```c
lv_init();
lv_display_t *disp = lv_linux_fbdev_create();
lv_linux_fbdev_set_file(disp, "/dev/fb0");
```

**Task 6.3**: Configure touch input

```c
lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
lv_indev_set_display(touch, disp);
```

**Task 6.4**: Create Black Hand OS theme

```c
// Color palette (Vertu-inspired)
#define BH_BG_PRIMARY     lv_color_hex(0x0A0A0A)  // Near-black
#define BH_BG_ELEVATED    lv_color_hex(0x1E1E1E)  // Cards
#define BH_GOLD_PRIMARY   lv_color_hex(0xC9A962)  // Accents
#define BH_TEXT_PRIMARY   lv_color_hex(0xE8E8E8)  // Text
```

**Task 6.5**: Implement dialer keypad

```c
static const char *btnm_map[] = {
    "1", "2\nABC", "3\nDEF", "\n",
    "4\nGHI", "5\nJKL", "6\nMNO", "\n",
    "7\nPQRS", "8\nTUV", "9\nWXYZ", "\n",
    "*", "0\n+", "#", ""
};
lv_obj_t *keypad = lv_buttonmatrix_create(screen);
lv_buttonmatrix_set_map(keypad, btnm_map);
```

**Task 6.6**: Implement screen transition animations

```c
lv_screen_load_anim(new_screen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
```

**Task 6.7**: Create status bar component

- Time display (centered)
- Signal strength indicator (left)
- Battery percentage (right)

**Task 6.8**: Implement main loop with timer handling

```c
while (1) {
    uint32_t time_until_next = lv_timer_handler();
    usleep(time_until_next * 1000);
}
```

### Completion checklist

- [ ] LVGL renders at 60fps on HyperPixel
- [ ] Touch input responsive with correct coordinates
- [ ] Dark luxury theme with gold accents applied
- [ ] Dialer keypad displays and responds to touch
- [ ] Screen transitions animate smoothly
- [ ] Status bar shows time placeholder

---

## Level 7: SQLite database and settings service

### What this level achieves

SQLite stores contacts, SMS history, call logs, and notes with WAL mode for reliability. A centralized settings service manages system preferences and notifies subscribers of changes.

### Learning prerequisites

- SQLite C API (`sqlite3_open`, `sqlite3_prepare_v2`, `sqlite3_step`)
- Database normalization principles
- Write-Ahead Logging (WAL) for crash safety
- Observer pattern for settings change notifications

### Completion prerequisites

- Level 4 complete (persistent `/mnt/data` partition)
- Level 3 complete (IPC for settings notifications)

### Incremental tasks

**Task 7.1**: Create database initialization

```c
sqlite3 *db;
sqlite3_open("/mnt/data/db/phone.db", &db);
sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
```

**Task 7.2**: Define contacts schema

```sql
CREATE TABLE contacts (
    id INTEGER PRIMARY KEY,
    first_name TEXT NOT NULL,
    last_name TEXT,
    photo_path TEXT,
    created_at INTEGER DEFAULT (strftime('%s','now'))
);

CREATE TABLE phone_numbers (
    id INTEGER PRIMARY KEY,
    contact_id INTEGER REFERENCES contacts(id),
    number TEXT NOT NULL,
    normalized TEXT NOT NULL,  -- E.164 format
    type TEXT DEFAULT 'mobile'
);

CREATE INDEX idx_normalized ON phone_numbers(normalized);
```

**Task 7.3**: Define SMS schema

```sql
CREATE TABLE threads (
    id INTEGER PRIMARY KEY,
    phone_number TEXT NOT NULL,
    contact_id INTEGER,
    last_message_time INTEGER,
    unread_count INTEGER DEFAULT 0
);

CREATE TABLE messages (
    id INTEGER PRIMARY KEY,
    thread_id INTEGER REFERENCES threads(id),
    body TEXT,
    timestamp INTEGER,
    direction TEXT,  -- 'sent' or 'received'
    status TEXT      -- 'pending','sent','delivered','failed'
);
```

**Task 7.4**: Implement settings key-value store

```sql
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT,
    category TEXT
);

INSERT INTO settings VALUES
    ('volume_call', '80', 'audio'),
    ('volume_media', '70', 'audio'),
    ('brightness', '128', 'display'),
    ('pin_enabled', '0', 'security');
```

**Task 7.5**: Create settings service with change notifications

```c
int settings_set(const char *key, const char *value) {
    // Update database
    sqlite3_exec(db, "UPDATE settings SET value=? WHERE key=?", ...);
    // Broadcast change to subscribers
    settings_notify_change(key, value);
}
```

**Task 7.6**: Implement backlight control integration

```c
int display_set_brightness(int level) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", level * 255 / 100);
    FILE *f = fopen("/sys/class/backlight/*/brightness", "w");
    fputs(buf, f);
    fclose(f);
}
```

### Completion checklist

- [ ] SQLite database created with WAL mode
- [ ] Contacts CRUD operations functional
- [ ] SMS messages stored and retrieved correctly
- [ ] Settings service responds to JSON-RPC queries
- [ ] Settings changes broadcast to subscribers
- [ ] Backlight responds to brightness setting

---

## Level 8: Cellular modem integration

### What this level achieves

USB 4G modem (SIM7600 or Quectel EC25) connects for voice calls and SMS. AT command handling is robust with proper response parsing, and the call state machine manages all call scenarios.

### Learning prerequisites

- Serial port programming (termios, non-blocking I/O)
- AT command protocol (command structure, response codes, URCs)
- GSM call states (IDLE, DIALING, ALERTING, INCOMING, ACTIVE, HELD)
- SMS PDU encoding/decoding (GSM 7-bit, UCS-2)
- State machine design patterns

### Completion prerequisites

- Level 3 complete (service manager for modem service)
- Level 5 complete (audio routing for call audio)
- Level 7 complete (database for call history and SMS storage)

### Incremental tasks

**Task 8.1**: Enable kernel USB serial driver

```
CONFIG_USB_SERIAL=y
CONFIG_USB_SERIAL_OPTION=y
CONFIG_USB_NET_QMI_WWAN=y
```

**Task 8.2**: Create udev rule for modem enumeration

```udev
SUBSYSTEM=="tty", ATTRS{idVendor}=="2c7c", ATTRS{idProduct}=="0125", \
    SYMLINK+="modem_at", MODE="0666"
```

**Task 8.3**: Implement serial port configuration

```c
int configure_modem_port(int fd) {
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetspeed(&tty, B115200);
    tty.c_cflag = CS8 | CREAD | CLOCAL;  // 8N1, no flow control
    tty.c_lflag = 0;  // Raw mode
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;  // 1 second timeout
    tcsetattr(fd, TCSANOW, &tty);
}
```

**Task 8.4**: Implement AT command send/receive

```c
int at_send_command(modem_t *m, const char *cmd, char *resp, int timeout_ms) {
    write(m->fd, cmd, strlen(cmd));
    write(m->fd, "\r", 1);
    return at_read_response(m->fd, resp, timeout_ms);
}
```

**Task 8.5**: Handle unsolicited result codes (URCs)

```c
void process_urc(const char *urc) {
    if (strstr(urc, "RING")) {
        // Incoming call
    } else if (strstr(urc, "+CLIP:")) {
        // Caller ID: +CLIP: "+15551234567",145
    } else if (strstr(urc, "+CMT:")) {
        // Incoming SMS
    }
}
```

**Task 8.6**: Implement call state machine

```c
typedef enum {
    CALL_IDLE, CALL_DIALING, CALL_ALERTING,
    CALL_INCOMING, CALL_ACTIVE, CALL_HELD
} call_state_t;

void call_dial(const char *number) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);
    at_send_command(modem, cmd, response, 30000);
    call_state = CALL_DIALING;
}

void call_answer() {
    at_send_command(modem, "ATA", response, 10000);
    call_state = CALL_ACTIVE;
}

void call_hangup() {
    at_send_command(modem, "AT+CHUP", response, 5000);
    call_state = CALL_IDLE;
}
```

**Task 8.7**: Implement SMS sending (PDU mode)

```c
int sms_send(const char *number, const char *message) {
    // Encode PDU
    uint8_t pdu[256];
    int pdu_len = sms_encode_pdu(number, message, pdu);

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=%d", pdu_len);
    at_send_command(modem, cmd, response, 5000);
    // Wait for '>' prompt, then send PDU + Ctrl+Z
}
```

**Task 8.8**: Implement SMS receiving and storage

```c
void on_sms_received(const char *pdu) {
    sms_message_t msg;
    sms_decode_pdu(pdu, &msg);
    db_store_message(&msg);
    notification_post("SMS", msg.sender, msg.body, PRIORITY_HIGH);
}
```

### Completion checklist

- [ ] Modem detected and AT commands respond
- [ ] Outgoing calls dial and connect
- [ ] Incoming calls detected with caller ID
- [ ] Call audio routes through USB audio or Bluetooth HFP
- [ ] SMS sends successfully
- [ ] Incoming SMS stored in database
- [ ] Call history logged to database

---

## Level 9: Notification system and power management

### What this level achieves

A centralized notification service handles all alerts with priority-based interrupts (calls override everything). Power management implements screen timeouts, CPU frequency scaling, and battery monitoring for graceful shutdown.

### Learning prerequisites

- Linux power management (sysfs interfaces, cpufreq governors)
- Battery fuel gauge protocols (I2C communication, state-of-charge calculation)
- Priority queue data structures
- LVGL overlay layers (`lv_layer_top()` for notifications)

### Completion prerequisites

- Level 6 complete (LVGL for notification UI)
- Level 7 complete (settings for timeout values)
- Level 8 complete (modem for call notifications)

### Incremental tasks

**Task 9.1**: Design notification structure

```c
typedef enum {
    PRIORITY_LOW,      // Silent
    PRIORITY_NORMAL,   // Sound
    PRIORITY_HIGH,     // Sound + vibrate
    PRIORITY_CRITICAL  // Incoming call - overrides all
} notification_priority_t;

typedef struct {
    uint32_t id;
    char *title;
    char *body;
    notification_priority_t priority;
    time_t timestamp;
} notification_t;
```

**Task 9.2**: Implement notification queue

```c
void notification_post(notification_t *n) {
    if (n->priority == PRIORITY_CRITICAL) {
        // Insert at head, interrupt current activity
        queue_insert_head(&notif_queue, n);
        ui_show_call_overlay(n);
    } else {
        queue_enqueue(&notif_queue, n);
        ui_show_notification_banner(n);
    }
}
```

**Task 9.3**: Create incoming call overlay

```c
void ui_show_call_overlay(notification_t *n) {
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, 800, 480);
    // Add caller name, accept/decline buttons
    // Make modal (capture all input)
}
```

**Task 9.4**: Implement screen timeout state machine

```c
typedef enum {
    SCREEN_ON, SCREEN_DIM, SCREEN_OFF
} screen_state_t;

void power_tick() {
    int idle = time(NULL) - last_activity;
    if (idle > 20 && screen_state == SCREEN_ON) {
        set_brightness(30);
        screen_state = SCREEN_DIM;
    }
    if (idle > 30 && screen_state == SCREEN_DIM) {
        set_brightness(0);
        fb_blank(1);
        screen_state = SCREEN_OFF;
    }
}
```

**Task 9.5**: Configure CPU frequency governor

```c
void power_set_mode(power_mode_t mode) {
    const char *gov = (mode == POWER_SAVE) ? "powersave" : "ondemand";
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "w");
    fputs(gov, f);
    fclose(f);
}
```

**Task 9.6**: Implement battery monitoring (PiSugar I2C)

```c
#define PISUGAR_ADDR 0x57

int battery_read_percent(int i2c_fd) {
    uint8_t reg = 0x2A;  // Battery percentage register
    write(i2c_fd, &reg, 1);
    uint8_t percent;
    read(i2c_fd, &percent, 1);
    return percent;
}
```

**Task 9.7**: Implement low battery warning and shutdown

```c
void battery_monitor_thread() {
    while (1) {
        int percent = battery_read_percent(i2c_fd);
        if (percent <= 5 && !shutdown_warned) {
            notification_post("Low Battery", "5% remaining", PRIORITY_HIGH);
            shutdown_warned = true;
        }
        if (percent <= 2) {
            sync();
            reboot(RB_POWER_OFF);
        }
        sleep(30);
    }
}
```

### Completion checklist

- [ ] Notifications display as banners
- [ ] Incoming call shows fullscreen overlay
- [ ] Screen dims after 20 seconds of inactivity
- [ ] Screen turns off after 30 seconds
- [ ] Touch/button wakes screen
- [ ] Battery percentage reads correctly
- [ ] Low battery warning at 5%
- [ ] Graceful shutdown at 2%

---

## Level 10: Security implementation

### What this level achieves

PIN lock protects device access with Argon2id hashing, user data partition optionally encrypts with dm-crypt/LUKS, and secure storage protects sensitive credentials.

### Learning prerequisites

- Password hashing (Argon2id algorithm, salt, time/memory parameters)
- Linux disk encryption (dm-crypt, LUKS, cryptsetup)
- Key derivation functions
- Secure coding practices (constant-time comparison, memory zeroing)

### Completion prerequisites

- Level 4 complete (partition layout)
- Level 6 complete (LVGL for PIN entry UI)
- Level 7 complete (settings for PIN enabled/hash storage)

### Incremental tasks

**Task 10.1**: Implement PIN hashing with Argon2id

```c
#include <argon2.h>

#define SALT_LEN 16
#define HASH_LEN 32

int hash_pin(const char *pin, uint8_t *salt, uint8_t *hash_out) {
    return argon2id_hash_raw(
        3,           // time cost (iterations)
        1 << 15,     // memory cost (32 MB)
        4,           // parallelism
        pin, strlen(pin),
        salt, SALT_LEN,
        hash_out, HASH_LEN
    );
}
```

**Task 10.2**: Create PIN entry UI

```c
void ui_show_pin_entry() {
    // 4-6 digit PIN entry with numeric keypad
    // Show dots for entered digits
    // Shake animation on wrong PIN
    // Lockout after 5 failed attempts
}
```

**Task 10.3**: Implement PIN verification

```c
int verify_pin(const char *entered_pin) {
    uint8_t stored_salt[SALT_LEN], stored_hash[HASH_LEN];
    load_pin_credentials(stored_salt, stored_hash);

    uint8_t computed_hash[HASH_LEN];
    hash_pin(entered_pin, stored_salt, computed_hash);

    // Constant-time comparison
    return crypto_verify_32(computed_hash, stored_hash);
}
```

**Task 10.4**: Implement lockout mechanism

```c
#define MAX_ATTEMPTS 5
static int failed_attempts = 0;
static time_t lockout_until = 0;

int attempt_unlock(const char *pin) {
    if (time(NULL) < lockout_until) return -ELOCKED;

    if (!verify_pin(pin)) {
        failed_attempts++;
        if (failed_attempts >= MAX_ATTEMPTS) {
            lockout_until = time(NULL) + (30 * (1 << (failed_attempts - MAX_ATTEMPTS)));
        }
        return -EWRONGPIN;
    }

    failed_attempts = 0;
    return 0;
}
```

**Task 10.5**: Configure LUKS encryption for data partition

```bash
# Initial setup (one-time)
cryptsetup luksFormat --type luks2 --pbkdf argon2id /dev/mmcblk0p4

# Unlock at boot
cryptsetup luksOpen /dev/mmcblk0p4 data_crypt
mount /dev/mapper/data_crypt /mnt/data
```

**Task 10.6**: Derive LUKS key from PIN

```c
int unlock_data_partition(const char *pin) {
    uint8_t key[64];
    derive_key_from_pin(pin, device_salt, key, sizeof(key));

    struct crypt_device *cd;
    crypt_init(&cd, "/dev/mmcblk0p4");
    crypt_load(cd, CRYPT_LUKS2, NULL);
    int ret = crypt_activate_by_passphrase(cd, "data_crypt",
        CRYPT_ANY_SLOT, key, sizeof(key), 0);

    explicit_bzero(key, sizeof(key));
    crypt_free(cd);
    return ret;
}
```

### Completion checklist

- [ ] PIN setup and change functional
- [ ] PIN verification uses Argon2id
- [ ] Lockout activates after 5 failed attempts
- [ ] Lock screen appears on boot and after timeout
- [ ] LUKS encryption optional but functional
- [ ] Credentials stored with proper permissions (600)

---

## Level 11: Voice-to-text integration

### What this level achieves

Vosk provides real-time speech recognition for voice commands and voice dialing. Whisper.cpp handles batch transcription for voice memos. Voice Activity Detection triggers recognition efficiently.

### Learning prerequisites

- Speech recognition fundamentals (acoustic models, language models)
- Audio buffering (circular buffers, producer-consumer pattern)
- Voice Activity Detection algorithms
- Thread synchronization for audio pipelines

### Completion prerequisites

- Level 5 complete (audio capture via ALSA)
- Level 8 complete (modem for voice dialing)

### Incremental tasks

**Task 11.1**: Integrate Vosk library into Buildroot

```makefile
VOSK_VERSION = 0.3.50
VOSK_SITE = https://alphacephei.com/vosk/models
# Use pre-built ARM64 library + small English model
```

**Task 11.2**: Download and integrate small English model

```bash
wget https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip
# ~40MB, suitable for Pi 5
```

**Task 11.3**: Implement Vosk recognition wrapper

```c
VoskModel *model = vosk_model_new("vosk-model-small-en-us-0.15");
VoskRecognizer *rec = vosk_recognizer_new(model, 16000);

// Process audio chunks (16kHz, 16-bit PCM)
if (vosk_recognizer_accept_waveform(rec, buffer, len)) {
    const char *result = vosk_recognizer_result(rec);
    // Parse JSON: {"text": "call mom"}
}
```

**Task 11.4**: Implement Voice Activity Detection

```c
#include <fvad.h>

Fvad *vad = fvad_new();
fvad_set_sample_rate(vad, 16000);
fvad_set_mode(vad, 3);  // Aggressive

// Process 10ms frames
int is_speech = fvad_process(vad, frame, 160);
```

**Task 11.5**: Create STT service with IPC interface

```c
// JSON-RPC interface
// Request: {"method": "stt.recognize", "params": {"mode": "streaming"}}
// Response: {"result": {"text": "call mom", "confidence": 0.95}}
```

**Task 11.6**: Implement voice command parsing

```c
void handle_voice_command(const char *text) {
    if (strncmp(text, "call ", 5) == 0) {
        const char *contact_name = text + 5;
        contact_t *c = db_find_contact_by_name(contact_name);
        if (c) modem_dial(c->phone_number);
    } else if (strcmp(text, "send message") == 0) {
        ui_navigate_to(SCREEN_SMS_COMPOSE);
    }
}
```

**Task 11.7**: Integrate Whisper.cpp for voice memo transcription

```c
struct whisper_context *ctx = whisper_init_from_file("ggml-tiny.en-q5_1.bin");
struct whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
params.n_threads = 4;  // Pi 5 has 4 cores

whisper_full(ctx, params, audio_samples, n_samples);

int n_segments = whisper_full_n_segments(ctx);
for (int i = 0; i < n_segments; i++) {
    const char *text = whisper_full_get_segment_text(ctx, i);
    // Append to memo transcription
}
```

### Completion checklist

- [ ] Vosk model loads successfully (~300MB RAM)
- [ ] Real-time speech recognition responds in <500ms
- [ ] "Call [name]" voice command initiates calls
- [ ] VAD prevents continuous processing during silence
- [ ] Whisper transcribes voice memos to text
- [ ] STT service exposes JSON-RPC API

---

## Level 12: Phone applications and system polish

### What this level achieves

Complete phone applications (Dialer, Messages, Contacts, Voice Memos, Notes, Settings) are functional with polished UI. Boot time optimizes to under 5 seconds. The system is ready for daily use and Phase 3 hardware migration.

### Learning prerequisites

- UI/UX design principles for phone interfaces
- Performance profiling and optimization
- System integration testing
- Documentation best practices

### Completion prerequisites

- Levels 1-11 complete

### Incremental tasks

**Task 12.1**: Complete Dialer application

- Number display with large, elegant digits
- Call/delete/contacts action buttons
- In-call screen with duration timer
- Speakerphone toggle, mute, keypad access

**Task 12.2**: Complete Messages application

- Conversation list sorted by recent
- Message thread view with bubbles
- Compose screen with soft keyboard
- Delivery status indicators

**Task 12.3**: Complete Contacts application

- Alphabetical scrolling list
- Contact detail view with photo
- Edit/delete capabilities
- Quick actions (call, message)

**Task 12.4**: Complete Voice Memos application

- Record button with waveform visualization
- Recording list with timestamps
- Playback controls
- Optional transcription display

**Task 12.5**: Complete Notes application

- Note list view
- Full-screen editor
- Auto-save on exit

**Task 12.6**: Complete Settings application

- Organized categories (Audio, Display, Connectivity, Security, About)
- Real-time setting application
- Factory reset confirmation

**Task 12.7**: Optimize boot time (<5 seconds target)

```
# Kernel optimizations
quiet loglevel=0 lpj=<measured_value>

# Buildroot: minimal kernel, no debug
CONFIG_PRINTK_TIME=n
CONFIG_DEBUG_INFO=n
```

- Parallel service startup
- Deferred initialization for non-critical services
- Profile with `systemd-analyze` equivalent or custom timestamps

**Task 12.8**: Implement watchdog for system stability

```c
int wdt_fd = open("/dev/watchdog", O_RDWR);
int timeout = 30;
ioctl(wdt_fd, WDIOC_SETTIMEOUT, &timeout);

// Kick in main loop
ioctl(wdt_fd, WDIOC_KEEPALIVE, NULL);
```

**Task 12.9**: Create hardware abstraction documentation

- Document all HAL interfaces
- Map Pi 5 specifics to abstract interfaces
- Prepare for NXP i.MX migration

**Task 12.10**: System integration testing

- Call flow: dial → connect → talk → hangup
- SMS flow: compose → send → receive → notify
- Bluetooth: pair → stream music → answer call → resume music
- Power: timeout → sleep → wake → unlock

### Completion checklist

- [ ] All phone apps functional with Vertu aesthetic
- [ ] Boot to home screen in <5 seconds
- [ ] Watchdog prevents system hangs
- [ ] All services start correctly on boot
- [ ] Factory reset works completely
- [ ] HAL documentation complete for NXP port
- [ ] 24-hour stability test passes

---

## Summary: Level dependencies and timeline

```
Level 1 ──→ Level 2 ──→ Level 6
   │           │
   └──→ Level 3 ──→ Level 4 ──→ Level 7 ──→ Level 8 ──→ Level 9 ──→ Level 12
          │                        │                        │
          └──→ Level 5 ────────────┴──→ Level 11            │
                                                            │
                                        Level 10 ───────────┘
```

**Estimated timeline** (experienced developer, part-time):

- Levels 1-4: 4-6 weeks (foundation)
- Levels 5-7: 4-6 weeks (subsystems)
- Levels 8-10: 6-8 weeks (telephony, power, security)
- Levels 11-12: 4-6 weeks (STT, polish)
- **Total: 4-6 months**

**Hardware bill of materials**:

- Raspberry Pi 5 (8GB recommended)
- Pimoroni HyperPixel 4.0 (rectangular)
- USB audio adapter (UAC1/UAC2 compliant)
- 4G USB modem (Quectel EC25 or SIMCom SIM7600)
- PiSugar 3 Plus battery HAT
- MicroSD card (32GB+, A2 rating)
- USB-to-serial adapter for debugging

This roadmap provides a structured path from bare-metal RTOS experience to a complete embedded Linux phone OS, with every component designed for modularity and future hardware migration.
