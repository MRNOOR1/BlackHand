#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="$1"

echo ">>> Black Hand OS post-build script"

# Create mount points
mkdir -p "${TARGET_DIR}/boot"
mkdir -p "${TARGET_DIR}/mnt/data"
mkdir -p "${TARGET_DIR}/data/notes"
mkdir -p "${TARGET_DIR}/data/music"
mkdir -p "${TARGET_DIR}/data/voice-memos"
mkdir -p "${TARGET_DIR}/data/alarms"
mkdir -p "${TARGET_DIR}/data/contacts"

# Build stamp for SSH verification
mkdir -p "${TARGET_DIR}/etc/blackhand"
BUILD_TIME_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
BUILD_COMMIT="unknown"
if command -v git >/dev/null 2>&1; then
  BUILD_COMMIT="$(git -C "${BR2_EXTERNAL_BLACKHAND_PATH}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
fi
printf "build_time_utc=%s\nbuild_commit=%s\n" "${BUILD_TIME_UTC}" "${BUILD_COMMIT}" \
  > "${TARGET_DIR}/etc/blackhand/build-id"

# The SPI panel is driven by the in-kernel panel-mipi-dbi driver (see
# board/blackhand/rpi-firmware/config.txt: dtoverlay=mipi-dbi-spi), so there are
# no custom device-tree overlays to compile here. The old HyperPixel/DPI
# overlays (hyperpixel4-touch, blackhand-fixes) were removed with the DPI panel.

# Generate dropbear SSH host key if it doesn't exist.
# Dropbear needs this to complete the SSH handshake. Without it the
# connection is closed immediately after the client sends its key exchange.
# We generate it here at build time so it's baked into the image and
# available on first boot — no manual setup needed on the device.
DROPBEAR_KEY_DIR="${TARGET_DIR}/etc/dropbear"
DROPBEAR_KEY="${DROPBEAR_KEY_DIR}/dropbear_ed25519_host_key"

# /etc/dropbear MUST be a real directory in the shipped image. Buildroot's
# dropbear package can leave it as a plain file or a symlink to /var/run/dropbear
# (keys-on-tmpfs). When it is not a directory, "mkdir -p" is a silent no-op on
# the device, dropbear has nowhere to write host keys, exits at startup, and
# every SSH connection is refused with no useful error. CONFIRMED on hardware.
#
# This check runs UNCONDITIONALLY -- it used to live inside the "key missing"
# branch below, so a stale TARGET_DIR that already had a key could ship a
# broken /etc/dropbear and skip the fix entirely.
#
# Test -L FIRST, and do not rely on -e. Buildroot's dropbear package ships
# /etc/dropbear as a symlink to /var/run/dropbear, and /var/run/dropbear does
# NOT exist inside TARGET_DIR at build time -- so the link is DANGLING. On a
# dangling symlink `-e` is FALSE (it follows the link and finds nothing), which
# made the old `[ -e ] && [ ! -d ]` guard skip silently. `mkdir -p` then fails
# with "File exists" because the symlink still occupies the path, and `set -e`
# aborts target-finalize. That is exactly how this broke the first real build.
#
# `[ ! -d ]` also covers "does not exist at all"; rm -rf on a missing path is a
# harmless no-op, so the two tests together are safe in every case.
if [ -L "${DROPBEAR_KEY_DIR}" ] || [ ! -d "${DROPBEAR_KEY_DIR}" ]; then
    if [ -L "${DROPBEAR_KEY_DIR}" ]; then
        echo ">>> /etc/dropbear is a symlink ($(readlink "${DROPBEAR_KEY_DIR}")) — replacing with a real directory"
    fi
    rm -rf "${DROPBEAR_KEY_DIR}"
fi
mkdir -p "${DROPBEAR_KEY_DIR}"

if [ ! -f "${DROPBEAR_KEY}" ]; then
    echo ">>> Generating dropbear ed25519 host key"

    # dropbearkey is a HOST tool built by Buildroot — it lives in HOST_DIR/bin.
    # We generate the key here at build time and bake it into the image so it is
    # ready on first boot. NOTE: dropbearkey IS also present on the device, and
    # rcS regenerates a key at runtime if this build-time step was skipped or if
    # /etc/dropbear turned out not to be a directory.
    HOST_DROPBEARKEY="${HOST_DIR}/bin/dropbearkey"

    if [ -x "${HOST_DROPBEARKEY}" ]; then
        "${HOST_DROPBEARKEY}" -t ed25519 -f "${DROPBEAR_KEY}"
        chmod 600 "${DROPBEAR_KEY}"
        echo ">>> Dropbear host key generated successfully"
    else
        echo "WARNING: ${HOST_DROPBEARKEY} not found — SSH will not work on device"
        echo "         Run: make dropbear-rebuild to ensure host tools are built"
    fi
else
    echo ">>> Dropbear host key already exists, skipping generation"
fi

# ── BCM4345C0 Bluetooth firmware ────────────────────────────────────────────
# The Pi 4 onboard BT chip (BCM4345C0) requires this firmware file.
# CONFIG_BT_HCIUART_BCM loads it automatically at boot via request_firmware().
# Without it the chip runs without firmware: pairing is unreliable,
# A2DP is broken, and bluetoothctl hangs waiting for the adapter.
BT_FW_DIR="${TARGET_DIR}/lib/firmware/brcm"
BT_FW_FILE="${BT_FW_DIR}/BCM4345C0.hcd"

if [ ! -f "${BT_FW_FILE}" ]; then
    echo ">>> Fetching BCM4345C0 Bluetooth firmware..."
    mkdir -p "${BT_FW_DIR}"
    # Use curl --fail so a GitHub HTML error page doesn't silently replace the
    # binary. wget -q accepts any 200 response, including redirect HTML pages.
    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --silent --show-error \
            -o "${BT_FW_FILE}" \
            "https://github.com/RPi-Distro/bluez-firmware/raw/master/broadcom/BCM4345C0.hcd" \
            && echo ">>> BCM4345C0.hcd installed ($(wc -c < "${BT_FW_FILE}") bytes)" \
            || { echo "WARNING: Could not fetch BT firmware — Bluetooth will be unreliable"; rm -f "${BT_FW_FILE}"; }
    else
        echo "WARNING: curl not found — place BCM4345C0.hcd in board/blackhand/overlay/lib/firmware/brcm/ manually"
    fi
else
    echo ">>> BCM4345C0.hcd already present, skipping download"
fi

# Create the device-specific symlink the kernel searches for first.
# Without it the kernel falls back to BCM4345C0.hcd (which works), but
# creating the symlink makes the lookup deterministic and matches how
# Raspberry Pi OS ships the firmware.
BT_FW_SYMLINK="${BT_FW_DIR}/BCM4345C0.raspberrypi,4-model-b.hcd"
if [ -f "${BT_FW_FILE}" ] && [ ! -e "${BT_FW_SYMLINK}" ]; then
    ln -sf BCM4345C0.hcd "${BT_FW_SYMLINK}"
    echo ">>> Created BT firmware symlink (device-specific name → BCM4345C0.hcd)"
fi

# ── Wi-Fi credentials (CYW43455) ────────────────────────────────────────────
# /etc/wpa_supplicant.conf is generated from the build environment so the SSID
# and passphrase are never committed. Wi-Fi is the board's ONLY network path,
# but a missing config must NEVER abort target-finalize: every failure below
# warns and carries on. rcS skips Wi-Fi when the file is absent.
#
# The wpa_supplicant package installs a default conf with an OPEN-network
# catch-all (key_mgmt=NONE) that would join any open AP. Always remove it, so
# "absent" really means "no Wi-Fi", and stale credentials from an earlier
# build in a reused TARGET_DIR never survive an unset build.
WPA_CONF="${TARGET_DIR}/etc/wpa_supplicant.conf"
rm -f "${WPA_CONF}"

# Persistent credentials for every build: board/blackhand/wifi.env (gitignored,
# never copied into the image). Environment variables, when set, win. Lines are
# KEY=value, read literally (no shell expansion); one pair of surrounding quotes
# is stripped, because a quoted value would silently hash the wrong passphrase.
WIFI_ENV="$(dirname "$0")/wifi.env"
if [ -f "${WIFI_ENV}" ]; then
    while IFS= read -r line || [ -n "${line}" ]; do
        line="${line%$'\r'}"
        v="${line#*=}"
        case "${v}" in \'*\'|\"*\") v="${v:1:${#v}-2}" ;; esac
        case "${line}" in
            BLACKHAND_WIFI_SSID=*) : "${BLACKHAND_WIFI_SSID:=${v}}" ;;
            BLACKHAND_WIFI_PSK=*)  : "${BLACKHAND_WIFI_PSK:=${v}}" ;;
        esac
    done < "${WIFI_ENV}"
    export BLACKHAND_WIFI_SSID BLACKHAND_WIFI_PSK
    unset line v
fi

wifi_warn() {
    echo "############################################################" >&2
    echo "WARNING: Wi-Fi NOT configured: $1" >&2
    echo "         The image has NO NETWORK (no ethernet, USB-C goes to the modem)" >&2
    echo "         and therefore no SSH. Put both in board/blackhand/wifi.env" >&2
    echo "         (gitignored, used by every build) or set them in the environment:" >&2
    echo "           BLACKHAND_WIFI_SSID  (network name, 1-32 bytes)" >&2
    echo "           BLACKHAND_WIFI_PSK   (8-63 chars, or 64 hex digits)" >&2
    echo "         With Docker pass them through: docker run -e BLACKHAND_WIFI_SSID -e BLACKHAND_WIFI_PSK ..." >&2
    echo "############################################################" >&2
}

if [ -z "${BLACKHAND_WIFI_SSID:-}" ] || [ -z "${BLACKHAND_WIFI_PSK:-}" ]; then
    wifi_warn "BLACKHAND_WIFI_SSID and/or BLACKHAND_WIFI_PSK not set"
elif ! command -v python3 >/dev/null 2>&1; then
    wifi_warn "python3 not found on the build host (needed to hash the passphrase)"
else
    # Hash the passphrase (PBKDF2-HMAC-SHA1, 4096 rounds -- what wpa_passphrase
    # does) so no plaintext PSK lands on the device. The SSID is written as hex
    # so quotes/newlines in it cannot break or inject into the config. Secrets
    # are read from the environment, not argv, so they stay out of `ps`.
    # shortcut: WPA2-PSK only (no SAE/WPA3, no hidden SSID), extend if needed.
    if WIFI_NET="$(python3 -I -c '
import hashlib, os, string, sys
ssid = os.environ["BLACKHAND_WIFI_SSID"].encode()
psk = os.environ["BLACKHAND_WIFI_PSK"]
if not 1 <= len(ssid) <= 32:
    sys.exit("SSID must be 1-32 bytes")
if len(psk) == 64 and all(c in string.hexdigits for c in psk):
    key = psk.lower()
elif 8 <= len(psk.encode()) <= 63:
    key = hashlib.pbkdf2_hmac("sha1", psk.encode(), ssid, 4096, 32).hex()
else:
    sys.exit("PSK must be 8-63 characters or 64 hex digits")
print(ssid.hex(), key)
')"; then
        read -r WIFI_SSID_HEX WIFI_PSK_HEX <<< "${WIFI_NET}"
        # The file holds a credential: create it 0600 (umask first, so it is
        # never briefly world-readable), root-owned by the image build.
        (
            umask 077
            printf 'country=AU\nap_scan=1\nnetwork={\n\tssid=%s\n\tpsk=%s\n\tkey_mgmt=WPA-PSK\n}\n' \
                "${WIFI_SSID_HEX}" "${WIFI_PSK_HEX}" > "${WPA_CONF}"
        )
        chmod 600 "${WPA_CONF}"
        echo ">>> Wi-Fi: wrote ${WPA_CONF} (mode 600, hashed PSK, country=AU)"
    else
        wifi_warn "invalid BLACKHAND_WIFI_SSID / BLACKHAND_WIFI_PSK (see error above)"
    fi
    unset WIFI_NET WIFI_SSID_HEX WIFI_PSK_HEX
fi

echo ">>> Post-build complete"
