#include "nvs.h"

esp_err_t nvs_open(const char* name, nvs_open_mode open_mode, nvs_handle* out_handle) {
    *out_handle = &(NvsEmulator::instance());
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle handle, const char* key, int32_t* out_value) {
    if (handle->tryGetI32(key, *out_value)) {
        return ESP_OK;
    } else {
        return ESP_ERR_NVS_NOT_FOUND;
    }
}
esp_err_t nvs_get_i8(nvs_handle handle, const char* key, int8_t* out_value) {
    if (handle->tryGetI8(key, *out_value)) {
        return ESP_OK;
    } else {
        return ESP_ERR_NVS_NOT_FOUND;
    }
}
esp_err_t nvs_get_str(nvs_handle handle, const char* key, char* out_value, size_t* length) {
    if (handle->tryGetStr(key, out_value, *length)) {
        return ESP_OK;
    } else {
        return ESP_ERR_NVS_NOT_FOUND;
    }
}
esp_err_t nvs_get_blob(nvs_handle handle, const char* key, void* out_value, size_t* length) {
    if (handle->tryGetBlob(key, out_value, *length)) {
        return ESP_OK;
    } else {
        return ESP_ERR_NVS_NOT_FOUND;
    }
}
esp_err_t nvs_erase_key(nvs_handle handle, const char* key) {
    handle->erase(key);
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle handle) {
    handle->clear();
    return ESP_OK;
}
esp_err_t nvs_set_i8(nvs_handle handle, const char* key, int8_t value) {
    char*       v = reinterpret_cast<char*>(&value);
    std::string data(v, v + 1);
    handle->set(key, data);
    return ESP_OK;
}
esp_err_t nvs_set_i32(nvs_handle handle, const char* key, int32_t value) {
    char*       v = reinterpret_cast<char*>(&value);
    std::string data(v, v + 4);
    handle->set(key, data);
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle handle, const char* key, const char* value) {
    handle->set(key, value);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle handle, const char* key, const void* value, size_t length) {
    auto        c = static_cast<const char*>(value);
    std::string data(c, c + length);
    handle->set(key, data);
    return ESP_OK;
}
