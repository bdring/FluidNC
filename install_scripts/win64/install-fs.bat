@echo off

set EsptoolPath=win64\esptool.exe

set SetupArgs=--chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect

set LocalFS=0x3d0000 wifi\spiffs.bin

echo %EsptoolPath% %SetupArgs% %LocalFS%
%EsptoolPath% %SetupArgs% %LocalFS%

echo Starting fluidterm
win64\fluidterm.exe

pause
