#!/bin/sh

BuildType=wifi

if ! . ./tools.sh; then exit 1; fi

Bootloader="0x1000 ${BuildType}/bootloader.bin"
install

deactivate
