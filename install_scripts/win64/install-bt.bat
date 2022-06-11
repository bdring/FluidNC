@echo off

call checksecurity.bat
if not %ErrorLevel% equ 0 (
   exit /b 1
)

set BuildType=bt
set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32 --baud 921600
set SetupArgs=--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect

set Bootloader=0x1000 common\bootloader_dio_80m.bin
set Bootapp=0xe000 common\boot_app0.bin
set Firmware=0x10000 %BuildType%\firmware.bin
set Partitions=0x8000 %BuildType%\partitions.bin

echo %EsptoolPath% %BaseArgs% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%
%EsptoolPath% %BaseArgs% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%

echo Starting fluidterm
win64\fluidterm.exe

pause
