#pragma once
bool spiffs_format(const char* partition_label);
bool spiffs_mount(const char* label = "spiffs", bool format = false);
void spiffs_unmount();
