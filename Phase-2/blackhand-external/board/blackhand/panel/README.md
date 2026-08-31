# Panel init blob

The kernel's `panel-mipi-dbi` driver does not know any panel's registers. It
gets the whole init sequence from a binary blob compiled from a text file
here, so switching controllers is a text edit rather than a driver patch.

```
panel.txt            ACTIVE — ST7789P, Rev2 / Touch module, 240x284
panel-nv3030b.txt    alternate — NV3030B, Rev1 non-touch module, 240x280
mkpanelbin.py        compiler  (panel.txt -> panel.bin)
```

The build consumes `../overlay/lib/firmware/panel.bin`, which is also
embedded into the kernel image via `CONFIG_EXTRA_FIRMWARE` (see
`BLACKHAND_INSTALL_PANEL_FIRMWARE` in `external.mk`).

## Regenerating after editing panel.txt

```sh
python3 mkpanelbin.py panel.txt ../overlay/lib/firmware/panel.bin
```

Then rebuild the kernel — the blob is linked into the image, so a rootfs-only
rebuild is not enough:

```sh
make linux-rebuild all
```

## Fast iteration on the init sequence

A full `make` is not needed to try a new sequence. `Image` lives on the FAT
boot partition, so once the kernel is rebuilt you can push just that:

```sh
scp output/images/Image root@<pi>:/boot/Image && ssh root@<pi> 'sync; reboot'
```

That turns each attempt into a couple of minutes instead of a full rebuild.

## Switching to the Rev1 / NV3030B panel

Three things change together — the controller, the resolution, AND the RAM
offset. Changing only the blob gives a lit panel with a shifted, wrapping
image, which is a confusing way to fail.

1. Compile the other sequence:

   ```sh
   python3 mkpanelbin.py panel-nv3030b.txt ../overlay/lib/firmware/panel.bin
   ```

2. Change the geometry in `../rpi-firmware/config.txt`:

   ```
   dtparam=width=240,height=280      # was height=284
   dtparam=x-offset=0,y-offset=20    # was y-offset=18
   ```

   The Rev1 offset is not a guess: Waveshare's own `LCD_1IN83_SetWindows()`
   adds a hardcoded `+20` to the row address in portrait orientation.

3. `make linux-rebuild all`, then reflash or push `Image` as above.

## Which panel do I have?

| Ribbon pins | Module | Controller | Resolution | y-offset |
|---|---|---|---|---|
| 8 | non-touch, **no** "Rev2" mark | NV3030B | 240x280 | 20 |
| 8 | non-touch, marked "Rev2" | ST7789P | 240x284 | 18 |
| 12 or 18 | touch module | ST7789P | 240x284 | 18 |

There is no software way to tell them apart on this board: MISO is not wired
(hence `write-only` in `config.txt`), so the controller ID cannot be read
back. It has to be identified visually.

## File format

`mkpanelbin.py` documents it in full. Briefly: a 15-byte `"MIPI DBI"` magic,
a version byte, then a flat `command, param_count, params...` stream. Delays
are the MIPI No-Op (`0x00`) carrying one byte of milliseconds, which is why
they cap at 255. The kernel validates all of it at probe and rejects a
malformed file with a clear dmesg line rather than a blank screen.
