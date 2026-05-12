#include "Driver/NVS.h"
#include <filesystem>
#include <fstream>

namespace stdfs = std::filesystem;

static stdfs::path NVSdir { "native_nvs" };

static stdfs::path munge(const char* name) {
    std::string mname(name);
    std::replace(mname.begin(), mname.end(), '/', '.');
    return NVSdir / mname;
}

NVS::NVS(const char* name) {
    stdfs::create_directory(NVSdir);
}

bool NVS::get_blob(const char* name, void* value, size_t* len) {
    std::ifstream file(munge(name));
    if (file.is_open()) {
        file.read((char*)value, *len);
        *len = file.gcount();
        return false;
    }
    *len = 0;
    return true;
}

bool NVS::get_str(const char* name, char* value, size_t* len) {
    std::ifstream file(munge(name));
    if (file.is_open()) {
        file.read(value, *len);
        *len        = file.gcount();
        value[*len] = '\0';
        return false;
    }
    *len = 0;
    return true;
}
bool NVS::set_blob(const char* name, const void* value, size_t length) {
    std::ofstream file(munge(name));
    if (file.is_open()) {
        file.write((const char*)value, length);
        return false;
    }
    return true;
}

bool NVS::set_str(const char* name, const char* value) {
    std::ofstream file(munge(name));
    if (file.is_open()) {
        file.write(value, strlen(value));
        return false;
    }
    return true;
}

bool NVS::get_i8(const char* name, int8_t* value) {
    std::ifstream file(munge(name));
    if (file.is_open()) {
        file.read((char*)value, 1);
        return false;
    }
    return true;
}
bool NVS::get_i32(const char* name, int32_t* value) {
    std::ifstream file(munge(name));
    if (file.is_open()) {
        file.read((char*)value, 4);
        return false;
    }
    return true;
}
bool NVS::set_i8(const char* name, int8_t value) {
    std::ofstream file(munge(name));
    if (file.is_open()) {
        file.write((char*)&value, 1);
        return false;
    }
    return true;
}
bool NVS::set_i32(const char* name, int32_t value) {
    std::ofstream file(munge(name));
    if (file.is_open()) {
        file.write((char*)&value, 4);
        return false;
    }
    return true;
}
bool NVS::erase_key(const char* name) {
    std::error_code ec;
    stdfs::remove(munge(name), ec);
    return !!ec;
}
bool NVS::erase_all() {
    std::error_code ec;
    stdfs::remove_all(NVSdir, ec);
    return !!ec;
}
bool NVS::get_stats(size_t& used, size_t& free, size_t& total) {
    used  = 0;
    free  = 0;
    total = 0;
    return true;
}
