@echo off

call checksecurity.bat
if not %ErrorLevel% equ 0 (
   exit /b 1
)

set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32 --baud 921600
set SetupArgs=--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect

set LocalFS=0x3d0000 wifi\spiffs.bin

echo %EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%
%EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%

echo Starting fluidterm
win64\fluidterm.exe

pause
