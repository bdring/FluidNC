#!/bin/sh

EsptoolPath=macos/esptool

SetupArgs="--chip esp32 --baud 230400 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

LocalFS="0x3d0000 wifi/spiffs.bin"

echo $EsptoolPath $SetupArgs $LocalFS
$EsptoolPath $SetupArgs $LocalFS
