#pragma once

#include "Uart.h"

int xmodemReceive(Uart* serial, Channel* out);
int xmodemTransmit(Uart* serial, Channel* out);
