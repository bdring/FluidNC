# Powershell script to download a FluidNC release and upload it to an ESP32
# Usage: ./InstallFluidNC.ps1 -version v3.1.0 -type wifi
# Type defaults to 'wifi'.  Other choices are 'bt', 'wifibt', and 'noradio'

param (
  [string]$type = "wifi",
  [string]$version = "latest"
)

$ErrorActionPreference = "Stop"

$FluidVer = 'fluidnc-' + $version + '-' + $type
$FluidZipFile =  $FluidVer + '.zip'
$FluidURI = 'https://github.com/bdring/FluidNC/releases/download/' + $version + '/' + $FluidZipFile
if (-Not (Test-Path $FluidVer)) {
   if (-Not (Test-Path $FluidZipFile)) {
     Write-Output "Downloading FluidNC $FluidVer from $FluidURI"
     Invoke-WebRequest -OutFile $FluidZipFile -Uri $FluidURI
   }
   Write-Output "Unpacking Fluid release $FluidVer"
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
   Expand-Archive $EspZipFile
}

Write-Output "Running Esptool to put $FluidVer on your ESP32"

$SetupArgs = '--chip', 'esp32', '--baud', '921600', '--before', 'default_reset', '--after', 'hard_reset', 'write_flash', '-z', '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', 'detect'

$BootloaderFile = $FluidVer + '/bootloader_qio_80m.bin'
$Bootloader = '0x1000', $BootloaderFile
$FirmwareFile = $FluidVer + '/firmware.bin'
$Firmware = '0x10000', $FirmwareFile
$PartitionsFile = $FluidVer + '/partitions.bin'
$Partitions = '0x8000', $PartitionsFile
$LocalFSFile=$FluidVer + '/spiffs.bin'
$LocalFS = '0x3d0000', $LocalFSFile

Write-Output "$EsptoolPath $SetupArgs $Firmware $Partitions $LocalFs"
& $EsptoolPath $SetupArgs $Firmware $Partitions $LocalFs
