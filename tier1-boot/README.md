# Tier 1 — Boot & access

**Purpose:** power on into our own PID 1, supervise services, and be reachable over
Wi-Fi + SSH. A boot you can't reach can't be tested, so access lives here too.

**Builds on:** Tier 0.
**Needed by:** everything. Tier 2 and up only add `S*` start-up scripts, services
and overlay files on top of this.

## Hardware
CM4 Lite (SD boot) on the BlackHand carrier.
- **SW1** must be in the **non-BOOT** position, or the CM4 waits for USB boot.
- Press **SW6** to power on; USB-C power alone doesn't boot it.
- **Serial console:** P1 header (pin 1 GND, 2 RX, 3 TX), 3.3 V, 115200 8N1.
- **No ethernet:** Wi-Fi is the only network.

## Boot flow
1. The firmware reads `config.txt`/`cmdline.txt`. `init=/sbin/blackhand-init`
   makes our binary PID 1, so BusyBox init and `/etc/inittab` are never used.
2. `blackhand-init` mounts the virtual filesystems, remounts `/` rw and runs `rcS`.
3. `rcS` mounts `/boot`, sets up logging (`/var/log/blackhand/`) and restores the
   clock from `/data/.clock` (there's no RTC). It then runs every
   `/etc/init.d/SNN-*` in name order, each in a subshell. Last come sysinfo,
   the errors.log filter, and the 2 MB-per-file log cap.
   - `S10-wifi`: brcmfmac → wpa_supplicant → udhcpc, in the background.
   - `S20-ssh`: dropbear, key auth only (`-s`). The host key is generated on first
     boot, so each device has its own.
4. `blackhand-init` starts `blackhand-svc-mgr`, which starts
   `/etc/blackhand/services.conf` in order and restarts crashed services with
   backoff. An empty config is valid; the manager just idles.
5. `blackhand-init` keeps `/usr/bin/bh-session` on tty1 once Tier 2 provides it.
   Below Tier 2 it retries once a second.

## Getting in
1. Before building, create `tier1-boot/wifi.env` (gitignored):
   ```
   BLACKHAND_WIFI_SSID=MyNetwork
   BLACKHAND_WIFI_PSK=my passphrase
   ```
2. After flashing, copy your **public** key to the FAT partition as
   `authorized_keys`. `/boot/ssh` already exists; deleting it disables SSH.
3. Find the IP in your router or in `/boot/logs/last-boot.log`, then `ssh root@<ip>`.

## Tests
- Host: `make test TIER=1` (restart-policy self-test).
- Device: `make device-test TIER=1 HOST=<ip>` checks PID 1, the service manager,
  mounts, the boot log, Wi-Fi, dropbear with password auth off, the per-device host
  key, the tty2 shell being compiled out, and the manager being respawned when killed.
- **Gate (by hand):** run `reboot` and confirm the device comes back with SSH. The
  host key fingerprint must stay the same across reboots, and must differ after a
  fresh flash.

## Known issues
- Not yet verified on the new tree: everything above. The last good image was
  built from the pre-rebirth tree (Buildroot 2024.02.13).
- dropbear isn't supervised. If it dies, SSH is gone until reboot.
- No liveness watchdog. A wedged service, as opposed to one that exits, is never
  restarted.
- Shutdown gives services 5 s. Long blocking I/O can exceed that.
- `/boot` (FAT, no journal) is mounted rw for logs. Power loss mid-write can
  corrupt it.
- Carrier errata (no software fix): PD charging asks for 9 V, which the charger
  can't supply.
