#!/bin/sh

BuildType=wifi_s3

if ! . ./tools.sh; then exit 1; fi

chip="esp32s3"
export chip

LocalFS="0x3d0000 ${BuildType}/littlefs.bin"
esptool_write $LocalFS

deactivate
