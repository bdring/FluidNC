@echo off

set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32 --baud 921600

rem Read the security fuses
%EsptoolPath% %BaseArgs% read_mem 0x3ff5a018 | findstr 3ff5a0 >SecurityFuses

if not %ErrorLevel% equ 0 (
   echo esptool failed
   pause
   exit
)

set /p SVAR=<SecurityFuses

if not "%SVAR%" == "0x3ff5a018 = 0x00000004" (
   echo Secure boot is enabled on this ESP32
   echo Loading FluidNC would probably brick it
   pause
   exit
)

set SetupArgs=--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect

set LocalFS=0x3d0000 wifi\spiffs.bin

echo %EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%
%EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%

echo Starting fluidterm
win64\fluidterm.exe

pause
