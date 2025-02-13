#!/bin/sh

BuildType=wifi_s3
LocalFS="0x5F0000 wifi_s3/littlefs.bin"

if ! . ./tools.sh; then exit 1; fi

# check_security runs in esptool_erase & install
# if ! check_security; then exit 1; fi

esptool_erase

esptool_write $LocalFS

install

deactivate