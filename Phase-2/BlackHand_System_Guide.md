# BlackHand OS — Complete System Guide

---

## Part 1: The Full Boot Chain

This is the exact sequence of events from the moment you plug in power to the moment the UI appears on screen. Every file in the chain is listed.

---

### Step 1 — GPU Bootloader (hardware, no code you write)

The Raspberry Pi 4 does not start the CPU first. The GPU starts first and runs a bootloader baked into the board. It reads the SD card's first partition (FAT32, the boot partition) and looks for two files:

- `config.txt` — hardware configuration (display mode, overlays, memory split)
- `cmdline.txt` — the kernel's command line arguments

**File:** `board/blackhand/rpi-firmware/cmdline.txt`
```
console=tty3 loglevel=1 quiet root=/dev/mmcblk0p2 rootfstype=ext4 rw
fsck.repair=yes rootwait vt.global_cursor_default=0 init=/sbin/blackhand-init
```

What each argument does:

- `console=tty3` — kernel messages go to tty3 (the third virtual terminal). The display shows tty1. This separates kernel noise from what you see on screen.
- `loglevel=1 quiet` — suppress most kernel boot messages.
- `root=/dev/mmcblk0p2` — the root filesystem is on the second SD card partition.
- `rootfstype=ext4` — it is an ext4 filesystem.
- `fsck.repair=yes rootwait` — wait for the SD card to be ready, repair if needed.
- `vt.global_cursor_default=0` — hide the blinking cursor on all virtual terminals.
- `init=/sbin/blackhand-init` — **the critical line**. Normally Linux runs `/sbin/init` (busybox init) as PID 1. This overrides it with our custom binary.

---

### Step 2 — Kernel Boots

The GPU hands control to the CPU. The kernel mounts the root filesystem, then executes whatever binary is in `init=`. In our case: `/sbin/blackhand-init`.

From this point everything is your code.

---

### Step 3 — blackhand-init runs as PID 1

**File:** `package/blackhand-init/src/init.c`

PID 1 is the most important process on the system. If it exits, the kernel panics immediately. It has one job: get everything else started, then wait forever.

The functions run in this exact order:

**`kmount_vfs()`**
Mounts the five virtual filesystems. Without these, nothing works.

- `/proc` — process information. Commands like `ps`, `top`, `cat /proc/cpuinfo` all read from here.
- `/sys` — hardware device information. The display, audio, GPIO are all visible here.
- `/dev` — device nodes. Without this there is no `/dev/tty1`, no `/dev/console`, no audio hardware.
- `/run` — temporary storage for runtime files. **This is where Unix socket files live** (`/run/bh-audio.sock` etc). It is mounted as tmpfs (RAM disk) so sockets are cleaned up on reboot.
- `/tmp` — temporary files. Also tmpfs.

After mounting `/dev`, it immediately calls `ksetup_console()`.

**`ksetup_console()`**
Opens `/dev/console` and binds file descriptors 0, 1, 2 (stdin, stdout, stderr) to it. Because `console=tty3` is in cmdline.txt, `/dev/console` points to tty3. This means all subsequent `write(STDERR_FILENO, ...)` calls go to tty3 — visible only on a serial console, not on the display.

Right after this, it opens `/dev/tty1` and stores it in `log_fd`.

**`log_fd = open("/dev/tty1", O_WRONLY | O_NOCTTY)`**
This is the on-screen logging fix. The HyperPixel display renders tty1. By opening tty1 into a separate file descriptor, `klog()` can write to both tty3 (serial) and tty1 (display) simultaneously. `O_NOCTTY` means we do not claim tty1 as our controlling terminal here — that happens later in the shell.

**`klog()`**
The logging helper. It writes to `STDERR_FILENO` (tty3, for serial debugging) AND to `log_fd` (tty1, the display). It uses a manual pointer walk to compute string length instead of `strlen()` because this function can be called from signal handlers and `strlen()` is not async-signal-safe. Every boot message you see on the screen comes from `klog()`.

**`krun_sysinit()`**
Forks a child and runs `/etc/init.d/rcS` (the startup script). The parent blocks with `waitpid()` until rcS finishes. This is deliberate — nothing starts until rcS completes. Crucially, SIGCHLD is NOT registered yet, so there is no race between our manual `waitpid()` and a signal handler's `waitpid()`.

**`ksetup_signals()`**
Registers three signal handlers:
- `SIGCHLD` → `ksigchld_handler` — reaps zombie child processes. Called every time any child dies.
- `SIGTERM` / `SIGUSR1` / `SIGUSR2` → `ksigterm_handler` — requests shutdown.
- `SIGINT` → `ksigint_handler` — Ctrl-Alt-Del reboot.

Registered AFTER rcS because rcS is run with a blocking `waitpid()`. If SIGCHLD was registered first, the kernel could fire the handler which also calls `waitpid(-1)`, stealing the exit status before our blocking wait returns.

**`kspawn_shell()`**
Forks a child that opens `/dev/tty1` as its controlling terminal (via `setsid()` + `TIOCSCTTY`) and execs `/bin/sh`. This is the debug shell you see on the display. `setsid()` creates a new session. `TIOCSCTTY` claims tty1 as the controlling terminal for that session so Ctrl-C works. The shell is started with `argv[0] = "-sh"` which tells the shell to behave as a login shell.

**`kspawn_services()`**
Calls `kspawn()` which forks and execs `/usr/bin/blackhand-svc-mgr`. The service manager becomes a child of PID 1.

**`for(;;) pause()`**
The main loop. `pause()` puts PID 1 to sleep until a signal arrives. When SIGCHLD fires (a child died), `ksigchld_handler` wakes up and reaps the zombie. When SIGTERM fires, `kdo_shutdown()` kills everything and reboots.

---

### Step 4 — rcS runs (synchronously, before services)

**File:** `board/blackhand/overlay/etc/init.d/rcS`

This script runs as a child of init and init waits for it to finish. It does:

1. Mounts `/dev/pts` (devpts) — required for PTY allocation. SSH uses PTYs. Without this, SSH connects but immediately prints "PTY allocation request failed".
2. Mounts `/dev/shm` — shared memory tmpfs.
3. Brings up `eth0` and runs `udhcpc` to get an IP via DHCP.
4. Starts `dropbear` SSH server.
5. Runs `hyperpixel4-init` if present (wakes the display controller).
6. Mounts the boot partition at `/boot`.
7. Loads `snd_bcm2835` — the kernel module for the Pi's built-in audio hardware.

---

### Step 5 — Service Manager starts

**File:** `package/blackhand-init/src/service_manager.c`

The service manager is a normal process (not PID 1). It reads `/etc/blackhand/services.conf` which lists every service with its binary path and restart policy. Then:

1. Registers its own SIGCHLD handler before spawning anything.
2. Forks and execs each service in config file order.
3. Stores each child's PID in a `Service` struct.
4. Enters `for(;;) pause()`.

When a service crashes, SIGCHLD fires, `sigchld_handler` calls `waitpid()` to find the dead PID, looks it up in the `services[]` array via `find_by_pid()`, and calls `start_service()` again to restart it. This is why services are "always" running — the service manager is a watchdog.

**`/etc/blackhand/services.conf`** defines the startup order:
```
[blackhand-audio]   → /usr/bin/blackhand-audio   restart=always
[blackhand-storage] → /usr/bin/blackhand-storage  restart=always
[blackhand-modem]   → /usr/bin/blackhand-modem    restart=always
[blackhand-ui]      → /usr/bin/blackhand-ui        restart=always
```

Order matters. The UI is started last because it connects to the other three sockets. If it started first, there would be no sockets to connect to.

The service manager also handles `blackhand-ui` specially: it opens `/dev/tty1` and redirects the UI's stdin/stdout/stderr to it before exec. This is how the UI gets to draw on the display.

---

### Step 6 — Services start

The process tree at runtime:

```
init (PID 1)
  └── blackhand-svc-mgr
        ├── blackhand-audio    → creates /run/bh-audio.sock
        ├── blackhand-storage  → creates /run/bh-storage.sock
        ├── blackhand-modem    → creates /run/bh-modem.sock
        └── blackhand-ui       → connects to all three as a client
```

---

## Part 2: The Build System

### What is Buildroot?

Buildroot is a tool that downloads, cross-compiles, and packages everything needed to run Linux on embedded hardware. It produces a single SD card image (`sdcard.img`) that contains the bootloader files, the kernel, and the entire root filesystem.

Cross-compiling means compiling ARM binaries on your x86 Mac inside a Docker container. Your Mac runs the compiler. The output runs on the Pi.

### What is BR2_EXTERNAL?

`BR2_EXTERNAL` is Buildroot's plugin system. Instead of modifying Buildroot's own source, you create a separate directory tree (here: `blackhand-external/`) and tell Buildroot about it with `BR2_EXTERNAL=/path/to/blackhand-external`. Buildroot then includes your packages, configs, and board files.

The `external.mk` file at the root of the external tree registers everything with Buildroot.

### The defconfig

**File:** `configs/blackhand_pi4_defconfig`

This is the master configuration for the entire build. It controls:
- Target architecture (`aarch64`, Cortex-A72 — the Pi 4's CPU)
- Which packages to include (`BR2_PACKAGE_BLACKHAND_AUDIO=y`)
- Kernel source and config
- Root password
- Filesystem size
- Which scripts to run after the build

To apply it: `make blackhand_pi4_defconfig`. This generates the `.config` file that `make` actually reads.

### How a Package Works (.mk files)

Every package in Buildroot has a `.mk` file that answers four questions:

1. **Where is the source?** (`SITE` and `SITE_METHOD=local`)
2. **How do you build it?** (`BUILD_CMDS`)
3. **How do you install it onto the target filesystem?** (`INSTALL_TARGET_CMDS`)
4. **Does it need to install headers/libraries for other packages to compile against?** (`INSTALL_STAGING_CMDS`)

Example for blackhand-audio:
```makefile
BLACKHAND_AUDIO_SITE     = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-audio/src
BLACKHAND_AUDIO_SITE_METHOD = local

define BLACKHAND_AUDIO_BUILD_CMDS
    $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define BLACKHAND_AUDIO_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/blackhand-audio $(TARGET_DIR)/usr/bin/blackhand-audio
endef
```

`$(TARGET_CONFIGURE_OPTS)` passes the cross-compiler (`CC=aarch64-linux-gnu-gcc`), include paths, and library paths to your Makefile. This is why your Makefiles use `CC ?=` — Buildroot overrides CC with the cross-compiler via this variable.

### STAGING_DIR vs TARGET_DIR

Two different directories, two different purposes:

- **STAGING_DIR** — a sysroot for compilation. Contains headers (`.h` files) and libraries (`.a`, `.so`) that other packages need to compile against. Packages install here when `BR2_PACKAGE_X_INSTALL_STAGING=YES`.
- **TARGET_DIR** — the actual root filesystem that ends up on the SD card. Contains only what the device needs at runtime.

`blackhand-ipc` installs its headers into STAGING_DIR so that `blackhand-audio`, `blackhand-storage`, `blackhand-modem`, and `blackhand-ui` can `#include "ipc.h"` during compilation.

### The Overlay

**Directory:** `board/blackhand/overlay/`

Every file here is copied directly into TARGET_DIR at the end of the build, overwriting any existing files. This is how we deploy:
- `/etc/init.d/rcS` — startup script
- `/etc/blackhand/services.conf` — service config
- `/etc/network/interfaces` — network config
- `/etc/hostname`, `/etc/resolv.conf`

No compilation needed — these files are deployed as-is.

### post-build.sh

**File:** `board/blackhand/post-build.sh`

Runs after all packages are built and installed but before the filesystem image is created. Used for things that need shell scripting rather than Makefile rules:
- Creates `/data/notes`, `/data/music`, `/data/alarms` directories.
- Compiles the HyperPixel touch device tree overlay.
- Generates the dropbear SSH host key using `HOST_DIR/bin/dropbearkey` (the host-side build tool) so SSH works from first boot without any manual setup.

---

## Part 3: The IPC Architecture

IPC stands for Inter-Process Communication. This is how the UI talks to the audio service, how it queries storage, how it sends commands to the modem. Every service communicates using the same protocol.

### Unix Domain Sockets

Each service creates a Unix domain socket file at startup. A Unix socket is like a TCP socket but only works on the local machine. Instead of an IP address and port, it has a file path.

Audio creates `/run/bh-audio.sock`. Anything that wants to send a command to audio opens a connection to that file.

### The Wire Protocol

Every message has two parts:

```
[4 bytes: message length as big-endian uint32][JSON payload bytes]
```

The length prefix tells the receiver how many bytes to read. Without it, the receiver would not know where one message ends and the next begins.

**File:** `package/blackhand-ipc/src/ipc_framing.c`
- `send_msg(fd, msg)` — writes the 4-byte length then the message.
- `recv_msg(fd, buffer, size)` — reads the 4-byte length, then reads exactly that many bytes.

Big-endian (`htonl`/`ntohl`) is used because network byte order is always big-endian. This ensures a message written on any machine is read correctly on any other machine.

### The JSON Format (JSON-RPC 2.0)

Every message is JSON following the JSON-RPC 2.0 standard:

Request:
```json
{"jsonrpc": "2.0", "method": "play", "params": {"file": "/data/music/song.wav"}, "id": 1}
```

Success response:
```json
{"jsonrpc": "2.0", "result": "ok", "id": 1}
```

Error response:
```json
{"error": "file not found"}
```

The `id` field lets the caller match a response to its request, important when multiple requests are sent.

**File:** `package/blackhand-ipc/src/cJSON.c` — the JSON parser/generator library.

### The Dispatch Table

Every service has its own `ipc_dispatch.c`. This file is NOT part of the shared IPC library — it is compiled directly into the service binary because it contains service-specific logic.

**Why not in the library?** Because `blackhand-audio/ipc_dispatch.c` calls `audio_play()`, which requires `audio_alsa.h`. If it were in `blackhand-ipc`, the library would have to link against ALSA — which makes no sense for a shared communication library.

The dispatch table is an array of `{method_name, handler_function}` pairs:

```c
static struct handler table[] = {
    {"ping",   handle_ping},
    {"play",   handle_play},
    {"volume", handle_volume},
    {"status", handle_status},
    {NULL, NULL}   // sentinel — marks the end of the array
};
```

`dispatch()` reads the `method` field from the incoming JSON, walks the table looking for a match, and calls the handler. If no match is found, it calls `send_error()`.

### Complete IPC Flow

The UI wants to play a song:

1. UI opens a connection to `/run/bh-audio.sock`
2. UI calls `send_msg()` with `{"jsonrpc":"2.0","method":"play","params":{"file":"/data/music/x.wav"},"id":1}`
3. `send_msg()` sends 4 bytes (message length) then the JSON bytes
4. Audio service's `main()` is blocked on `accept()`. A new client connected — it calls `accept()` which returns a new file descriptor for this specific connection
5. Audio service calls `recv_msg()` which reads the 4-byte length, then reads exactly that many bytes of JSON
6. `cJSON_Parse()` parses the JSON into a tree structure
7. `dispatch()` reads `method = "play"`, finds `handle_play` in the table, calls it
8. `handle_play()` reads `params.file`, calls `audio_play(path)`
9. `audio_play()` starts a background thread that opens the WAV file and writes PCM data to ALSA
10. `handle_play()` calls `send_result(fd, req, "ok")` which sends `{"jsonrpc":"2.0","result":"ok","id":1}`
11. UI receives the response and knows playback started

---

## Part 4: The Audio Service Deep Dive

**Files:** `package/blackhand-audio/src/`

### Two ALSA Layers

ALSA has two layers:
- **Kernel layer** — the `snd_bcm2835` kernel module that talks directly to the Pi's audio hardware. Loaded by `modprobe snd_bcm2835` in rcS.
- **Userspace layer** — `libasound` (the ALSA library, `BR2_PACKAGE_ALSA_LIB=y` in defconfig). Your C code talks to this library, which talks to the kernel.

### PCM vs Mixer

- **PCM** (Pulse-Code Modulation) — the raw audio data pipeline. You open a PCM device, configure sample rate/channels/format, then write audio frames to it. This is how music comes out of the speaker.
- **Mixer** — the volume control layer. Separate from PCM. You open a mixer, find the right element (e.g. "PCM" on the Pi), and set its volume value.

### The Playback Thread

`audio_play()` does not play audio itself. It creates a background thread (`playback_thread`) that reads the WAV file and feeds it to ALSA. This is because audio_play() must return immediately — blocking the IPC loop while playing audio would make the entire service unresponsive.

**Key bug fixes made:**

**`volatile int stop_requested`** — `volatile` tells the compiler "do not cache this in a register — always read from memory." Without it, the compiler might read `stop_requested` once, decide it's always 0, and optimize the check out of the loop. The playback thread reads this every iteration. `audio_stop()` sets it from the main thread. `volatile` ensures the thread always sees the latest value.

**Memory leak fix** — `audio_play()` calls `strdup(path)` to give the thread its own copy of the path string. The thread owns this memory and must `free(path)` at every exit point (there are 5 — each early return for error conditions, plus the normal end). Before the fix, path was cast as `const char*` so you couldn't call `free()`. After: cast as `char *path = (char *)arg`, freed at every exit.

**`pthread_join` instead of `pthread_detach`** — `detach` means "I will never join this thread." Once detached, you cannot wait for it to finish. This caused a race: `audio_stop()` sets `stop_requested=1` and returns immediately, then `audio_play()` starts a new thread. For a brief moment two threads could both be calling `snd_pcm_writei()` on the same PCM handle — which is not thread-safe and causes crashes or corruption. The fix: `pthread_join()` in `audio_stop()` blocks until the playback thread fully exits before returning. The mutex is unlocked BEFORE the join because the playback thread needs to acquire the mutex to set `playback_active=0` before it exits — holding it during the join would deadlock.

---

## Part 5: Files Changed and Why

| File | What Changed | Why |
|------|-------------|-----|
| `package/blackhand-init/src/init.c` | Added `log_fd`, `klog()`, opened `/dev/tty1` in `kmount_vfs()`, replaced all `write(STDERR_FILENO,...)` with `klog()`, added ASCII banner | Boot messages were going to tty3 (serial), invisible on display. Now they print on the HyperPixel screen. |
| `board/blackhand/overlay/etc/init.d/rcS` | Added `mkdir -p /dev/pts` and `mount -t devpts devpts /dev/pts` at the top. Changed udhcpc from background (`-b &`) to foreground. | devpts was never mounted — SSH PTY allocation failed, making SSH unusable. Two udhcpc instances were running due to double-backgrounding. |
| `board/blackhand/post-build.sh` | Changed dropbear cleanup to `rm -rf` inside the key-generation block instead of a separate pre-check | The separate `if [ ! -d ]` check was not catching all cases. Buildroot creates `/etc/dropbear` as a plain file, causing `mkdir -p` to fail with `set -euo pipefail`. The fix always removes and recreates the directory if the key doesn't exist. |
| `package/blackhand-audio/src/audio_alsa.c` | Added `volatile` to `stop_requested`, fixed memory leak on path_copy, replaced `pthread_detach` with `pthread_join` | Three bugs causing crashes, memory leaks, and race conditions. |
| `package/blackhand-audio/src/ipc_dispatch.c` | Created new file with full audio dispatch table | Service-specific dispatch must live in the service binary, not the shared library. |
| `package/blackhand-ipc/src/Makefile` | Removed `ipc_dispatch.c` from SRCS | Same reason — dispatch is service-specific. |
| `package/blackhand-ipc/blackhand-ipc.mk` | Added `INSTALL_STAGING = YES` and `INSTALL_STAGING_CMDS` | Other packages need the IPC headers at compile time. Without staging install, their builds fail with "ipc.h not found". |
| `package/blackhand-audio/src/Makefile` | Changed `CFLAGS ?=` to `override CFLAGS +=` | Buildroot passes CFLAGS on the command line. `?=` is silently overridden by command-line variables. `override +=` appends to whatever Buildroot passed, preserving the cross-compile include paths. |
| `package/blackhand-init/blackhand-init.mk` | Added `$(TARGET_CONFIGURE_OPTS)` to `BUILD_CMDS` | Without this, Buildroot's cross-compiler and sysroot paths are not passed to the Makefile — it compiles with the host gcc instead of the ARM cross-compiler. |
| `configs/blackhand_pi4_defconfig` | Added all BlackHand packages, kept `BR2_INIT_BUSYBOX=y` | Packages were not enabled. Kept busybox init so `/sbin/init` exists — `verify-image.sh` checks for it. Our init becomes PID 1 via `init=` in cmdline.txt, not by replacing busybox init. |

---

## Part 6: Roadmap

---

### Your Tasks (Stabilization + UI)

**Priority 1 — Stable Build**

These are already coded. You just need to rebuild and reflash:
- Rebuild: `make blackhand_pi4_defconfig && make`
- Verify on device: SSH works without manual steps, boot messages on display, all 3 service sockets exist

**Priority 2 — UI on Display**

The UI is already starting (PID 127 in your last ps output). One change needed:

In `init.c`, find `kspawn_shell()` and change:
```c
if (!kopen_tty("/dev/tty1"))
```
to:
```c
if (!kopen_tty("/dev/tty2"))
```

This moves the debug shell to tty2, freeing tty1 for the UI. The service manager already redirects the UI to tty1. Rebuild and reflash after this change.

**Priority 3 — Test Everything**

After reflash, verify in this order:
1. SSH into device, run `ls /run/bh-*.sock` — should show all 3
2. Run `ipc_test_client /run/bh-audio.sock '{"jsonrpc":"2.0","method":"ping","id":1}'`
3. Copy a WAV file to the device, test playback via IPC
4. Confirm UI renders on the display

---

### Other Engineer Tasks

**Task 1 — Define the IPC contract (do this first)**

Before writing any service code, document every JSON command for storage and modem in a shared file. Both you and the other engineer agree on the exact request/response format. This prevents the UI from being built against one format while the service implements another.

Example for storage:
```json
// Save a note
Request:  {"jsonrpc":"2.0","method":"note_save","params":{"title":"...","body":"..."},"id":1}
Response: {"jsonrpc":"2.0","result":{"id":42},"id":1}

// List all notes
Request:  {"jsonrpc":"2.0","method":"note_list","id":2}
Response: {"jsonrpc":"2.0","result":[{"id":1,"title":"..."},{"id":2,"title":"..."}],"id":2}
```

**Task 2 — Storage Service**

The service already starts and creates its socket. It needs three things:

1. **Database init** — at startup, open (or create) `/data/blackhand.db` using SQLite. Run `CREATE TABLE IF NOT EXISTS` for notes, alarms, contacts, settings. The `IF NOT EXISTS` ensures the tables are only created on first boot, not wiped on every restart.

2. **`ipc_dispatch.c`** — create this file inside `package/blackhand-storage/src/`. Follow the exact same pattern as `blackhand-audio/src/ipc_dispatch.c`. One handler per IPC method. Each handler parses the JSON params, runs a SQLite query, and sends back a JSON result.

3. **SQLite queries** — each handler calls the sqlite3 C API. The basic pattern:
   ```c
   sqlite3_stmt *stmt;
   sqlite3_prepare_v2(db, "INSERT INTO notes (title, body) VALUES (?,?)", -1, &stmt, NULL);
   sqlite3_bind_text(stmt, 1, title, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 2, body,  -1, SQLITE_STATIC);
   sqlite3_step(stmt);
   sqlite3_finalize(stmt);
   ```

**Task 3 — Modem Service**

Similar to storage but talks to hardware. Three things:

1. **Serial port setup** — open the modem's serial device (likely `/dev/ttyUSB0` for a USB modem or `/dev/ttyAMA0` for the Pi's UART). Configure baud rate, parity, stop bits using the `termios` API.

2. **AT command layer** — the `at_parser.c` stub already exists. AT commands are text strings sent over the serial port. `ATD+1234567890;` dials a number. `AT+CMGS="+1234567890"` sends an SMS. The modem responds with `OK` or `ERROR`.

3. **`ipc_dispatch.c`** — same pattern. `handle_call_dial` sends the AT dial command. `handle_sms_send` sends the SMS AT command.

**Task 4 — IPC Test Client in Build**

Add `ipc_test_client.c` to the `blackhand-ipc` Makefile so it is compiled and installed to `/usr/bin/ipc_test_client` on the device. Useful for testing any service directly from an SSH session without needing the UI.

---

### Future Phases

After the above is complete, the system will be fully functional. Future work:

- **Contacts screen** — storage service needs a contacts table, UI needs a contacts screen
- **Audio for calls** — modem uses its own audio path (not ALSA). When a call connects, the modem streams audio over its serial connection. This needs a separate handler.
- **Voice memos** — audio capture via ALSA (recording, not just playback). `snd_pcm_open(..., SND_PCM_STREAM_CAPTURE, ...)`.
- **Bluetooth** — the `blackhand-stt` (speech-to-text) package exists but is a stub. Bluetooth keyboard input for text entry.
- **PIN lock screen** — `pin_service.c` exists in the UI package. Needs to be wired to boot sequence.
- **OTA updates** — mechanism to pull a new sdcard.img over the network and reflash.
