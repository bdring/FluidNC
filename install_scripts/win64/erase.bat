@echo off

call checksecurity.bat
if not %ErrorLevel% equ 0 (
   exit /b 1
)

set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32 --baud 921600
set SetupArgs=--before default-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size detect

echo %EsptoolPath% %BaseArgs% erase-flash
%EsptoolPath% %BaseArgs% erase-flash

pause
