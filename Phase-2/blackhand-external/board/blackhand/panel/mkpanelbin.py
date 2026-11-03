#!/usr/bin/env python3
"""Build a panel-mipi-dbi firmware blob (panel.bin) from a text sequence.

Stdlib only — no dependency on the upstream notro/panel-mipi-dbi tool.

Blob format (from drivers/gpu/drm/tiny/panel-mipi-dbi.c):

    offset  0   15 bytes   magic "MIPI DBI" + NUL padding
    offset 15    1 byte    file format version, must be 1
    offset 16    n bytes   command stream

The command stream is flat: for each entry
    command_byte, param_count, param0 .. paramN
A delay is the MIPI No-Op (0x00) carrying a single millisecond byte, i.e.
    0x00, 0x01, <ms>
so delays are capped at 255 ms.

Input text format (see panel.txt / panel-nv3030b.txt):
    command 0xNN [0xNN ...]   command byte followed by its parameters
    delay   N                 wait N ms (0-255); N may be decimal or 0xNN
    # comment                 comments and blank lines are ignored

Usage:
    mkpanelbin.py INPUT.txt OUTPUT.bin
"""

import sys

MAGIC = b"MIPI DBI"          # 8 bytes; padded to 15 below
MAGIC_LEN = 15
VERSION = 1


def parse_byte(tok):
    """Parse a decimal or 0x-hex token into an int in 0..255."""
    val = int(tok, 0)
    if not 0 <= val <= 255:
        raise ValueError("byte out of range 0..255: %s" % tok)
    return val


def build_stream(lines):
    stream = bytearray()
    for lineno, raw in enumerate(lines, 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        kind = parts[0].lower()
        try:
            if kind == "command":
                if len(parts) < 2:
                    raise ValueError("command needs at least a command byte")
                cmd = parse_byte(parts[1])
                params = [parse_byte(p) for p in parts[2:]]
                if len(params) > 255:
                    raise ValueError("too many parameters (max 255)")
                stream.append(cmd)
                stream.append(len(params))
                stream.extend(params)
            elif kind == "delay":
                if len(parts) != 2:
                    raise ValueError("delay needs exactly one value")
                ms = parse_byte(parts[1])
                stream.append(0x00)   # No-Op
                stream.append(0x01)   # one parameter
                stream.append(ms)     # milliseconds
            else:
                raise ValueError("unknown directive: %s" % parts[0])
        except ValueError as exc:
            sys.exit("mkpanelbin: line %d: %s" % (lineno, exc))
    if not stream:
        sys.exit("mkpanelbin: input produced an empty command stream")
    return stream


def verify(blob):
    """Re-parse the blob exactly as the kernel does, to prove it is consumable."""
    if blob[:MAGIC_LEN] != MAGIC + b"\x00" * (MAGIC_LEN - len(MAGIC)):
        sys.exit("mkpanelbin: magic check failed")
    if blob[MAGIC_LEN] != VERSION:
        sys.exit("mkpanelbin: version byte is not 1")
    i = MAGIC_LEN + 1
    n = len(blob)
    entries = 0
    while i < n:
        if i + 2 > n:
            sys.exit("mkpanelbin: truncated entry header at offset %d" % i)
        num_params = blob[i + 1]
        i += 2 + num_params
        entries += 1
    if i != n:
        sys.exit("mkpanelbin: stream not exactly consumed (ended at %d of %d)"
                 % (i, n))
    return entries


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: mkpanelbin.py INPUT.txt OUTPUT.bin")
    infile, outfile = sys.argv[1], sys.argv[2]

    with open(infile, "r") as f:
        stream = build_stream(f.readlines())

    blob = bytearray()
    blob += MAGIC + b"\x00" * (MAGIC_LEN - len(MAGIC))
    blob.append(VERSION)
    blob += stream

    entries = verify(blob)

    with open(outfile, "wb") as f:
        f.write(blob)

    print("mkpanelbin: wrote %s (%d bytes, %d entries)"
          % (outfile, len(blob), entries))


if __name__ == "__main__":
    main()
