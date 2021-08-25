# Powershell script to download a FluidNC release and upload it to an ESP32
# Usage: ./InstallFluidNC.ps1 -version 3.1 -type wifi
# Type defaults to 'wifi'.  Other choices are 'bt', 'wifibt', and 'noradio'

param (
  [string]$type = "wifi",
  [string]$version = "latest"
)

$ErrorActionPreference = "Stop"

$FluidDir = 'fluidnc-' + $type
# $FluidDir = '.pio/build/' + $type + '/'
$FluidZipFile =  $FluidDir + '.zip'
$FluidURI = 'https://github.com/bdring/FluidNC/releases/download/' + $version + '/' + $FluidZipFile
if (-Not (Test-Path $FluidDir)) {
   if (-Not (Test-Path $FluidZipFile)) {
     Write-Output "Downloading FluidNC $FluidDir ($version) from $FluidURI"
     Invoke-WebRequest -OutFile $FluidZipFile -Uri $FluidURI
   }
   Write-Output "Unpacking Fluid release $FluidDir"
   Expand-Archive $FluidZipFile
}

# Get esptool
$ESPTOOL_VERSION = "v3.1"
$EspDirName = "esptool-" + $ESPTOOL_VERSION +"-win64"
$EspZipFile = $EspDirName + ".zip"
$EspURI = "https://github.com/espressif/esptool/releases/download/" + $ESPTOOL_VERSION + "/" + $EspZipFile
$EsptoolPath = $EspDirName + "/" + $EspDirName + "/esptool.exe" 
if (-Not (Test-Path $EsptoolPath)) {
   if (-Not (Test-Path $EspZipFile)) {
     Write-Output "Downloading Esptool"
     Invoke-WebRequest -OutFile $EspZipFile -Uri $EspURI
   }
   Write-Output "Unpacking Esptool"
   Expand-Archive $ZipFile
}

Write-Output "Running Esptool to put $FluidDir on your ESP32"

$SetupArgs = '--chip', 'esp32', '--baud', '921600', '--before', 'default_reset', '--after', 'hard_reset', 'write_flash', '-z', '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', 'detect'

$Bootloader = '0x1000', $FluidDir + 'bootloader_qio_80m.bin'
$Firmware = '0x10000', $FluidDir + 'firmware.bin'
$Partitions = '0x8000', $FluidDir + 'partitions.bin'
$LocalFS = '0x3d0000', $FluidDir + 'spiffs.bin'

& $EsptoolPath $SetupArgs $Firmware $Partitions $LocalFs
