#pragma once
bool spiffs_format(const char* partition_label);
bool spiffs_mount();
void spiffs_unmount();
