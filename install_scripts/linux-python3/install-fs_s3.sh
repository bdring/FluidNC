#!/bin/sh

if ! . ./tools.sh; then exit 1; fi

LocalFS="0x610000 wifi_s3/littlefs.bin"

esptool_write $LocalFS

deactivate
