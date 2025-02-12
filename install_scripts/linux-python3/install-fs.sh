#!/bin/sh

if ! ./checksecurity.sh; then
    exit
fi

. ./tools.sh

LocalFS="0x5F0000 wifi/spiffs.bin"

esptool_write $LocalFS

echo Starting fluidterm
sh fluidterm.sh
