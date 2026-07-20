#include "Driver/NVS.h"
#include <nvs.h>
#include <nvs_flash.h>

#include <cstdint>

class ESP_NVS : public NVS {
private:
    nvs_handle_t _handle;

    // nvs_open() does not work when the NVS constructor is first
    // called, so we have to do it later.
    nvs_handle_t handle() {
        if (!_handle) {
#ifndef Arduino_h
            // Init NVS and recreate if it fails.  The Arduino framework does this for us
            esp_err_t ret = nvs_flash_init();
            if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                nvs_flash_erase();
                ret = nvs_flash_init();
            }
#endif
            esp_err_t err = nvs_open(name(), NVS_READWRITE, &_handle);
            if (err) {
                printf("nvs_open failed %x\n", err);
            }
        }
        return _handle;
    }

public:
    ESP_NVS(const char* name) : NVS(name) {}

    bool init() override {
        return handle() == 0;
    }

    bool get_str(const char* name, char* value, size_t* len) override {
        return nvs_get_str(handle(), name, value, len) || nvs_commit(handle());
    }

    bool set_str(const char* name, const char* value) override { return nvs_set_str(handle(), name, value) || nvs_commit(handle()); }

    bool get_i32(const char* name, int32_t* value) override { return nvs_get_i32(handle(), name, value) || nvs_commit(handle()); }

    bool set_i32(const char* name, int32_t value) override { return nvs_set_i32(handle(), name, value) || nvs_commit(handle()); }

    bool get_i8(const char* key, int8_t* out_value) override { return nvs_get_i8(handle(), key, out_value) || nvs_commit(handle()); }

    bool set_i8(const char* key, int8_t value) override { return nvs_set_i8(handle(), key, value) || nvs_commit(handle()); }

#if 0
    bool get_i16(const char* key, int16_t* out_value) override {
        return nvs_get_i16(handle(), key, out_value) || nvs_commit(handle());
    }
    bool set_i16(const char* key, int16_t value) override {
        return nvs_set_i16(handle(), key, value) || nvs_commit(handle());
    }
#endif

    bool get_blob(const char* key, void* out_value, size_t* length) override {
        return nvs_get_blob(handle(), key, out_value, length) || nvs_commit(handle());
    }

    bool set_blob(const char* key, const void* value, size_t length) override {
        return nvs_set_blob(handle(), key, value, length) || nvs_commit(handle());
    }

    bool erase_key(const char* key) override { return nvs_erase_key(handle(), key) || nvs_commit(handle()); }

    bool erase_all() override { return nvs_erase_all(handle()) || nvs_commit(handle()); }

    bool get_stats(size_t& used, size_t& free, size_t& total) override {
        nvs_stats_t stats;
        if (nvs_get_stats(NULL, &stats)) {
            return true;
        }
        used  = stats.used_entries;
        free  = stats.free_entries;
        total = stats.total_entries;
        return false;
    }

#if 0  // The SDK we use does not have this yet
nvs_iterator_t it = nvs_entry_find(NULL, NULL, NVS_TYPE_ANY);
while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    it = nvs_entry_next(it);
    log_info("namespace:"<<info.namespace_name<<" key:"<<info.key<<" type:"<< info.type);
}
#endif
};
ESP_NVS esp_nvs("FluidNC");
NVS&    nvs = esp_nvs;
