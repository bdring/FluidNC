#!/bin/sh

. ./tools.sh

if ! check_security; then
    exit
fi

LocalFS="0x3d0000 wifi/spiffs.bin"
esptool_write $LocalFS
