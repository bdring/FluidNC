#!/bin/sh

if ! . ./tools.sh; then exit 1; fi

chip="esp32s3"
export chip

esptool_erase

deactivate
