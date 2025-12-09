#!/bin/sh

BuildType=wifi_s3

if ! . ./tools.sh; then exit 1; fi

export chip="esp32s3"

LocalFS="0x3d0000 wifi/littlefs.bin"
esptool_write $LocalFS

deactivate
