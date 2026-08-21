# USB bootstrap for the v4.0.4 diagnostic image

This is the fallback when the already-installed HTTP OTA handler disconnects
before a full application image reaches the inactive OTA partition. It must be
run only while the machine is physically unable to move or start its spindle.

## Bound artifact

- staged file: `debug-v4.0.4-webhardening-r2-soak-bound-firmware.bin`
- `firmware.bin`: 1,815,216 bytes
- SHA-256: `28A8A2FFD167E1DA4F65F34D2FFC18C418BE8C9927389E60FD4AFE777DB3D4A0`
- `firmware.elf` SHA-256:
  `62983A61C0350C6111A13722135A61F623CAE99B8FED91A989D10EB639FB6070`
- Environment: `wifi`, ESP32, 4 MiB flash, `min_littlefs.csv`
- Live identity after boot: `[ESP420]` field `Diagnostic hardening ID` must
  equal `v4.0.4-webhardening-20260820-r2` and `Diagnostic boot sequence` must
  be positive.

The superseded r1/F7935559 artifact must not be flashed. Its harness contract
predates the final per-client stale timing and all-or-nothing transport gates.

The partition layout keeps NVS at `0x9000..0xdfff` and LittleFS at
`0x3d0000..0x3fffff`. Never use `erase_flash` and never flash a merged image
whose `0xff` padding spans either region.

## Preferred command

After identifying the new physical USB serial port (not a Bluetooth COM port):

```powershell
pio run -e wifi -t upload --upload-port COMx
```

PlatformIO writes the already-built non-contiguous images and leaves NVS and
LittleFS untouched. Before running it, require that `.pio/build/wifi/firmware.bin`
has the SHA-256 above and that the selected COM device identifies as the newly
attached ESP32 USB/UART adapter.

## Explicit equivalent

If PlatformIO port detection is unavailable, use its packaged `esptool.py` to
write these separate segments in one invocation:

```text
0x1000  .pio/build/wifi/bootloader.bin
0x8000  .pio/build/wifi/partitions.bin
0xe000  framework-arduinoespressif32/tools/partitions/boot_app0.bin
0x10000 .pio/build/wifi/firmware.bin
```

Do not merge the files and do not add an NVS or filesystem segment. After the
reset, run the read-only evidence collector and require the new firmware ID,
the same configured-file hash, the same runtime configuration, and an empty
unexpected-reset delta before any fault injection.
