@echo off

set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32s3 --baud 921600
set SetupArgs=--before default-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size detect

set LocalFS=0x610000 wifi_s3\littlefs.bin

echo %EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%
%EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%

echo Starting fluidterm
win64\fluidterm.exe

pause
