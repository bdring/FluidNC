#pragma once

#include "Uart.h"

int xmodemReceive(Uart* serial, Print* out);
int xmodemTransmit(Uart* serial, Stream* out);
