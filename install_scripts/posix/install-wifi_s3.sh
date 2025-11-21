#!/bin/sh

BuildType=wifi_s3

if ! . ./tools.sh; then exit 1; fi

Bootloader="0x0000 ${BuildType}/bootloader.bin"
install

deactivate
