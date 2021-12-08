@echo off
set EsptoolPath=win64\esptool.exe

set BaseArgs=--chip esp32 --baud 921600

rem Read the security fuses
echo %EsptoolPath% %BaseArgs% dump_mem 0x3ff5a018 4 SecurityFuses.bin
%EsptoolPath% %BaseArgs% dump_mem 0x3ff5a018 4 SecurityFuses.bin

if not %ErrorLevel% equ 0 (
   echo esptool failed
   pause
   exit /b 1
)

fc /b SecurityFuses.bin common\SecurityFusesOK.bin > nul 2>&1
if not %Errorlevel% equ 0 (
   echo *******************************************
   echo *  Secure boot is enabled on this ESP32   *
   echo * Loading FluidNC would probably brick it *
   echo *    !ABORTED! Read Wiki for more Info    *
   echo *******************************************
   del SecurityFuses.bin
   pause
   exit /b 1
)
del SecurityFuses.bin
exit /b 0
