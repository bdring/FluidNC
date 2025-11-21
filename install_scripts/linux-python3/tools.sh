# Subroutines to call esptool with common arguments

# Ensure local binary path is in $PATH (pip3 default target path)
echo "$PATH" | grep '$HOME\/\.local\/bin' 2>&1 >/dev/null
if test "$?" != "0"; then
    export PATH="$HOME/.local/bin:$PATH"
fi

# Install esptool.py if needed
which esptool.py 2>&1 >/dev/null
if test "$?" != "0"; then
    echo esptool.py not found, attempting to install
    python3 -m pip install --user esptool
    if test "$?" != "0"; then
        echo esptool.py install failed
        exit 1
    fi
    which esptool.py 2>&1 >/dev/null
    if test "$?" != "0"; then
        echo esptool.py claims to have installed successfully, but cannot be found in PATH
        echo PATH= $PATH
        exit 1
    fi
fi

EsptoolPath=esptool.py

BaseArgs="--chip esp32 --baud 230400"

SetupArgs="--before default-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size detect"

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
