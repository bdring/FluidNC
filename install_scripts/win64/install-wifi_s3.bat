@echo off

set BuildType=wifi_s3
set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32s3 --baud 921600
set SetupArgs=--before default-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size detect

set Bootloader=0x0000 %BuildType%\bootloader.bin
set Bootapp=0xe000 common\boot_app0.bin
set Firmware=0x10000 %BuildType%\firmware.bin
set Partitions=0x8000 %BuildType%\partitions.bin

echo %EsptoolPath% %BaseArgs% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%
%EsptoolPath% %BaseArgs% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%

echo Starting fluidterm
win64\fluidterm.exe

pause
