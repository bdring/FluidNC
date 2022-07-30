#pragma once
bool littlefs_format(const char* partition_label);
bool littlefs_mount();
void littlefs_unmount();
