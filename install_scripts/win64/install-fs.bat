@echo off


set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32s3 --baud 921600
set SetupArgs=--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect

set LocalFS=0x3d0000 wifi_s3\littlefs.bin

echo %EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%
%EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%


exit /b 0

echo Starting fluidterm
win64\fluidterm.exe

pause
