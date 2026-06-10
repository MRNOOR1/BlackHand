# Black Hand OS — Phase 2 Code Review
*Reviewed 2026-06-10. Covers every package in `blackhand-external`, the IPC contracts between them, and boot/build wiring.*

---

## Architecture map (as actually implemented)

```
blackhand-init (PID 1)
 ├─ rcS (sync): display, net, SSH, audio card, Bluetooth, [NEW: usb-serial + udevd]
 └─ service manager ← /etc/blackhand/services.conf
     ├─ blackhand-audio    → /run/bh-audio.sock     (ALSA owner: play/record)
     ├─ blackhand-modem    → /run/bh-modem.sock     (SIM7600 AT owner)
     ├─ (blackhand-storage → /run/bh-storage.sock)  ⚠ BUILT BUT NEVER STARTED
     └─ blackhand-ui       → client of all three (notcurses, tty1)

libblackhand-ipc: 4-byte length-prefixed JSON-RPC over Unix sockets (static lib)
```

Persistence is split two ways:

| Data | Owner | Mechanism |
|---|---|---|
| calls, messages, locations, (contacts, settings unused) | blackhand-storage | JSON-RPC → JSON files |
| contacts, notes, alarms, settings, themes, voice memos | UI process itself | direct `fopen()` on /data |

---

## 1. blackhand-ipc (core library) — solid, two design risks

**Correct:** framing handles partial I/O and EINTR; `recv_msg` bounds-checks length; lib deliberately excludes dispatch (each service has its own). Clean.

**Issues**
- **No timeout anywhere.** `ipc_request()` blocks forever if a server accepts but never replies. Every UI call is synchronous on the render thread, so one stuck service freezes the whole phone UI. Worst case: SMS send (modem holds the request up to **60 s**) — the UI is frozen the entire time.
- `recv_msg` returns -1 when a message exceeds the buffer but doesn't drain the socket — the connection is then poisoned. Low impact (one-shot connections) but worth knowing.
- `ipc_framing.c` / `ipc_dispatch.c` in the lib directory duplicate `send_msg`/`recv_msg` and are **not** in the lib Makefile — dead source that causes "multiple definition" if ever linked. Delete or clearly mark.

## 2. blackhand-modem — call path now complete; 5 UI methods unimplemented

**Correct:** URC thread design is sound (single reader, cond-var handoff to `at_cmd`); CLIP/CMT parsing OK; degraded-mode + background re-probe now in place; dispatch contract for calls/SMS matches the UI.

**Missing / incomplete**
- **`get_messages`, `get_calls`, `gps_enable`, `gps_disable`, `get_location` are not implemented in `modem_dispatch.c`.** The UI client (`modem_ipc.c`) sends all five. `gps_*`/`get_location` are *live calls* from `screen_gps.c` → the GPS screen is fully broken ("unknown method"). `get_messages`/`get_calls` currently have no UI callers (dead client code) — implement or delete both sides. SIM7600 GPS needs `AT+CGPS=1`, `AT+CGPSINFO` parsing.
- **`modem_status` runs `AT+CSQ` inline** (up to 2 s) on every poll. UI polls ~1–2×/s from two places (main loop + calls tick). It works, but it serializes behind dial/answer on the single dispatch thread and adds latency at the worst moments. Cache signal in a background ticker instead, and have `modem_status` return the cached value.
- **SMS send blocks dispatch up to 60 s** (`modem_sms_send` cond-wait). Combined with the no-timeout IPC, this freezes the UI. Either make it async (queue + `sms_status` poll) or cap at ~10 s.
- **No USB-disconnect recovery.** After bring-up, a yanked cable leaves `fn` pointing at a dead fd; URC `read_line` gets EIO, thread exits, `modem_present` stays 1. Detect read errors → mark offline → re-enter the probe loop.
- **No registration/SIM state surfaced.** `modem_registered()` exists but nothing calls it after boot; the UI can't show "No SIM" / "Searching..." / operator name. `pending_sms`/`gps_enabled` in `modem_status` are hardcoded 0.
- **Multi-line SMS bodies are truncated** — `handle_cmt` reads exactly one line; an SMS containing `\n` loses everything after the first line.
- **Dead files in tree:** `modem_service.c`, `modem_core.c/.h`, `modem_uart.c/.h`, `ipc_dispatch.c`, `at_parser.c/.h`, `sms.c` (3 lines, empty) were dropped from `SRCS` but still sit in src/. Delete them — they're a second, stale implementation of the same daemon and will confuse every future edit.
- Minor: `strncpy(buffer, response_buffer, BUFFERSIZE)` in `at_cmd` lacks guaranteed NUL on exact-fit; `modem_signal()` returns uninitialized `strength` if the response has no comma.

## 3. blackhand-audio — contract OK; two owners of the mic

**Correct:** dispatch table (ping/play/pause/stop/volume/status/record_start/record_stop) is a superset of what the UI calls. ALSA capture thread handles EPIPE.

**Issues**
- **Voice memos bypass the audio service entirely** — `voice_memo_service.c` forks `arecord` directly while blackhand-audio also owns capture (`record_start/record_stop` are implemented but unused). Two processes contending for the same ALSA capture device; recording will fail whenever the audio service holds it (and vice versa). Pick one owner — either route memos through `record_start` or delete capture from the service.
- MP3 playback path: confirm `play` handles .mp3 (mpg123 is in the image) vs WAV-only ALSA writes — check `handle_play` if MP3 screen misbehaves.

## 4. blackhand-storage — fully implemented, **never started**

**Correct:** all 16 methods the UI's `storage_ipc.c` sends exist server-side, names match exactly. Schema/persist logic looks complete.

**Issues**
- **Not listed in `services.conf` → the daemon never runs.** Every `storage_ipc_*` call fails to connect: call log and SMS inbox are silently empty and nothing persists. This is the single biggest "UI logic seems broken" cause after the modem itself. Fix = add a `[blackhand-storage]` block between audio and modem/ui (the services.conf comment even documents that the UI needs it — the block was just never written).
- **Contacts split-brain:** storage implements `contacts.*` and the UI has `storage_ipc_contacts_*` wrappers, but `contacts_service.c` ignores them and writes one file per contact under /data/contacts. Same for settings (`settings.get/set` unused; UI has its own settings file). Decide: either migrate contacts/settings to the storage service or delete the dead server methods + client wrappers.
- No file locking / atomic writes (`save_json` truncates in place) — a crash mid-write corrupts the JSON. Write-to-temp + rename is a 5-line fix.

## 5. blackhand-ui — structurally good; specific gaps

**Correct:** screen enum ↔ draw/input switch fully populated (12 screens); softkey normalization with PUA codepoints is sound; dial-mode/compose-mode gating of numpad remapping works; the calls flow after this week's fixes covers outgoing, incoming, missed, remote hangup.

**Missing / incomplete**
- **Messages: no read view.** Enter on an inbox item jumps straight to compose-reply; long bodies are truncated in the list and can never be read in full. Need a detail view (and call `messages.mark_read` — currently *nothing* in the UI ever marks messages read; `storage_ipc` has the wrapper, unused).
- **"Message sent!" feedback never shows** — `s_send_feedback` is set, then state immediately returns to INBOX, and the feedback only draws in COMPOSE state. Dead code as written.
- **SMS send freezes the UI** (see §1/§2 — synchronous 60 s worst case).
- **GPS screen depends on the five unimplemented modem methods** — currently 100 % broken.
- **No ringtone / call audio routing.** Incoming-call UI appears, but nothing plays a ringtone via the audio service, and there's no in-call audio path wiring (SIM7600 USB-audio or analog out). Calls will connect with no sound UX.
- **Missed-call indicator:** missed calls now land in the log, but the home screen shows no badge/notification; same for unread SMS.
- Incoming SMS drain runs every ~5 s only when `modem_ipc_is_online()`; messages received are appended via `comm_service_message_add_full` → requires storage service (see §4) or they vanish on reboot.
- Softkey binds `q`/`e` are consumed by `normalize_control_key()` *before* text-entry gating — on a dev USB keyboard you can't type the letters q/e into an SMS body. Irrelevant for the final keypad, annoying for testing.
- `lookup_name_by_phone` does exact string match — "+1416..." vs "416..." won't match; consider suffix matching (last 7–10 digits).

## 6. blackhand-init / service manager — sound; one config gap

**Correct:** VFS mounts, console binding, sync rcS then services, SIGCHLD reap-and-restart loop with async-signal-safe writes. Nice.

**Issues**
- services.conf missing storage (§4).
- Restart policy has **no backoff** — a service that dies instantly (e.g., bad binary) is restarted in a tight fork loop, starving the system. Add a per-service restart counter + delay.
- No `stdout/stderr → log file` for services (only tty); service logs are lost. Consider redirecting each child to /data/log/<name>.log.

## 7. blackhand-stt — placeholder only

`stt_service.c` is 4 lines (a comment). Not in defconfig, not in services.conf, no socket, no UI hook. The README promises offline voice assistant — this is the largest unstarted feature. Decide scope (Vosk small model is realistic on Pi 4) or cut it from README.

## 8. Build / boot wiring

- `BR2_PACKAGE_EUDEV=y` was added, but check it actually lands: with `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS` set, Buildroot may not select eudev's init integration. If `udevd` is absent from the image, switch to `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y`. (rcS now degrades gracefully either way thanks to the modprobe fallback.)
- `BR2_PACKAGE_BLACKHAND_STT` absent from defconfig (consistent with §7 — fine until STT is real).
- Repo hygiene: `BCM4345C0.hcd`, `BCM4345C0.raspberrypi,4-model-b.hcd`, `test.mp3` (1 MB) sit at repo root — duplicates of the overlay firmware; move/remove. `.DS_Store` is committed-adjacent; add to .gitignore.

---

## Prioritized missing/incomplete list

**P0 — blocks "phone works" (calls + SMS end-to-end)**
1. Add `[blackhand-storage]` to services.conf (call log / inbox / persistence dead without it).
2. ~~USB serial driver + udev at boot~~ ✅ fixed this session.
3. ~~Modem degraded-mode retry, AT+CHUP, missed/incoming UI states~~ ✅ fixed this session.
4. Make SMS send non-blocking (or cap timeout) — UI freeze up to 60 s.
5. Ringtone + in-call audio routing (calls are silent otherwise).

**P1 — broken features visible to user**
6. Implement modem `gps_enable/gps_disable/get_location` (AT+CGPS) or hide the GPS screen.
7. Message read view + mark_read + unread badge.
8. Cache AT+CSQ; stop running it inline per status poll.
9. USB-disconnect → offline → re-probe in modem service.
10. Restart backoff in service manager.

**P2 — correctness/robustness debt**
11. IPC client timeout (poll/SO_RCVTIMEO) so no service can freeze the UI.
12. Atomic writes in storage service; multi-line SMS bodies; CLIP/name suffix matching.
13. Resolve contacts/settings split-brain (storage service vs UI-local files).
14. Voice memo vs audio-service capture ownership.
15. Delete dead modem sources + lib's unused framing/dispatch duplicates; repo-root firmware/mp3 cleanup.

**P3 — unstarted scope**
16. blackhand-stt (entire voice-assistant feature).
17. Missed-call/SMS home-screen notifications, call-state header icons (signal bars currently unused by header?).
