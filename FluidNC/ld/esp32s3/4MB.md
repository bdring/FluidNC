# ESP32-S3 boards with 4 MB flash

Use the `wifi_s3_4m` environment for boards such as the ESP32-S3FH4R2
Super Mini (4 MB flash and 2 MB Quad PSRAM). The `wifi_s3` and
`wifi_s3_octalPSRAM` environments use an 8 MB partition table.

The current S3 firmware exceeds the capacity of the two application slots
in the older 4 MB OTA layout. This environment uses a single 3 MB factory
application and a 960 KiB LittleFS partition, ending at `0x400000`.
WiFi, WebUI, machine configuration and drivers are retained. Firmware
updates through WebUI and ArduinoOTA are disabled; use USB/serial to update.

Build with PlatformIO:

```sh
pio run -e wifi_s3_4m
pio run -e wifi_s3_4m -t build_merged
python tools/check_s3_4m_image.py .pio/build/wifi_s3_4m
```

The combined image is `.pio/build/wifi_s3_4m/merged-flash.bin` and must be
flashed at offset **0x0**. For the first installation, back up your machine
configuration, enter ROM download mode (hold BOOT while pressing RESET,
then release BOOT), close serial monitors, and run:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 115200 erase-flash
python -m esptool --chip esp32s3 --port PORT --baud 115200 write-flash --flash-size 4MB 0x0 .pio/build/wifi_s3_4m/merged-flash.bin
```

Replace `PORT` with your serial port (for example `COM10` on Windows).
Erasing flash also removes WiFi settings and configuration files. The
combined image installs the files in `FluidNC/data`; supply your own
`config.yaml` there before building the filesystem or upload it after boot.

Do not use the standard S3 release installer or WebUI firmware update to
install this variant: the partition table, bootloader, firmware and
filesystem must all match this layout. Subsequent USB updates can flash
only `firmware.bin` at `0x10000` to preserve configuration and files.
