#!/bin/sh
install_dir=$(dirname "$0")
cd "$install_dir"

BuildType=wifi_s3
LocalFS="0x5F0000 wifi_s3/littlefs.bin"

if ! . ./tools.sh; then exit 1; fi

# check_security runs in esptool_erase & install
# if ! check_security; then exit 1; fi

esptool_erase

esptool_write $LocalFS

install

read  -n 1 -p $'\n\nInstallation Successful!\nPress any key to exit...' _input

deactivate