# Tier 2 — Display & session

**Purpose:** light the SPI panel and run the tmux session `bh` that hosts the UI on
both the panel (tty1) and SSH.

**Builds on:** Tier 1 (kernel, PID 1 keeps tty1 attached, rcS runs `S30`/`S90`).
**Needed by:** Tier 4. The UI runs inside the `bh` session.

## What's here
| Item | Role |
|---|---|
| `blackhand-panel/` | `panel.txt` → `panel.bin`, generated at kernel build and linked in (`CONFIG_EXTRA_FIRMWARE`) |
| `linux.fragment` | SPI, DRM `panel-mipi-dbi`, fbdev console, gpio backlight, 8x16 font |
| `config.txt` | `mipi-dbi-spi` overlay: 240×280, write-only, 8 MHz |
| `S30-display` | logs fb geometry, paints the strip below the text grid, disables blanking |
| `S90-session` | `bh-session --detached` creates the `bh` session |
| `bh-session` | the one definition of the session (30×17). Below Tier 4 it shows the build stamp |
| `fbfill` | fills or patterns the framebuffer, for checking the panel by eye |

## Panel wiring (dev, header-wired to SPI0)
| Pad | BCM | Pi pin |
|---|---|---|
| DIN | 10 (MOSI) | 19 |
| CLK | 11 (SCLK) | 23 |
| CS | 8 (CE0) | 24 |
| DC | 25 | 22 |
| RST | 27 | 13 |
| BL | 18 | 12 |

MISO isn't connected: the panel is write-only, so there's no read-back or ID check.
The SPI speed is capped by the level shifter (~25 MHz). It's set to 8 MHz.

**Carrier FPC2 wiring (not active):** SPI4 with CS 4, MOSI 6, SCLK 7, RST 20, DC 21,
BL 12 (active high), 240×304. It needs `dtoverlay=spi0-1cs` and `spi4-1cs`,
because GPIO7 is SPI0 CE1. **GPIO27 is the touch INT on the carrier**, so the dev
RST pin must change when switching. The carrier backlight (10 Ω off 3V3) will be dim.

For the NV3030B fallback panel, set `BLACKHAND_PANEL_SEQUENCE` to
`panel-nv3030b.txt` (see `blackhand-panel.mk`).

## Tests
- Device: `make device-test TIER=2 HOST=<ip>` checks that fb0 is bound and 240 px
  wide, the `bh` session exists, and tty1 is attached.
- **Gate (by eye):** `fbfill cycle` fills the whole panel with each colour, and the
  build stamp shows on the panel. `bh-session` over SSH shows the same screen.

## Known issues
- The fbdev exposes XRGB8888 (32 bpp, stride 960) even though RGB565 goes over the
  wire. Never hardcode framebuffer sizes.
- The panel has rounded corners (radius ≈ 28 px). The UI keeps the outer rows as
  background only.
- Nothing restarts the tmux server if it dies. The tty1 respawn recreates it.
- Touch (CST816D on I2C1) has no driver. That's parked above Tier 5.
