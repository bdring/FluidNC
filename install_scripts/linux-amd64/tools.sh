# Subroutines to call esptool with common arguments

EsptoolPath=linux-amd64/esptool

BaseArgs="--chip esp32 --baud 230400"

SetupArgs="--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

esptool_basic () {
    echo echo $EsptoolPath $BaseArgs $*
    $EsptoolPath $BaseArgs $BaseArgs $*
    if test "$?" != "0"; then
        echo esptool failed
        exit 1
    fi
}
esptool_write () {
    esptool_basic $SetupArgs $*
}
