#pragma once

#include "Settings.h"

extern StringSetting* config_filename;

extern StringSetting* build_info;

extern StringSetting* start_message;

extern IntSetting* status_mask;

extern IntSetting* sd_fallback_cs;

extern EnumSetting* message_level;

extern EnumSetting* gcode_echo;

void make_proxies();
void make_coordinates();
