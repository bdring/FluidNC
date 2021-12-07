#!/bin/sh

if ! ./checksecurity.sh; then
    exit
fi

. ./tools.sh

LocalFS="0x3d0000 wifi/spiffs.bin"

esptool_write $LocalFS

echo Starting fluidterm
sh fluidterm.sh
