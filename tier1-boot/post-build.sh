#!/usr/bin/env bash
# Tier 1 rootfs fix-ups. Args: TARGET_DIR, then build.sh's generated dir.
set -euo pipefail

TARGET_DIR="$1"

mkdir -p "${TARGET_DIR}/boot" "${TARGET_DIR}/data"

# /etc/dropbear must be a real directory. Buildroot ships it as a symlink to
# /var/run/dropbear, which dangles in TARGET_DIR (so test -L first, not -e).
# The host key is NOT made here: S20-ssh generates it on first boot, so each
# device gets its own key instead of every flash sharing the image's key.
DROPBEAR_DIR="${TARGET_DIR}/etc/dropbear"
if [ -L "${DROPBEAR_DIR}" ] || [ ! -d "${DROPBEAR_DIR}" ]; then
    rm -rf "${DROPBEAR_DIR}"
fi
mkdir -p "${DROPBEAR_DIR}"
rm -f "${DROPBEAR_DIR}"/dropbear_*_host_key

# ── Wi-Fi credentials (CYW43455) ────────────────────────────────────────────
# /etc/wpa_supplicant.conf is generated from the build environment so the SSID
# and passphrase are never committed. Wi-Fi is the board's ONLY network path,
# but a missing config must NEVER abort target-finalize: every failure below
# warns and carries on. S10-wifi skips Wi-Fi when the file is absent.
#
# The wpa_supplicant package installs a default conf with an OPEN-network
# catch-all (key_mgmt=NONE) that would join any open AP. Always remove it, so
# "absent" really means "no Wi-Fi", and stale credentials from an earlier
# build in a reused TARGET_DIR never survive an unset build.
WPA_CONF="${TARGET_DIR}/etc/wpa_supplicant.conf"
rm -f "${WPA_CONF}"

# Credentials come from tier1-boot/wifi.env (gitignored, never copied into
# the image); environment variables win. Lines are KEY=value, read literally
# (no shell expansion); one pair of surrounding quotes is stripped, because a
# quoted value would silently hash the wrong passphrase.
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
    echo "         The image has NO NETWORK and therefore no SSH." >&2
    echo "         Put BLACKHAND_WIFI_SSID and BLACKHAND_WIFI_PSK in" >&2
    echo "         tier1-boot/wifi.env (gitignored) or the environment." >&2
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
