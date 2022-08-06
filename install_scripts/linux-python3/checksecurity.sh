#!/bin/sh

. ./tools.sh

esptool_basic dump_mem 0x3ff5a018 4 SecurityFuses.bin

if ! cmp -s SecurityFuses.bin common/SecurityFusesOK.bin ; then
   echo *******************************************
   echo *  Secure boot is enabled on this ESP32   *
   echo * Loading FluidNC would probably brick it *
   echo *    !ABORTED! Read Wiki for more Info    *
   echo *******************************************
   cmp -l SecurityFuses.bin common/SecurityFusesOK.bin
   rm SecurityFuses.bin
   exit 1
fi

rm SecurityFuses.bin
exit 0
