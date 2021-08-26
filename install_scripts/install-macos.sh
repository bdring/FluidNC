#!/bin/sh

EsptoolPath=../macos/esptool

SetupArgs="--chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

Bootloader=0x1000 bootloader_qio_80m.bin
Firmware=0x10000 firmware.bin
Partitions=0x8000 partitions.bin
LocalFS=0x3d0000 ../common/spiffs.bin

echo $EsptoolPath $SetupArgs $Firmware $Partitions $LocalFs
$EsptoolPath $SetupArgs $Firmware $Partitions $LocalFs
