#!/bin/sh

BuildType=wifi

if ! . ./tools.sh; then exit 1; fi

export chip="esp32"

Bootloader="0x1000 ${BuildType}/bootloader.bin"
install

deactivate
