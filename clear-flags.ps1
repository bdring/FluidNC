# Remove the override of PLATFORMIO_BUILD_FLAGS
# This is useful if you were running build-all.ps1 and
# had to kill it with Ctrl-C, thus leaving
# PLATFORMIO_BUILD_FLAGS set to override the machine type.
# A clear-flags.sh equivalent is unnecessary, as build-all.sh
# does not set PLATFORMIO_BUILD_FLAGS globally
Remove-Item Env:PLATFORMIO_BUILD_FLAGS