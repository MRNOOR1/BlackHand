# Agent Brief — Bring up the SPI display on Black Hand OS

**Status:** hardware is proven working on Raspberry Pi OS with the vendor demo.
The Buildroot integration must be (re)built — the repo was reset and currently
contains the old HyperPixel DPI configuration, which will not drive this panel.

**Goal:** BlackHand boots to its notcurses UI on the SPI panel, with the UI
fully inside the panel's rounded borders.

Everything below was measured on the actual hardware in this project. Where a
value was *derived* rather than measured it says so. Do not substitute
plausible-looking numbers from datasheets for the measured ones — several of
the datasheet-derived values in this document's history were wrong.

---

## 1. The hardware

| | |
|---|---|
| Module | Waveshare 1.83inch LCD Module (SPI) |
| Host | Raspberry Pi 4 Model B |
| **Visible resolution** | **240 × 280** — MEASURED, see §1.1 |
| **RAM start row** | **0** (no offset) — MEASURED |
| Controller | ST7789-family, 240×320 GRAM. See §1.2 — silicon not conclusively identified |
| Interface | 4-wire SPI, **write-only** (MISO not on the header) |
| Colour | RGB565 over the wire |
| Corner radius | ~28 px, rounded — **the UI must respect this**, see §6.3 |
| Touch | **NOT USED.** Leave all TP_* pins unconnected |

### 1.1 How the geometry was measured

Waveshare ship two drivers that address the same controller RAM differently:

| Vendor driver | width × height | Y offset | RAM rows written |
|---|---|---|---|
| `LCD_1inch83.py` | 240 × 280 | **+20** | 20 – 299 |
| `LCD_2inch.py` | 240 × 320 | **0** | 0 – 319 |

On this panel `2inch_LCD_test.py` covers the whole screen and
`1inch83_LCD_test.py` does not. That proves the visible area **starts at RAM
row 0** — the `+20` offset pushes the image down and leaves a blank strip.

A banded probe (20-px bands labelled with their row number, drawn through the
2inch driver's 1:1 RAM mapping) showed **260 as the last visible label**. Band
260 spans rows 260–279, so:

> **Visible area = 240 × 280, RAM rows 0–279, no offset.**

### 1.2 Controller identity is NOT settled

240×280 is Waveshare's **Rev1** geometry, which pairs with the **NV3030B**
controller. But Rev1's own driver applies the `+20` offset that this panel
demonstrably does not want. Rev2 is ST7789P at 240×284.

MISO is not wired, so the controller ID cannot be read back — it can only be
identified visually (a "Rev2" mark on the back; 8-pin ribbon = non-touch,
12/18-pin = touch).

**Practical guidance:** start with the ST7789P sequence. If the panel stays
dark while SPI is confirmed transmitting (§7.2), switch to NV3030B. Both
sequences are given in §4.

---

## 2. Reference material

| What | Where |
|---|---|
| Module wiki (pinout, specs, demo) | https://www.waveshare.com/wiki/1.83inch_LCD_Module |
| Touch variant docs | https://docs.waveshare.com/1.83inch_Touch_LCD_Module |
| Vendor demo code (C + Python, all platforms) | https://files.waveshare.com/wiki/1.83inch-LCD-Module/LCD_1.83_Code.zip |
| `panel-mipi-dbi` driver + blob format | `drivers/gpu/drm/tiny/panel-mipi-dbi.c` in the RPi kernel |
| Upstream blob tooling / wiki | https://github.com/notro/panel-mipi-dbi |
| RPi overlay parameters | `arch/arm/boot/dts/overlays/README`, section `mipi-dbi-spi` |
| Waveshare's own ST7789 fbtft driver (init values) | https://github.com/waveshareteam/LCD-show/blob/master/st7789_module/fb_st7789v.c |

Note the wiki's demo ZIP is the **Rev1 / NV3030B** code. Its C driver
(`RaspberryPi/c/lib/LCD/LCD_1in83.c`) declares `HEIGHT 280` and hardcodes `+20`
in `LCD_1IN83_SetWindows()`.

---

## 3. Wiring — VERIFIED WORKING

Matches Waveshare's Raspberry Pi table. Touch pins unconnected.

| Module pad | Pi signal | BCM | **Pi physical pin** |
|---|---|---|---|
| VCC | 3.3 V | — | **1** |
| GND | GND | — | **6** |
| DIN / MOSI | SPI0_MOSI | 10 | **19** |
| CLK / SCLK | SPI0_SCLK | 11 | **23** |
| CS | SPI0_CE0 | 8 | **24** |
| DC | GPIO | 25 | **22** |
| RST | GPIO | 27 | **13** |
| BL | GPIO | **18** | **12** |
| MISO | — | — | **not connected** |

**VCC must be 3.3 V, not 5 V.** Waveshare: *"ensure that the supply voltage and
logic voltage are consistent, otherwise it may fail to function properly."* The
Pi drives 3.3 V logic.

**BL is BCM 18 = physical pin 12.** An earlier revision of this project used BCM
24 / physical pin 18 — the numbers 18 and 24 swap roles between the two
schemes, which is very easy to mis-wire. Using GPIO 18 with `gpio-backlight`
does **not** touch the PWM block, so onboard headphone audio is unaffected.
Only `pwm-backlight` would break audio.

**Pins 19 / 21 / 23 are adjacent on the same row.** DIN on 21 (MISO) instead of
19 gives a completely silent failure: SPI enumerates, the driver binds,
megabytes transmit with zero errors, and nothing ever reaches the glass. This
cost this project several hours. Verify by counting, twice.

---

## 4. Panel init blob (`panel.bin`)

`panel-mipi-dbi` knows no panel registers; it loads the entire init sequence
from a firmware blob. Format, from `panel-mipi-dbi.c`:

```
offset  0   15 bytes   magic "MIPI DBI" + 7 NUL
offset 15    1 byte    file format version, must be 1
offset 16    n bytes   command stream
```

Command stream is flat: `command, param_count, [params...]`. A delay is the
MIPI No-Op (`0x00`) carrying one byte of milliseconds — hence delays cap at
255. The kernel validates magic, version, and that the array is exactly
consumed, and rejects a bad file with a clear dmesg line.

Build it with a small generator (stdlib Python is enough — do not take a
dependency on the upstream tool). Recommended layout:

```
board/blackhand/panel/panel.txt            ST7789P sequence
board/blackhand/panel/panel-nv3030b.txt    NV3030B sequence
board/blackhand/panel/mkpanelbin.py        text -> panel.bin
board/blackhand/panel/use-panel.sh         switch variant + geometry together
```

### 4.1 ST7789P sequence (try first)

Derived from Waveshare's fbtft driver, HSD20_IPS branch. `INVON (0x21)` is
mandatory on this IPS panel — omit it and every colour inverts.

```
command 0x11                                     # SLPOUT
delay   120
command 0x3A 0x05                                # COLMOD RGB565
command 0x36 0x00                                # MADCTL portrait RGB
command 0xB2 0x05 0x05 0x00 0x33 0x33            # PORCTRL
command 0xB7 0x75                                # GCTRL
command 0xC2 0x01 0xFF                           # VDVVRHEN
command 0xC3 0x13                                # VRHS
command 0xC4 0x20                                # VDVS
command 0xBB 0x22                                # VCOMS
command 0xC5 0x20                                # VCMOFSET
command 0xD0 0xA4 0xA1                           # PWCTRL1
command 0xE0 0xD0 0x05 0x0A 0x09 0x08 0x05 0x2E 0x44 0x45 0x0F 0x17 0x16 0x2B 0x33
command 0xE1 0xD0 0x05 0x0A 0x09 0x08 0x05 0x2E 0x43 0x45 0x0F 0x16 0x16 0x2B 0x33
command 0x21                                     # INVON — REQUIRED
command 0x13                                     # NORON
command 0x29                                     # DISPON
delay   100
```

### 4.2 NV3030B sequence (fallback)

Transcribed from `LCD_1IN83_InitReg()` in the vendor ZIP. Note the `0x61`–`0x68`
and `0xE0`–`0xEc` banks — no ST7789 equivalent; the two are not interchangeable.

```
command 0xfd 0x06 0x08
command 0x61 0x07 0x04
command 0x62 0x00 0x44 0x45
command 0x63 0x41 0x07 0x12 0x12
command 0x64 0x37
command 0x65 0x09 0x10 0x21
command 0x66 0x09 0x10 0x21
command 0x67 0x20 0x40
command 0x68 0x90 0x4c 0x7C 0x66
command 0xb1 0x0F 0x02 0x01
command 0xB4 0x01
command 0xB5 0x02 0x02 0x0a 0x14
command 0xB6 0x04 0x01 0x9f 0x00 0x02
command 0xdf 0x11
command 0xE2 0x13 0x00 0x00 0x30 0x33 0x3f
command 0xE5 0x3f 0x33 0x30 0x00 0x00 0x13
command 0xE1 0x00 0x57
command 0xE4 0x58 0x00
command 0xE0 0x01 0x03 0x0e 0x0e 0x0c 0x15 0x19
command 0xE3 0x1a 0x16 0x0C 0x0f 0x0e 0x0d 0x02 0x01
command 0xE6 0x00 0xff
command 0xE7 0x01 0x04 0x03 0x03 0x00 0x12
command 0xE8 0x00 0x70 0x00
command 0xEc 0x52
command 0xF1 0x01 0x01 0x02
command 0xF6 0x09 0x10 0x00 0x00
command 0xfd 0xfa 0xfc
command 0x3a 0x05
command 0x35 0x00
command 0x21
command 0x11
delay   200
command 0x29
delay   10
command 0x36 0x08
```

Hardware reset, window addressing and backlight are the driver's job — do not
duplicate them in the blob.

---

## 5. Boot and kernel configuration

### 5.1 `board/blackhand/rpi-firmware/config.txt`

Delete every HyperPixel DPI line: `enable_dpi_lcd`, `dpi_group`, `dpi_mode`,
`dpi_output_format`, `dpi_timings`, `framebuffer_width/height`,
`display_rotate`, the `i2c-gpio` overlay and the `hyperpixel4-touch` overlay.

```
arm_64bit=1
kernel=Image
gpu_mem=64

dtparam=spi=on                                   # MUST precede the dtoverlay

dtoverlay=mipi-dbi-spi,spi0-0,speed=8000000
dtparam=write-only
dtparam=width=240,height=280
dtparam=x-offset=0,y-offset=0
dtparam=reset-gpio=27,dc-gpio=25
dtparam=backlight-gpio=18

dtparam=audio=on
enable_uart=1
disable_splash=1
boot_delay=0
```

- `dtparam=spi=on` **before** the overlay. A `dtparam` after a `dtoverlay` is
  read as a parameter *of that overlay*, and `mipi-dbi-spi` has no `spi` param.
- `write-only` is **required** — MISO is not wired. Without it the driver tries
  to read the controller ID and probe fails.
- `speed=8000000` matches Waveshare's most conservative demo setting. Their
  drivers use 7.8 MHz (bcm2835 divider 32) or 25 MHz, never above 25. The
  onboard level shifter is the limit, not the controller. `panel-mipi-dbi`
  always clocks *init commands* at 10 MHz regardless; this value only affects
  pixel data. Raise it only after the panel is known good.
- **`gpu_mem` must be ≥ 32.** Below that the VideoCore demands the cutdown
  firmware blobs (`start4cd.elf`/`fixup4cd.dat`) which Buildroot's `rpi-firmware`
  package does not install, and it halts silently before Linux loads. `16` will
  produce a dead board with no output whatsoever.

### 5.2 `board/blackhand/linux/bcm2711.config`

> **A Buildroot kernel fragment is MERGED onto `bcm2711_defconfig`. Deleting a
> line does NOT unset the option.** Anything that must be off needs an explicit
> `# CONFIG_X is not set`. This is the single most expensive mistake available
> in this file.

Must be explicitly disabled (all are `=y`/`=m` in the base defconfig):

```
# CONFIG_FB_BCM2708 is not set      # VideoCore firmware fb — would claim fb0
# CONFIG_FB_SIMPLE is not set       # ditto
# CONFIG_TOUCHSCREEN_GOODIX is not set
# CONFIG_I2C_GPIO is not set
```

If either framebuffer survives, it registers first, fbcon binds to *it*, and
the SPI panel ends up as `fb1` with no console — a black screen that looks
exactly like a driver failure. With both off the panel is the only framebuffer
and fbcon takes it over automatically; **no `fbcon=map:` is needed or wanted.**

Must be enabled:

```
CONFIG_SPI=y
CONFIG_SPI_MASTER=y
CONFIG_SPI_BCM2835=y

CONFIG_DRM=y
CONFIG_DRM_KMS_HELPER=y
CONFIG_DRM_GEM_DMA_HELPER=y
CONFIG_DRM_MIPI_DBI=y
CONFIG_DRM_PANEL_MIPI_DBI=y
CONFIG_DRM_FBDEV_EMULATION=y      # creates the /dev/fb fbcon needs

CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_BACKLIGHT_GPIO=y           # gpio-backlight, NOT pwm-backlight

CONFIG_FW_LOADER=y
CONFIG_FONTS=y
CONFIG_FONT_8x16=y                # VGA8x16 — the chosen console font
CONFIG_FONT_6x11=y                # ProFont6x11 — kept as an alternative

# Kernel modules must not be compressed — see §8.3
CONFIG_MODULE_COMPRESS_NONE=y
# CONFIG_MODULE_COMPRESS_XZ is not set
# CONFIG_MODULE_COMPRESS_GZIP is not set
# CONFIG_MODULE_COMPRESS_ZSTD is not set
```

Name the DRM helpers explicitly rather than relying on
`DRM_PANEL_MIPI_DBI`'s `select`: a select from a tristate can resolve to `=m`
and reintroduce a module dependency chain.

### 5.3 Built-in vs module — pick one deliberately

**Built-in (`=y`), recommended for the final image.** A built-in driver probes
before the rootfs is mounted, so `request_firmware("panel.bin")` returns
`-ENOENT` and never retries. The blob must therefore be linked into the kernel:

```
CONFIG_EXTRA_FIRMWARE="panel.bin"
CONFIG_EXTRA_FIRMWARE_DIR="firmware"
```

`EXTRA_FIRMWARE_DIR` has no leading `/`, so the kernel resolves it against
`$(srctree)` (see `drivers/base/firmware_loader/builtin/Makefile`). Add a
Buildroot pre-build hook in `external.mk` to place the blob there:

```make
define BLACKHAND_INSTALL_PANEL_FIRMWARE
	$(INSTALL) -D -m 0644 \
		$(BR2_EXTERNAL_BLACKHAND_PATH)/board/blackhand/overlay/lib/firmware/panel.bin \
		$(LINUX_DIR)/firmware/panel.bin
endef
LINUX_PRE_BUILD_HOOKS += BLACKHAND_INSTALL_PANEL_FIRMWARE
```

Iterating on the sequence then costs `make linux-rebuild all` plus copying
`Image` to the FAT boot partition — a couple of minutes, not a full rebuild.

**Module (`=m`)** is viable *only* with `CONFIG_MODULE_COMPRESS_NONE=y`, and
lets you retune by dropping a new `/lib/firmware/panel.bin` on the card. If you
choose it, `rcS` must `modprobe panel-mipi-dbi` after the rootfs is mounted,
and should fall back to resolving `modules.dep` by hand — `insmod` does not
resolve dependencies and will fail with `Unknown symbol drm_gem_dma_dumb_create`.

### 5.4 `cmdline.txt`

```
console=serial0,115200 console=tty3 loglevel=1 quiet root=/dev/mmcblk0p2 \
rootfstype=ext4 rw fsck.repair=yes rootwait vt.global_cursor_default=0 \
init=/sbin/blackhand-init fbcon=font:VGA8x16 snd_bcm2835.enable_headphones=1
```

- `fbcon=font:VGA8x16` — the exact name the kernel matches on. Names are not
  guessable: the 6×11 font is `ProFont6x11`, not `6x11`.
- **No `fbcon=map:1`.** With a single framebuffer it maps the console to a
  non-existent `fb1` and you get a working panel with no console on it.
- `console=serial0,115200` costs nothing and is the only way to see a panic.

---

## 6. The UI — 30 × 17, inside rounded borders

### 6.1 Console geometry

```
240 px / 8 px  = 30 columns    (VGA8x16)
280 px / 16 px = 17 rows       (272 px used, 8 px unlit at the bottom)
```

280 has no factor that fits a sensible font height, so a few unused pixels at
the bottom are unavoidable. 8 px of 280 is under 3%.

In `package/blackhand-ui/src/config.h`:

```c
#define PHONE_COLS        30
#define PHONE_SCREEN_ROWS 17
```

Screens read their extent via `ncplane_dim_yx()` rather than hardcoding, so
most adapt without per-screen edits. Confirm no popup exceeds the width —
`ALARM_TIME_POPUP_WIDTH` and `ALARM_RING_POPUP_WIDTH` were 32 and 34 and must
come down to ≤ 28.

Verify on hardware: `stty -F /dev/tty1 size` must print `17 30`.

### 6.2 There are two UI trees and they have diverged

`test-ui/` and `package/blackhand-ui/src/` are **not** the same code — each has
screens the other lacks. **`package/blackhand-ui/src/` is what builds.** Keep
both in step for shared constants, or reconcile them; editing the wrong one is
a live footgun.

### 6.3 Rounded corners — the UI must stay inside them

The panel's corners are rounded, radius ≈ **28 px**. In character cells at 8×16
that is **3.5 columns wide and 1.75 rows tall**. Geometrically, for a corner of
radius R the horizontal inset at height *y* into the corner is
`R - sqrt(R² - (R-y)²)`:

| Pixel row into the corner | Inset | In columns |
|---|---|---|
| 0 (very top) | 28 px | 3.5 |
| 8 (middle of char row 0) | ~8 px | 1.0 |
| 16 (top of char row 1) | ~2 px | 0.25 |
| 24 | ~0.3 px | ~0 |

So **only character row 0 and row 16 are meaningfully clipped**, losing up to
~3.5 columns at each end. Rows 1–15 are effectively full width.

Requirement: **no glyph may be drawn in the corner cells.** Concretely, on the
first and last character rows, columns 0–3 and 26–29 are unsafe. Acceptable
approaches, in order of preference:

1. **Inset the frame.** Draw the phone frame's top and bottom borders from
   column 4 to column 25, with the corner turns moved inward — a rounded frame
   that mirrors the glass.
2. **Reserve the outer ring.** Keep the plane 30 × 17 but treat row 0, row 16,
   column 0 and column 29 as bleed: draw only background there, never glyphs.
3. **Shrink the plane to 28 × 15** at origin (1,1). Simplest, costs two rows
   and two columns of content.

Do **not** simply draw a rectangular border at the extreme edges — its corners
will disappear under the bezel, which looks like a rendering bug.

Verify with the border test in §7.3 before and after.

---

## 7. Verification

### 7.1 Read the geometry the device actually has

Have `rcS` log this every boot — with no network, `/boot/logs/last-boot.log` on
the FAT partition is the only instrument:

```
display: fb0 up, fb=$(cat /sys/class/graphics/fb0/virtual_size)
display: console grid (rows cols) = $(stty -F /dev/tty1 size)
display: stride=... bpp=...
display: dt geometry: width=... height=... x-offset=... y-offset=...
```

Expected: `240,280` / `17 30` / `stride=960 bpp=32` / offsets `0`.

### 7.2 Prove SPI is transmitting

```sh
S=$(find /sys/devices -type d -name statistics -path '*spi0.0*' | head -1)
grep . $S/messages $S/transfers $S/bytes $S/errors $S/timedout
```

Bytes climbing with zero errors means the Pi is doing its job and any remaining
fault is wiring or the init sequence. This is the single most useful diagnostic
in the whole stack — it cleanly separates software from hardware.

### 7.3 Fill and border tests

> **THE FRAMEBUFFER IS 32 bpp (XRGB8888), stride 960 — NOT RGB565.**
> RGB565 is only what travels over the SPI wire. A full screen is
> `960 × 280 = 268,800` bytes. Sizing a fill as `240*280*2 = 134,400` writes
> **exactly half the rows** and leaves the bottom looking broken. This wasted
> hours in this project. **Always read `stride` and `bits_per_pixel` from
> sysfs; never compute from width and depth.**

A `fbfill`-style helper belongs in the rootfs overlay at `/usr/bin/`, sized
from sysfs, supporting solid fills plus `bars` (white bar on first and last 10
rows) and `ruler` (alternating 10-row bands to count visible rows).

For the rounded-corner check, draw a rounded border at the exact edges with
corner tags, and confirm the curve follows the bezel on all four corners.

### 7.4 Build-time checks (`verify-image.sh`)

Assert: `mipi-dbi-spi` overlay present; `write-only` set; `width=240` and
`height=280`; `y-offset=0`; `panel.bin` present with valid `MIPI DBI` magic;
console font is `VGA8x16`; **no compressed modules** (`*.ko.xz` etc.); no
HyperPixel or DPI leftovers.

---

## 8. Pitfalls — every one of these actually happened here

1. **Fragment deletion ≠ unset.** §5.2. Removing `CONFIG_FB_BCM2708=y` from the
   fragment left it enabled from the base defconfig.
2. **`gpu_mem=16` bricks boot silently.** Needs cutdown firmware blobs Buildroot
   does not ship. Keep ≥ 32.
3. **`CONFIG_MODULE_COMPRESS_XZ=y` is in `bcm2711_defconfig`** and breaks
   everything: the host `depmod` cannot index `.ko.xz`, so the module is absent
   from `modules.dep`; busybox `modprobe` reports "not found" though the file is
   installed; busybox `insmod` cannot decompress it. Buildroot's
   `BR2_PACKAGE_KMOD` is the library only — busybox is the sole modprobe.
4. **`fbcon=map:1` with one framebuffer** maps the console to a non-existent
   `fb1`.
5. **32-bpp framebuffer.** §7.3. Cost hours of "the bottom half is broken".
6. **`y-offset=18` was wrong.** Derived from Waveshare's
   `(320 - height + 1) / 2`, which applies to a different revision. Measured
   value is **0**.
7. **BL on the wrong GPIO.** BCM 18 / pin 12, not BCM 24 / pin 18.
8. **VCC on 5 V.** Must be 3.3 V to match the Pi's logic level.
9. **SPI above 25 MHz.** The level shifter, not the controller, is the limit.
10. **DIN on pin 21 (MISO) instead of 19.** Silent, error-free, invisible.
    Everything enumerates and transmits; nothing displays.
11. **Buildroot does not re-sync local package sources.** `SITE_METHOD = local`
    rsyncs once at extract and stamps it, so a plain `make` will not pick up an
    edited `config.h`. Use `make blackhand-ui-dirclean && make`. The rootfs
    overlay *is* re-copied every build, which makes this confusing: `rcS`
    changes appear while UI changes do not.
12. **`/boot` is 128 MB and fills up.** A crash-looping service wrote a 105 MB
    log and took it to 100%, after which even `sed -i /boot/config.txt` failed.
    Cap per-file log size at boot and periodically.
13. **`blackhand-ui` has no log file.** The service manager points its
    stdout/stderr at `/dev/tty1` so errors print to the screen, not to
    `/boot/logs`. Redirect stderr to a file when debugging it.

---

## 9. Suggested agent split

| Agent | Scope |
|---|---|
| `drivers` | §3 wiring verification, §4 blob, §5.1 `config.txt`, §7.2–7.3 |
| `kernel` | §5.2 fragment, §5.3 built-in vs module, `EXTRA_FIRMWARE` hook |
| `buildroot` | §5.3 `external.mk` hook, §8.11 local-source rebuild, §7.4 |
| `ui` | §6 — `config.h` geometry, popup widths, rounded-corner insets |
| `qa` | §7 end to end, plus regression on audio / BT / modem |

Bring up in this order: boot config → kernel → blob → framebuffer proven with a
fill test → console grid confirmed `17 30` → UI geometry → rounded-corner
inset. Do not start on the UI until `fbfill white` covers the entire panel.

---

## 10. Definition of done

1. Cold boot to the BlackHand UI on the panel, no network attached.
2. `/boot/logs/last-boot.log` shows `fb=240,280`, `console grid = 17 30`,
   `y-offset=0`.
3. A full-screen fill covers **every** pixel, edge to edge.
4. No UI glyph is clipped by the rounded corners on any screen.
5. No screen has content truncated at the right edge.
6. Audio, Bluetooth and modem still pass their existing checks.
7. `verify-image.sh` passes with the §7.4 assertions; no HyperPixel references
   remain in the tree.
