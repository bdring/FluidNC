#!/bin/sh

if ! ./checksecurity.sh; then
    exit
fi

. ./tools.sh

BuildType=bt

Bootloader="0x1000 common/bootloader_dio_80m.bin"
Bootapp="0xe000 common/boot_app0.bin"
Firmware="0x10000 ${BuildType}/firmware.bin"
Partitions="0x8000 ${BuildType}/partitions.bin"

esptool_write $Bootloader $Bootapp $Firmware $Partitions

echo Starting fluidterm
sh fluidterm.sh
