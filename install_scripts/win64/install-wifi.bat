@echo off

set BuildType=wifi
set EsptoolPath=win64\esptool.exe

set SetupArgs=--chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect

set Bootloader=0x1000 common\bootloader_dio_80m.bin
set Bootapp=0xe000 common\boot_app0.bin
set Firmware=0x10000 %BuildType%\firmware.bin
set Partitions=0x8000 %BuildType%\partitions.bin

echo %EsptoolPath% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%
%EsptoolPath% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%

pause
