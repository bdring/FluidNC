#pragma once

#include <unordered_map>
#include <string>
#include "esp_err.h"

class NvsEmulator {
    // NVS is basically a key-value store.

public:
    std::unordered_map<std::string, std::string> data;

    bool tryGetI32(const char* str, int32_t& value) {
        auto it = data.find(str);
        if (it != data.end() && it->second.size() == 4) {
            value = *reinterpret_cast<const int32_t*>(it->second.data());
            return true;
        } else {
            return false;
        }
    }

    bool tryGetI8(const char* str, int8_t& value) {
        auto it = data.find(str);
        if (it != data.end() && it->second.size() == 1) {
            value = *reinterpret_cast<const int8_t*>(it->second.data());
            return true;
        } else {
            return false;
        }
    }

    bool tryGetStr(const char* str, char* buf, size_t& len) {
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
        } else {
            return false;
        }
    }

    bool tryGetBlob(const char* str, void* buf, size_t& len) {
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
        } else {
            return false;
        }
    }

    void set(const char* str, std::string value) { data[str] = value; }

    void erase(const char* str) {
        auto it = data.find(str);
        if (it != data.end()) {
            data.erase(it);
        }
    }

    void clear() { data.clear(); }

    static NvsEmulator& instance() {
        static NvsEmulator instance;
        return instance;
    }
};

struct nvs_stats_t {
    int used_entries;
    int free_entries;
    int total_entries;
};

using nvs_handle = NvsEmulator*;

inline esp_err_t nvs_get_stats(const char* part_name, nvs_stats_t* stats) {
    auto& inst           = NvsEmulator::instance();
    stats->used_entries  = inst.data.size();
    stats->free_entries  = 1000 - inst.data.size();
    stats->total_entries = 1024;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle handle);

enum nvs_open_mode { NVS_READWRITE };

#define ESP_ERR_NVS_BASE 0x1100                               /*!< Starting number of error codes */
#define ESP_ERR_NVS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x01) /*!< The storage driver is not initialized */
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)       /*!< Id namespace doesn’t exist yet and mode is NVS_READONLY */
#define ESP_ERR_NVS_TYPE_MISMATCH                                                                                                          \
    (ESP_ERR_NVS_BASE + 0x03) /*!< The type of set or get operation doesn't match the type of value stored in NVS */
#define ESP_ERR_NVS_READ_ONLY (ESP_ERR_NVS_BASE + 0x04)        /*!< Storage handle was opened as read only */
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE (ESP_ERR_NVS_BASE + 0x05) /*!< There is not enough space in the underlying storage to save the value */
#define ESP_ERR_NVS_INVALID_NAME (ESP_ERR_NVS_BASE + 0x06)     /*!< Namespace name doesn’t satisfy constraints */
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)   /*!< Handle has been closed or is NULL */
#define ESP_ERR_NVS_REMOVE_FAILED                                                                                                          \
    (ESP_ERR_NVS_BASE +                                                                                                                    \
     0x08) /*!< The value wasn’t updated because flash write operation has failed. The value was written however, and update will be finished after re-initialization of nvs, provided that flash operation doesn’t fail again. */
#define ESP_ERR_NVS_KEY_TOO_LONG (ESP_ERR_NVS_BASE + 0x09) /*!< Key name is too long */
#define ESP_ERR_NVS_PAGE_FULL (ESP_ERR_NVS_BASE + 0x0a)    /*!< Internal error; never returned by nvs API functions */
#define ESP_ERR_NVS_INVALID_STATE                                                                                                          \
    (ESP_ERR_NVS_BASE +                                                                                                                    \
     0x0b) /*!< NVS is in an inconsistent state due to a previous error. Call nvs_flash_init and nvs_open again, then retry. */
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c) /*!< String or blob length is not sufficient to store data */
#define ESP_ERR_NVS_NO_FREE_PAGES                                                                                                          \
    (ESP_ERR_NVS_BASE +                                                                                                                    \
     0x0d) /*!< NVS partition doesn't contain any empty pages. This may happen if NVS partition was truncated. Erase the whole partition and call nvs_flash_init again. */
#define ESP_ERR_NVS_VALUE_TOO_LONG (ESP_ERR_NVS_BASE + 0x0e) /*!< String or blob length is longer than supported by the implementation */
#define ESP_ERR_NVS_PART_NOT_FOUND (ESP_ERR_NVS_BASE + 0x0f) /*!< Partition with specified name is not found in the partition table */

#define ESP_ERR_NVS_NEW_VERSION_FOUND                                                                                                      \
    (ESP_ERR_NVS_BASE + 0x10) /*!< NVS partition contains data in new format and cannot be recognized by this version of code */
#define ESP_ERR_NVS_XTS_ENCR_FAILED (ESP_ERR_NVS_BASE + 0x11)      /*!< XTS encryption failed while writing NVS entry */
#define ESP_ERR_NVS_XTS_DECR_FAILED (ESP_ERR_NVS_BASE + 0x12)      /*!< XTS decryption failed while reading NVS entry */
#define ESP_ERR_NVS_XTS_CFG_FAILED (ESP_ERR_NVS_BASE + 0x13)       /*!< XTS configuration setting failed */
#define ESP_ERR_NVS_XTS_CFG_NOT_FOUND (ESP_ERR_NVS_BASE + 0x14)    /*!< XTS configuration not found */
#define ESP_ERR_NVS_ENCR_NOT_SUPPORTED (ESP_ERR_NVS_BASE + 0x15)   /*!< NVS encryption is not supported in this version */
#define ESP_ERR_NVS_KEYS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x16) /*!< NVS key partition is uninitialized */
#define ESP_ERR_NVS_CORRUPT_KEY_PART (ESP_ERR_NVS_BASE + 0x17)     /*!< NVS key partition is corrupt */
#define ESP_ERR_NVS_WRONG_ENCRYPTION                                                                                                       \
    (ESP_ERR_NVS_BASE +                                                                                                                    \
     0x19) /*!< NVS partition is marked as encrypted with generic flash encryption. This is forbidden since the NVS encryption works differently. */

#define ESP_ERR_NVS_CONTENT_DIFFERS                                                                                                        \
    (ESP_ERR_NVS_BASE + 0x18) /*!< Internal error; never returned by nvs API functions.  NVS key is different in comparison */

#define NVS_DEFAULT_PART_NAME "nvs" /*!< Default partition name of the NVS partition in the partition table */

#define NVS_PART_NAME_MAX_SIZE 16 /*!< maximum length of partition name (excluding null terminator) */
#define NVS_KEY_NAME_MAX_SIZE 16  /*!< Maximal length of NVS key name (including null terminator) */

esp_err_t nvs_open(const char* name, nvs_open_mode open_mode, nvs_handle* out_handle);
esp_err_t nvs_get_i32(nvs_handle handle, const char* key, int32_t* out_value);
esp_err_t nvs_get_i8(nvs_handle handle, const char* key, int8_t* out_value);
esp_err_t nvs_get_str(nvs_handle handle, const char* key, char* out_value, size_t* length);
esp_err_t nvs_get_blob(nvs_handle handle, const char* key, void* out_value, size_t* length);
esp_err_t nvs_erase_key(nvs_handle handle, const char* key);
esp_err_t nvs_erase_all(nvs_handle handle);

esp_err_t nvs_set_i8(nvs_handle handle, const char* key, int8_t value);
esp_err_t nvs_set_i32(nvs_handle handle, const char* key, int32_t value);
esp_err_t nvs_set_str(nvs_handle handle, const char* key, const char* value);
esp_err_t nvs_set_blob(nvs_handle handle, const char* key, const void* value, size_t length);
