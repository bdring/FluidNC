#pragma once

// Interface to non-volatile key/value store based on the ESP32 API.
// If we are running on ESP32, we just delegate to that; otherwise we mimic it

#include <cstddef>
#include <cstdint>

class NVS {
private:
    const char* _name;

protected:
    const char* name() const { return _name; }

public:
    NVS(const char* name) : _name(name) {}
    virtual ~NVS() {}

    // All methods return true on failure
    virtual bool init() = 0;  // Initialize and detect/recover from corruption
    virtual bool get_i32(const char* key, int32_t* out_value) = 0;
    virtual bool get_i8(const char* key, int8_t* out_value) = 0;
    virtual bool get_str(const char* key, char* out_value, size_t* length) = 0;
    virtual bool get_blob(const char* key, void* out_value, size_t* length) = 0;
    virtual bool erase_key(const char* key) = 0;
    virtual bool erase_all() = 0;
    virtual bool set_i8(const char* key, int8_t value) = 0;
    virtual bool set_i32(const char* key, int32_t value) = 0;
    virtual bool set_str(const char* key, const char* value) = 0;
    virtual bool set_blob(const char* key, const void* value, size_t length) = 0;
    virtual bool get_stats(size_t& used, size_t& free, size_t& total) = 0;
};

extern NVS& nvs;
