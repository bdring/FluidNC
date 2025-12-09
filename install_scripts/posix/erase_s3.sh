#!/bin/sh

if ! . ./tools.sh; then exit 1; fi

export chip="esp32s3"

esptool_erase

deactivate
