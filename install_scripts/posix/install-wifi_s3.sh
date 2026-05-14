#!/bin/sh

BuildType=wifi_s3

if ! . ./tools.sh; then exit 1; fi

export chip="esp32s3"

Bootloader="0x0000 ${BuildType}/bootloader.bin"
install

deactivate
