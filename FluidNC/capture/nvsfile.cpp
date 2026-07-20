#include "Driver/NVS.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace stdfs = std::filesystem;

class FILE_NVS : public NVS {
private:
    stdfs::path _nvsdir { "native_nvs" };

    stdfs::path munge(const char* key) {
        std::string mname(key);
        std::replace(mname.begin(), mname.end(), '/', '.');
        return _nvsdir / mname;
    }

public:
    FILE_NVS(const char* name) : NVS(name) {
        stdfs::create_directory(_nvsdir);
    }

    bool init() override {
        return false;
    }

    bool get_i32(const char* key, int32_t* out_value) override {
        std::ifstream file(munge(key));
        if (file.is_open()) {
            file.read((char*)out_value, 4);
            return false;
        }
        return true;
    }

    bool get_i8(const char* key, int8_t* out_value) override {
        std::ifstream file(munge(key));
        if (file.is_open()) {
            file.read((char*)out_value, 1);
            return false;
        }
        return true;
    }

    bool get_str(const char* key, char* out_value, size_t* length) override {
        std::ifstream file(munge(key));
        if (file.is_open()) {
            if (out_value) {
                file.read(out_value, *length);
                *length = file.gcount();
                out_value[*length] = '\0';
            } else {
                file.seekg(0, std::ios_base::end);
                *length = (size_t)file.tellg() + 1;
            }
            return false;
        }
        *length = 0;
        return true;
    }

    bool get_blob(const char* key, void* out_value, size_t* length) override {
        std::ifstream file(munge(key));
        if (file.is_open()) {
            if (out_value) {
                file.read((char*)out_value, *length);
                *length = file.gcount();
            } else {
                file.seekg(0, std::ios_base::end);
                *length = (size_t)file.tellg();
            }
            return false;
        }
        *length = 0;
        return true;
    }

    bool erase_key(const char* key) override {
        std::error_code ec;
        stdfs::remove(munge(key), ec);
        return !!ec;
    }

    bool erase_all() override {
        std::error_code ec;
        stdfs::remove_all(_nvsdir, ec);
        return !!ec;
    }

    bool set_i8(const char* key, int8_t value) override {
        std::ofstream file(munge(key));
        if (file.is_open()) {
            file.write((char*)&value, 1);
            return false;
        }
        return true;
    }

    bool set_i32(const char* key, int32_t value) override {
        std::ofstream file(munge(key));
        if (file.is_open()) {
            file.write((char*)&value, 4);
            return false;
        }
        return true;
    }

    bool set_str(const char* key, const char* value) override {
        std::ofstream file(munge(key));
        if (file.is_open()) {
            file.write(value, strlen(value));
            return false;
        }
        return true;
    }

    bool set_blob(const char* key, const void* value, size_t length) override {
        std::ofstream file(munge(key));
        if (file.is_open()) {
            file.write((const char*)value, length);
            return false;
        }
        return true;
    }

    bool get_stats(size_t& used, size_t& free, size_t& total) override {
        used  = 0;
        free  = 0;
        total = 0;
        return true;
    }
};

FILE_NVS file_nvs("FluidNC");
NVS&     nvs = file_nvs;
