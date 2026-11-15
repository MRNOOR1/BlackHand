# Tier 4 — UI shell

**Purpose:** the notcurses UI: home, settings, PIN lock and keypad input.

**Builds on:** Tier 2 (runs in the `bh` tmux session) and Tier 3 (IPC, storage).
**Needed by:** Tier 5. The phone screens are part of this UI.

## What's here
| Item | Role |
|---|---|
| `notcurses/` | notcurses 3.0.11, no multimedia, no C++ |
| `blackhand-ui/` | the UI (CMake). 30×17 grid, see `config.h` |

## Tests
- Host: `make test TIER=4` covers PIN (PBKDF2 verifier, migration, fail-closed on
  corruption) and notes (atomic writes).
- **Gate (on device):** the UI appears on the panel at boot, every keypad key does
  something, lock → PIN → unlock works, the inactivity lock fires, and settings
  persist across a reboot.

## Work still to do in this tier (not done yet)
The UI still contains features parked above Tier 5. When this tier is worked on,
they get cut so the UI builds without mpg123 and D-Bus:
- MP3 (`mp3_service`, `screen_mp3`), voice memos, Bluetooth and AVRCP, notes,
  themes, GPS screen, `headphone_input`
- GPM in notcurses (only for the touch input, which has no driver)

Then move the phone screens (calls, messages, contacts) to `tier5-phone/ui/`,
built only when Tier 5 is included.

## Known issues
- Screens make synchronous IPC calls while rendering, so a slow daemon freezes the UI.
- The status strip has no Bluetooth, unread or missed-call indicators.
- Brightness has no consumer (the backlight is plain GPIO, not PWM).
- The 4-digit PIN is low entropy and doesn't encrypt `/data`.
- The keypad W/S/A/D/Q/E mapping was fixed in source but never confirmed on hardware.
