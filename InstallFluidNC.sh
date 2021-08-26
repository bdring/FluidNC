# Shell script to download a FluidNC release and upload it to an ESP32
# Usage: ./InstallFluidNC.sh -version v3.1.0 -type wifi
# Type defaults to 'wifi'.  Other choices are 'bt', 'wifibt', and 'noradio'

while (( "$#" )); do
  case "$1" in
    -version)
      if [ -n "$2" ] && [ ${2:0:1} != "-" ]; then
        VERSION=$2
        shift 2
      else
        echo "Error: Argument for $1 is missing" >&2
        exit 1
      fi
      ;;
    -type)
      if [ -n "$2" ] && [ ${2:0:1} != "-" ]; then
        TYPE=$2
        shift 2
      else
        echo "Error: Argument for $1 is missing" >&2
        exit 1
      fi
      ;;
  esac
done

if test "$VERSION" = ""; then
   VERSION=latest
fi

if test "$TYPE" = ""; then
   TYPE=wifi
fi

if test "$OSTYPE" = 'darwin'; then
    OS=macos
else
    OS=linux-amd64
fi

FluidVer=fluidnc-${VERSION}-${TYPE}
FluidZipFile=${FluidVer}.zip
FluidURI=https://github.com/bdring/FluidNC/releases/download/${VERSION}/${FluidZipFile}
if test ! -d "$FluidVer"; then
   if test ! -e "$FluidZipFile"; then
     echo Downloading FluidNC $FluidVer from $FluidURI
     curl -sS $FluidURI >$FluidZipFile
   fi
   if test ! -d "$FluidZipFile"; then
       echo Cannot download $FluidURI
       exit 1
   fi
   echo Unpacking Fluid release $FluidVer
   unzip -q $FluidZipFile
fi

# Get esptool
ESPTOOL_VERSION=v3.1
EspDirName=esptool-${ESPTOOL_VERSION}-${OS}
EspZipFile=${EspDirName}.zip
EspURI=https://github.com/espressif/esptool/releases/download/${ESPTOOL_VERSION}/${EspZipFile}
echo $EspURI
EsptoolPath=${EspDirName}/esptool
if test ! -d "$EsptoolPath"; then
   if test ! -e "$EspZipFile"; then
     echo Downloading Esptool
     curl -sS $EspURI >$EspZipFile 
   fi
   if test ! -e "$EspZipFile"; then
       echo Cannot download $EspZipFile
       exit 1
   fi
   echo Unpacking Esptool
   unzip -q $EspZipFile
fi

echo Running Esptool to put $FluidDir on your ESP32

SetupArgs="--chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

Bootloader=0x1000 $FluidDir/bootloader_qio_80m.bin
Firmware=0x10000 $FluidDir/firmware.bin
Partitions=0x8000 $FluidDir/partitions.bin
LocalFS=0x3d0000 $FluidDir/spiffs.bin

echo $EsptoolPath $SetupArgs $Firmware $Partitions $LocalFs
$EsptoolPath $SetupArgs $Firmware $Partitions $LocalFs
