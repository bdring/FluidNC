#!/bin/sh

BuildType=bt
EsptoolPath=macos/esptool

BaseArgs="--chip esp32 --baud 230400"

SECV=`$EsptoolPath $BaseArgs read_mem 0x3ff5a018 | grep 3ff5a0`

if test "$?" != "0"; then
   echo esptool failed
   exit
fi

if test "$SECV" != "0x3ff5a018 = 0x00000004"; then
   echo Secure boot is enabled on this ESP32
   echo Loading FluidNC would probably brick it
   exit
fi

SetupArgs="--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

Bootloader="0x1000 common/bootloader_dio_80m.bin"
Bootapp="0xe000 common/boot_app0.bin"
Firmware="0x10000 ${BuildType}/firmware.bin"
Partitions="0x8000 ${BuildType}/partitions.bin"

echo $EsptoolPath $BaseArgs $SetupArgs $Bootloader $Bootapp $Firmware $Partitions
$EsptoolPath $BaseArgs $SetupArgs $Bootloader $Bootapp $Firmware $Partitions

echo Starting fluidterm
python3 -m pip install -q pyserial xmodem
python3 fluidterm/fluidterm.py

