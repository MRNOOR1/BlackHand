#!/usr/bin/env python3
"""
mkpanelbin.py — compile a panel init sequence into the panel.bin format that
the Linux panel-mipi-dbi driver expects.

    ./mkpanelbin.py panel.txt ../overlay/lib/firmware/panel.bin

WHY THIS EXISTS
---------------
Upstream ships a tool for this (notro/panel-mipi-dbi's mipi-dbi-cmd). This is a
30-line stdlib-only reimplementation so the build has no extra dependency and
the on-disk format is documented in-tree rather than in someone else's repo.

FILE FORMAT
-----------
Defined by struct panel_mipi_dbi_config in
drivers/gpu/drm/tiny/panel-mipi-dbi.c:

    offset  0  15 bytes  magic "MIPI DBI" + 7 NUL
    offset 15   1 byte   file format version, must be 1
    offset 16   n bytes  command stream

Command stream is a flat sequence of:

    <command byte> <param count> [<param byte> ...]

A delay is encoded as the MIPI No-Op command (0x00) carrying one parameter,
the delay in milliseconds. That is why delays cap at 255 ms — for longer,
emit several in a row.

The driver validates magic, version, and that the command array is exactly
consumed, so a malformed file fails at probe with a clear dmesg line rather
than a mystery black screen.
"""

import sys

MAGIC = b"MIPI DBI" + b"\x00" * 7   # 15 bytes
VERSION = 1


def parse(path):
    out = bytearray()
    for lineno, raw in enumerate(open(path), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        kind, args = parts[0].lower(), parts[1:]

        if kind == "delay":
            if len(args) != 1:
                sys.exit(f"{path}:{lineno}: delay takes exactly one value")
            ms = int(args[0], 0)
            if not 1 <= ms <= 255:
                sys.exit(f"{path}:{lineno}: delay {ms} out of range 1..255 "
                         f"(split it into several delays)")
            out += bytes((0x00, 0x01, ms))

        elif kind == "command":
            if not args:
                sys.exit(f"{path}:{lineno}: command needs at least an opcode")
            vals = [int(a, 0) for a in args]
            for v in vals:
                if not 0 <= v <= 0xFF:
                    sys.exit(f"{path}:{lineno}: 0x{v:x} is not a byte")
            if len(vals) - 1 > 255:
                sys.exit(f"{path}:{lineno}: too many parameters")
            out += bytes((vals[0], len(vals) - 1)) + bytes(vals[1:])

        else:
            sys.exit(f"{path}:{lineno}: unknown directive {kind!r} "
                     f"(expected 'command' or 'delay')")
    if not out:
        sys.exit(f"{path}: no commands found")
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <panel.txt> <panel.bin>")
    src, dst = sys.argv[1], sys.argv[2]

    commands = parse(src)
    blob = MAGIC + bytes((VERSION,)) + commands

    with open(dst, "wb") as f:
        f.write(blob)

    # Same walk the kernel does, so a bad file is caught here not on hardware.
    i, n = 0, len(commands)
    count = 0
    while i + 1 < n:
        i += 2 + commands[i + 1]
        count += 1
    if i != n:
        sys.exit(f"{dst}: malformed command array — kernel would reject this")

    print(f"{dst}: {len(blob)} bytes, {count} commands")


if __name__ == "__main__":
    main()
