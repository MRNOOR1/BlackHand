# BlackHand remediation status — 2026-09-09

This file tracks the supplied 54-item problem list. “Implemented” means a code
change exists; it does **not** mean a Buildroot image was built or a device was
flashed. Hardware is offline. Partial and open items remain work to do.

## Implemented changes and remaining work

| # | Status | Change / outstanding work |
|---|---|---|
| 1 | Implemented | Storage owns `history.clear`; White Wipe checks its result, clears caches/drafts, and preserves the lock on deletion failure. Logical deletion, not secure flash erasure. |
| 2 | Partial | Modem validates RPC results and reports a latched storage error. No normal SMS notification on failure. Durable retry/SIM retention and deduplication remain; a failed write can still lose a message. |
| 3 | Implemented | WAV helper counts only complete written frames, stops on write failure, finalises the prefix, and exposes recording errors. Finalisation can itself fail on a damaged/full filesystem. |
| 4 | Implemented | Atomic note writes check flush/fsync/close; cache changes only after successful publication. UI retains edits on failure. |
| 5 | Implemented | Back saves; idle and shutdown flush notes. Oversize notes cannot be silently truncated by the editor. |
| 6 | Partial | Physically allocated 256 KiB emergency reserve is released on ENOSPC during JSON replacement and replenished after requests when space allows. Finite protection; directory creation, media quotas and user storage management remain. |
| 7 | Implemented | Random memo suffix plus exclusive creation prevents overwriting existing recordings. |
| 8 | Implemented | Compose remains visible with send feedback. |
| 9 | Implemented | Shared softkey renderer displays supplied labels. |
| 10 | Implemented | Persisted talk duration uses CLOCK_MONOTONIC; wall time is used only as record timestamp. |
| 11 | Implemented | An unavailable modem produces CALL STATE UNKNOWN instead of continuing a convincing live timer. Hardware recovery remains unverified. |
| 12 | Partial | One durable draft survives leaving/restarting and failed sends. Corrupt drafts are preserved and reported. Multiple drafts and ambiguous-send recovery are not implemented. |
| 13 | Partial | Cancelled and failed outcomes exist and are labelled distinctly. Ordering between hangup acknowledgements and unsolicited end events still needs a call-state regression test. |
| 14 | Partial | AT dial failures are recorded and shown. Invalid requests and dispatch-time hardware absence do not create call records. |
| 15 | Implemented | Duplicate numbers are rejected; updates carry the original number and replace name/number in one atomic storage operation. |
| 16 | Implemented | Normal incoming notification is queued only after acknowledged persistence. |
| 17 | Implemented | Conversation CALL action opens outgoing call flow with its sender number. |
| 18 | Implemented | MP3 decoding/output failures produce MP3_ERROR and stop automatic queue advance. Corrupt-media regression covers this. |
| 19 | Implemented | Shared atomic call policy guards MP3/memo playback and AVRCP fallback resume. Device button timing and outgoing-dial timing need target testing. |
| 20 | Partial | Power-off checks the adapter result, stops discovery and requests audio fallback. Power-off is refused while connecting. Route failures and adapter disappearance need fuller recovery/UI reporting. |
| 21 | Implemented | Forget/disconnect only reroute the currently selected audio device. Bluetooth hardware verification remains. |
| 22 | Open | Persist auto-connect preference; distinguish explicit disconnect from background reconnect. Single-worker ownership, cache snapshots and cancellation are still needed. |
| 23 | Hardware information required | Call PCM/USB audio routing is absent. Need exact SIM7600 variant, exposed interfaces, wiring and duplex testing. Current UI does not claim mute works. |
| 24 | Open | Separate call/media/ringtone/alarm volume paths require audio policy and actual playback producers. Only existing global volume is supported. |
| 25 | Open | MP3 decoding/output still lives in UI. Move playback engine to audio daemon; keep library browsing/selection in UI. |
| 26 | Implemented | UI requests `route`; audio daemon validates the Bluetooth sink and atomically owns asound.conf updates. |
| 27 | Open | Rendering still invokes synchronous service calls. Move reads/commands to workers with immutable results, operation IDs and explicit pending/error states; never report a queued mutation as success. |
| 28 | Implemented, measurement pending | Monotonic polling deadlines and a minimum idle interval replace unbounded timeout spin. CPU/context-switch measurements must be repeated on the target. |
| 29 | Implemented | Confirmed Power off action flushes drafts/notes, finalises recording and signals BlackHand PID 1 directly. Not executed on this host. |
| 30 | Partial | UI SIGTERM/SIGINT cleanup and audio shutdown finalisation added; init grants five seconds. Blocking RPC, PCM I/O and detached Bluetooth workers can still exceed shutdown grace. |
| 31 | Implemented | Removed duplicate test-ui sources; its CMake entry delegates to the production UI. It requires production Linux dependencies, not a separate macOS UI. |
| 32 | Implemented | Removed dead modem_service.c after checking live Makefile and references. |
| 33 | Implemented, hardware pending | Live gps_enable/gps_disable/get_location handlers and isolated validated coordinate parser added. No-fix and AT failure are tested; receiver behaviour is unverified. |
| 34 | Implemented | All live daemons accept sockets through a helper with two-second I/O timeouts. A continuously active client can still monopolise a serial server; absolute request budgets remain desirable. |
| 35 | Implemented | Partial/oversize framing failures shut down the stream; callers must close and reconnect. Broken-stream regression passes. |
| 36 | Implemented | Salted PBKDF2-HMAC-SHA256 verifier, checked randomness, constant-time comparison, atomic plaintext migration and fail-closed corrupt-file handling. Four-digit PIN is still low entropy and does not encrypt storage. |
| 37 | Implemented | Global lock redirects to Home, with live calls allowed and return to locked Home afterwards. |
| 38 | Implemented | Persisted inactivity timeout now drives automatic locking. |
| 39 | Partial | Wipe scope is shown and music/Bluetooth retention is explicit. Separate preferences/connectivity/factory reset modes are absent; storage-owned country settings are not reset. |
| 40 | Open | Process exit supervision exists; liveness detection/watchdog for a wedged process or kernel does not. |
| 41 | Open | Shared status strip still needs Bluetooth, unread/missed, sound and background-work indicators using cached data. |
| 42 | Implemented | Removed unused status renderer and its private helpers; hardware comments identify bh_status_strip. |
| 43 | Implemented | Incoming call records the interrupted screen and resumes only media paused by that call. No blanket stop on viewing call history. |
| 44 | Implemented | Navigation previews themes; Cancel restores persisted theme; lock/call interruption cancels preview. Settings write failure is reported globally. |
| 45 | Partial | Timeout has a consumer. Brightness and auxiliary input still lack completed controls/hardware integration. |
| 46 | Hardware information required | Touch driver/DT integration remains. Need exact display/controller and IRQ/reset wiring. |
| 47 | Implemented | Shared atomic file helper fsyncs file and parent directory. Settings persistence errors are visible; most existing void setters retain live values after failure. |
| 48 | Implemented | Four remaining runtime log paths use volatile /var/log/blackhand instead of boot FAT. Logs do not survive reboot. |
| 49 | Implemented | Pure restart_policy.h is shared by init, manager and host selftest; no inclusion of .c files or static-name rename trick. |
| 50 | Implemented | PIN source declares POSIX feature level and compiles under C11. |
| 51 | Implemented | Old review marked historical; current README and this status file identify shipping notcurses UI, JSON persistence and remaining work. Older design documents remain historical context. |
| 52 | Target build required | UI poweroff bypasses BusyBox telinit by signalling custom PID 1. Still inspect generated BusyBox config and test shell poweroff on next image. |
| 53 | Implemented, deployment pending | Removed known root password in favour of an unrecoverable random-password hash; Dropbear key-only authentication restored. Ensure a provisioned SSH key before deploying. |
| 54 | Implemented, deployment pending | Unauthenticated tty2 root debug shell disabled in defconfig. Existing flashed images are unaffected. |

The separately listed absent features (alarms, activity, DND, backup/import,
updates, battery/airplane, storage UI, SIM PIN, richer contacts, receipts,
playlists and emergency calling) remain product work. This patch does not
pretend they exist.

## Module contracts

- Storage daemon is the authority for contact identity and communication
  history. `contacts.save` without `original_number` creates; with it updates
  exactly that source contact. Destination collisions return an RPC error.
- `history.clear` is an idempotent logical deletion of communication history,
  not an in-memory counter reset and not a filesystem format.
- UI `file_store` is a small filesystem transaction primitive reused by notes,
  settings, PIN and SMS draft persistence. Parent-sync failure after rename
  means publication happened but durability is uncertain; callers show error.
- `wav_file` is independent of ALSA; recording owns one exclusive file and
  reports failure instead of pretending capture continues.
- `audio_policy` supplies call interruption state to existing UI playback
  clients. It is an interim boundary until audio policy is daemon-owned.
- `restart_policy` contains only pure supervision decisions, so its tests do
  not depend on Linux mount/syscall implementations.
- No new background daemon or event framework was introduced.

OpenSSL Crypto is used instead of writing cryptographic primitives locally.
The new Buildroot dependency is explicit. APIs were checked against official
[PBKDF2 documentation](https://docs.openssl.org/3.0/man3/PKCS5_PBKDF2_HMAC/)
and [RAND_bytes documentation](https://docs.openssl.org/3.0/man3/RAND_bytes/).
GPS parsing follows the manufacturer's
[SIM7500/SIM7600 AT manual](https://simcom.ee/documents/SIM7600C/SIM7500_SIM7600%20Series_AT%20Command%20Manual_V1.01.pdf).

## Verification

Run from the repository root (tests use temporary data; never the device /data):

```sh
make -C Phase-2/tests check check-media
make -C Phase-2/blackhand-external/package/blackhand-init/src selftest
./Phase-2/blackhand-external/package/blackhand-init/src/selftest
```

- Host regression suite: original 47 checks plus notes, framing failure,
  storage wipe/contact collision, modem persistence acknowledgement, WAV
  short write, draft restart/corruption, PIN migration/failure, GPS parser,
  and corrupt MP3/call playback policy executables pass.
- Same test executables passed with address and undefined-behaviour sanitizers.
- Init restart-policy selftest passes.
- Production modem sources link on macOS with platform feature macros.
- UI sources except Linux input and D-Bus modules pass C11 syntax checks with
  installed notcurses 3.0.17, mpg123 and OpenSSL headers. Existing unused-helper
  warnings remain. This is not a target link or visual verification.
- Audio source syntax checks pass using the existing ALSA test shim. Actual
  ALSA linkage, duplex audio, Bluetooth and shutdown timing are unverified.
- Full Buildroot build, pinned target-header build, flash, call tests, touch,
  power-cut recovery and idle CPU measurement have not been performed.

## Next implementation increments

1. Regression-test and fix modem call-event ordering; durable incoming SMS
   retention/retry with idempotent storage IDs.
2. Move MP3 playback and call/media arbitration to audio daemon.
3. Move synchronous render-thread IPC to explicit asynchronous operations;
   centralise Bluetooth worker ownership and snapshots.
4. Complete connectivity preferences, resets, status, supported settings and
   watchdog health checks; then hardware integration and target verification.
