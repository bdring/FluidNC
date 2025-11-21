#!/bin/sh

BuildType=wifi_s3

if ! . ./tools.sh; then exit 1; fi

if ! check_security; then exit 1; fi

LocalFS="0x3d0000 ${BuildType}/littlefs.bin"
esptool_write $LocalFS

deactivate
