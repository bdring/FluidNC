#include "Driver/NVS.h"

#include <unordered_map>
#include <string>

static std::unordered_map<std::string, std::string> data;

static bool tryGetI32(const char* str, int32_t& value) {
    auto it = data.find(str);
    if (it != data.end() && it->second.size() == 4) {
        value = *reinterpret_cast<const int32_t*>(it->second.data());
        return true;
    }
    return false;
}

static bool tryGetI8(const char* str, int8_t& value) {
    auto it = data.find(str);
    if (it != data.end() && it->second.size() == 1) {
        value = *reinterpret_cast<const int8_t*>(it->second.data());
        return true;
    }
    return false;
}

static bool tryGetStr(const char* str, char* buf, size_t& len) {
    auto it = data.find(str);
    if (it != data.end()) {
        auto v = it->second.size();
        if (buf) {
            if (v > len) {
                v = len;
            }
            memcpy(buf, it->second.c_str(), v + 1);
            len = v;
        } else {
            len = v;
        }
        return true;
    }
    return false;
}
static bool tryGetBlob(const char* str, void* buf, size_t& len) {
    auto it = data.find(str);
    if (it != data.end()) {
        auto v = it->second.size();
        if (buf) {
            if (v > len) {
                v = len;
            }
            memcpy(buf, it->second.c_str(), v);
            len = v;
        } else {
            len = v;
        }
        return true;
    }
    return false;
}

static void set(const char* str, std::string value) {
    data[str] = value;
}

static void erase(const char* str) {
    auto it = data.find(str);
    if (it != data.end()) {
        data.erase(it);
    }
}

static void clear() {
    data.clear();
}

NVS::NVS(const char* name) {}
bool NVS::get_i8(const char* key, int8_t* out_value) {
    return tryGetI8(key, *out_value) ? 0 : 1;
}
bool NVS::get_i32(const char* key, int32_t* out_value) {
    return tryGetI32(key, *out_value) ? 0 : 1;
}
bool NVS::get_str(const char* key, char* out_value, size_t* length) {
    return tryGetStr(key, out_value, *length) ? 0 : 1;
}
bool NVS::get_blob(const char* key, void* out_value, size_t* length) {
    return tryGetBlob(key, out_value, *length) ? 0 : 1;
}
bool NVS::set_i8(const char* key, int8_t value) {
    char*       v = reinterpret_cast<char*>(&value);
    std::string data(v, v + 1);
    set(key, data);
    return false;
}
bool NVS::set_i32(const char* key, int32_t value) {
    char*       v = reinterpret_cast<char*>(&value);
    std::string data(v, v + 4);
    set(key, data);
    return false;
}
bool NVS::set_str(const char* key, const char* value) {
    set(key, value);
    return false;
}
bool NVS::set_blob(const char* key, const void* value, size_t length) {
    auto        c = static_cast<const char*>(value);
    std::string data(c, c + length);
    set(key, data);
    return false;
}
bool NVS::erase_key(const char* key) {
    erase(key);
    return false;
}
bool NVS::erase_all() {
    clear();
    return false;
}
bool NVS::get_stats(size_t& used, size_t& free, size_t& total) {
    used  = data.size();
    free  = 1000 - data.size();
    total = 1000;
    return false;
}
