# Tier 0 — Build

**Purpose:** turn the tier folders into a flashable image, the same way every time.

**Builds on:** nothing.
**Needed by:** every tier. If Tier 0 is broken, nothing can be built or tested.

## What's here
| File | Role |
|---|---|
| `Dockerfile` | Ubuntu 24.04 build environment, run as an unprivileged user |
| `build.sh` | runs in the container: clones Buildroot **2024.02.13**, stacks tiers 0..N, builds |
| `buildroot.fragment` | target CPU, toolchain, root filesystem |
| `tests/check.sh` | structure and script-syntax checks across all tiers |

## How tiers stack
`make image TIER=N` runs `build.sh N`. For each tier 0..N in order it collects:

- `buildroot.fragment`: appended to the defconfig
- `linux.fragment`: kernel config fragment
- `overlay/`: files copied into the root filesystem
- `post-build.sh`: rootfs fix-ups
- `services.conf`: appended to `/etc/blackhand/services.conf`
- `config.txt`: appended to `/boot/config.txt`

`build.sh` also writes `/etc/blackhand/build-id` (tier, git commit, build time).
After `defconfig` it **fails if Kconfig dropped any option a fragment asked for**.
In the old tree, `BR2_GCC_VERSION_14_X` and `BR2_BINUTILS_VERSION_2_45_X` don't
exist in 2024.02.13 and were silently ignored.

The `blackhand-cache` Docker volume holds Buildroot, downloads, ccache and one
output directory per tier, so a rebuild is incremental. To force a clean build of
one tier, run `make shell` and then `rm -rf /work/cache/output-tierN`.

## Tests
- `make test TIER=0`: every tier has a README and tests, no option is owned by two
  tiers, no two start-up scripts share a number, and every script parses.
- **Gate:** `make image TIER=1` succeeds from a clean cache.

## Known issues
- The Buildroot pin is a tag, not a hash. That's good enough unless upstream re-tags.
- Builds happen only in Docker. On macOS a case-insensitive bind mount breaks
  Buildroot output, which is why the output lives on the volume.
