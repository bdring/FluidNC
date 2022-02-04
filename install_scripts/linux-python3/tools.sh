# Subroutines to call esptool with common arguments

# Install esptool.py if needed
which esptool.py
if test "$?" != "0"; then
    echo esptool.py not found, attempting to install
    python3 -m pip install esptool
    if test "$?" != "0"; then
        echo esptool.py install failed
        exit 1
    fi
fi

EsptoolPath=esptool.py

BaseArgs="--chip esp32 --baud 230400"

SetupArgs="--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

esptool_basic () {
    echo echo $EsptoolPath $BaseArgs $*
    $EsptoolPath $BaseArgs $BaseArgs $*
    if test "$?" != "0"; then
        echo esptool.py failed
        exit 1
    fi
}
esptool_write () {
    esptool_basic $SetupArgs $*
}
