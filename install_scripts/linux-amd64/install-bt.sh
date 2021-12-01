#!/bin/sh

BuildType=bt
EsptoolPath=linux-amd64/esptool

SetupArgs="--chip esp32 --baud 230400 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

Bootloader="0x1000 common/bootloader_dio_80m.bin"
Bootapp="0xe000 common/boot_app0.bin"
Firmware="0x10000 ${BuildType}/firmware.bin"
Partitions="0x8000 ${BuildType}/partitions.bin"

echo $EsptoolPath $SetupArgs $Bootloader $Bootapp $Firmware $Partitions
$EsptoolPath $SetupArgs $Bootloader $Bootapp $Firmware $Partitions
