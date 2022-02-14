#!/bin/sh

if ! . ./tools.sh; then exit 1; fi

esptool_erase

deactivate
