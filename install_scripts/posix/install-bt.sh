#!/bin/sh

BuildType=bt

if ! . ./tools.sh; then exit 1; fi

chip="esp32"
export chip

Bootloader="0x1000 ${BuildType}/bootloader.bin"
install

deactivate
