#pragma once

#include <cstdint>

#include "Channel.h"
#include "FileStream.h"

int32_t xmodemReceive(Channel* serial, FileStream* outfile);
int32_t xmodemTransmit(Channel* serial, FileStream* infile);
