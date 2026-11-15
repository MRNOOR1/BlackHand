# Black Hand OS

A small phone OS for the CM4 on the BlackHand carrier: Buildroot, its own PID 1,
a few single-purpose daemons, and a notcurses UI on a 240×280 SPI panel.

## Tiers
The system is a pyramid. Each tier depends only on the ones below it, and is
built and tested before the next one is started.

| Tier | Folder | What it gives you |
|---|---|---|
| 0 | `tier0-build/` | reproducible image build (Docker, Buildroot 2024.02.13) |
| 1 | `tier1-boot/` | kernel, PID 1, service manager, Wi-Fi, SSH |
| 2 | `tier2-display/` | SPI panel, tmux session `bh` on tty1 and SSH |
| 3 | `tier3-plumbing/` | IPC library, storage daemon |
| 4 | `tier4-ui/` | notcurses UI: home, settings, PIN lock |
| 5 | `tier5-phone/` | modem, calls, SMS, contacts, volume |

Parked until Tier 5 passes: media (MP3, voice memos), Bluetooth, and extras
(notes, GPS, themes, LoRa, speech-to-text). They are on branch
`park/full-features` and tag `pre-rebirth`, and each will come back as its own tier.

Each tier folder has a `README.md` (purpose, links, known issues) and `tests/`.

## Use
```
make image TIER=1                  # -> output/tier1-sdcard.img (tiers 0..1)
make test TIER=5                   # host tests for tiers 0..5
make device-test TIER=1 HOST=<ip>  # on-device checks over SSH
```
Needs Docker. Put Wi-Fi credentials in `tier1-boot/wifi.env` first (see
`tier1-boot/README.md`).

**Flash:** write `output/tierN-sdcard.img` to the SD card (Raspberry Pi Imager, or
`dd`). Then copy your SSH public key to the card's boot partition as
`authorized_keys`.
