# Tier 3 — Plumbing

**Purpose:** the IPC library every daemon and the UI talk through, and the storage
daemon that owns persistent data.

**Builds on:** Tier 1 (the service manager starts storage).
**Needed by:** Tier 4 (UI reads contacts and settings) and Tier 5 (modem writes
call/SMS history).

## What's here
| Item | Role |
|---|---|
| `blackhand-ipc/` | `libblackhand-ipc.a`: length-prefixed JSON over Unix sockets, cJSON, country codes / E.164 |
| `blackhand-storage/` | daemon on `/run/bh-storage.sock`, owns `/data/contacts` and `/data/numbers/<E.164>/` |
| `services.conf` | starts storage first |
| `S40-data` | clears group/other bits on `/data` left by older images |

## Contracts
- **IPC:** one I/O timeout and a hard message-size ceiling. A partial or oversize
  frame shuts the stream down, so the caller must reconnect. Servers accept with
  2 s I/O timeouts.
- **Storage:** writes are atomic (tmp + fsync + rename + fsync dir), dirs 0700,
  files 0600. A present-but-unparseable file is an error, never silently "empty".
  History is capped. A 256 KiB reserve is released on ENOSPC.
- `ipc_framing.h` is included by the IPC library, storage, the UI and the Tier 5
  daemons. Check all of them before changing it.

## Tests
- Host: `make test TIER=3` covers the IPC round trip, oversize and partial frames,
  cJSON, country codes, an incomplete frame invalidating the connection, and
  storage wipe, collision and atomic renumber.
- Device: `make device-test TIER=3 HOST=<ip>` checks that storage is running, the
  socket exists, `/data` is private, and storage restarts after a kill.
- **Gate:** contacts survive a reboot. This needs a client; the UI provides one
  in Tier 4.

## Known issues
- There's no command-line IPC client on the device, so storage can't be exercised
  without the UI. A tiny `bh-ipc` tool would make this tier testable on its own.
- `send_result`/`param_string` helpers are copied into each daemon's dispatch code
  instead of living in the library.
- A continuously active client can monopolise a single-threaded server.
- White Wipe is a logical delete, not a secure erase.
