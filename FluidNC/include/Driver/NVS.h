#pragma once

// Interface to non-volatile key/value store based on the ESP32 API.
// If we are running on ESP32, we just delegate to that; otherwise we mimic it

#include <cstddef>
#include <cstdint>

class NVS {
public:
    NVS(const char* name);
    bool get_i32(const char* key, int32_t* out_value);
    bool get_i8(const char* key, int8_t* out_value);
    bool get_str(const char* key, char* out_value, size_t* length);
    bool get_blob(const char* key, void* out_value, size_t* length);
    bool erase_key(const char* key);
    bool erase_all();
    bool set_i8(const char* key, int8_t value);
    bool set_i32(const char* key, int32_t value);
    bool set_str(const char* key, const char* value);
    bool set_blob(const char* key, const void* value, size_t length);
    bool get_stats(size_t& used, size_t& free, size_t& total);
};
