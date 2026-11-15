# Tier 5 — Phone

**Purpose:** calls, SMS and contacts over the SIM7600 modem, plus the volume control
calls need.

**Builds on:** Tier 3 (modem writes history to storage) and Tier 4 (the phone
screens are in the UI).
**Needed by:** nothing yet. Media, Bluetooth and extras come back above this.

## What's here
| Item | Role |
|---|---|
| `blackhand-modem/` | daemon on `/run/bh-modem.sock`: AT commands, URCs, calls, SMS, GPS |
| `blackhand-audio/` | daemon on `/run/bh-audio.sock`: ALSA volume and status |
| `linux.fragment` | USB serial (option driver), ALSA bcm2835 + USB audio |
| `S60-audio` | picks the USB card or onboard card 0, writes `/etc/asound.conf`, sets mixer levels with `amixer` |
| `services.conf` | starts audio, then modem |

## Hardware (blocker)
On the current carrier, **the modem is unreachable by both routes**:
- **UART2:** TX/RX are wired TX-to-TX. Don't enable `dtoverlay=uart2`, because two
  outputs would fight on GPIO0.
- **USB:** the CM4's USB never reaches the modem.

Until the board is reworked, this tier can only be checked on the host, or with
the modem on a USB adapter wired straight to the CM4.

## Tests
- Host: `make test TIER=5` covers modem → storage history writes, the SMS draft
  round trip, GPS parsing and the WAV writer.
- **Gate (on device):** a real call out and in, SMS both ways, history persisting,
  and a ringtone on an incoming call.

## Work still to do in this tier (not done yet)
- Cut the audio daemon down to volume + status: playback, recording, WAV and
  `bt_route` belong to parked tiers. Then `asound.conf` has one writer.
- Remove GPS from the modem (parked) and the `/dev/blackhand-modem-at` fallback in
  `serial_utility.c` (lines 28 and 56). That symlink came from udev, which isn't
  used.
- Add a ringtone. Today an incoming call only changes the screen.

## Known issues
- Dispatch is single-threaded and blocking. A slow AT command can stall modem IPC
  (capped at about 60 s).
- Call audio routing (modem PCM/USB audio) doesn't exist. Mute (`A`) is UI-only.
- No per-message SMS delete in the backend.
- A failed history write can still lose an SMS (no durable retry).
- Incoming calls don't duck other audio, and `/tmp/bh-audio-card` couples rcS to
  the audio daemon.
