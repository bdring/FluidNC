#!/bin/sh

EsptoolPath=../linux-amd64/esptool

SetupArgs="--chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

Bootloader="0x1000 ../common/bootloader_dio_80m.bin"
Bootapp="0xe000 ../common/boot_app0.bin"
Firmware="0x10000 firmware.bin"
Partitions="0x8000 partitions.bin"
LocalFS="0x3d0000 ../common/spiffs.bin"

echo $EsptoolPath $SetupArgs $Bootloader $Bootapp $Firmware $Partitions $LocalFs
$EsptoolPath $SetupArgs $Bootloader $Bootapp $Firmware $Partitions $LocalFs
