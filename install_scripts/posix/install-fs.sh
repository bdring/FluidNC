#!/bin/sh

if ! . ./tools.sh; then exit 1; fi

if ! check_security; then exit 1; fi

LocalFS="0x3d0000 wifi/spiffs.bin"
esptool_write $LocalFS

deactivate
