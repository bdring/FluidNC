// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 Non-Volatile Storage (NVS) implementation
// Simple key-value store using a dedicated flash sector

#include "Driver/NVS.h"
// #include "hardware/flash.h"
// #include "pico/stdlib.h"
#include <cstring>
#include <cstdio>
#include <Arduino.h>
#include <EEPROM.h>

class EEPROM_NVS : public NVS {
private:
    // Record types
    enum RecordType : uint8_t {
        RType_Full   = 0x00,
        RType_I8     = 0x01,
        RType_I32    = 0x02,
        RType_String = 0x03,
        RType_Blob   = 0x04,
        RType_Error  = 0xFE,
        RType_Empty  = 0xFF,
    };

    // Record header format:
    // [type:1][key_len:1][value_len:2][key:key_len][value:value_len]
    struct RecordHeader {
        RecordType  type;
        uint8_t  key_len;
        uint16_t value_len;
        uint8_t  key[0];
    } __attribute__((packed));

    RecordHeader errorRecord { RType_Error, 0, 0 };
    RecordHeader fullRecord { RType_Full, 0, 0 };

    RecordHeader* _start;
    RecordHeader* _end;

// Minimum record size is header + at least 1 byte key
#define MIN_RECORD_SIZE (sizeof(RecordHeader) + 1)

    // Check if the a byte is erased (0xFF) at the start of a record
    inline bool is_erased(const RecordHeader* record) { return record->type == RType_Empty; }

    inline size_t recordSize(const RecordHeader* record) { return sizeof(RecordHeader) + record->key_len + record->value_len; }

    // Find the current write position (end of valid records) in the buffer
    RecordHeader* find_erased(RecordHeader* record) {
        while (record < _end) {
            if (is_erased(record)) {
                return record;
            }
            record = (RecordHeader*)((uint8_t*)record + recordSize(record));
        }

        return _end;
    }

    uint8_t* toKey(RecordHeader* record) { return record->key; }
    uint8_t* toValue(RecordHeader* record) { return toKey(record) + record->key_len; }

    RecordHeader* nextRecord(RecordHeader* record) { return (RecordHeader*)((uint8_t*)record + recordSize(record)); }

    // Find a record by key in the buffer
    // Returns pointer to record value data
    RecordHeader* find_record(const char* key) {
        RecordHeader* record  = _start;
        size_t        key_len = strlen(key);

        while (record < _end) {
            if (is_erased(record)) {
                return record;
            }

            if (record->key_len == 0) {
                return &errorRecord;
            }

            auto next = nextRecord(record);
            if (next > _end) {
                return &errorRecord;
            }

            if (record->key_len == key_len && strncmp((char*)toKey(record), key, key_len) == 0) {
                return record;
            }

            record = next;
        }

        return &fullRecord;
    }

    // Detect if NVS sector is corrupted
    bool detect_corruption() {
        auto record = _start;

        while (record < _end) {
            if (is_erased(record)) {
                return false;
            }

            if (record->key_len == 0) {
                return true;
            }

            auto next = nextRecord(record);
            if (recordSize(record) < MIN_RECORD_SIZE || next > _end) {
                return true;
            }

            record = next;
        }

        return false;
    }

    void delete_record(RecordHeader* record) {
        size_t record_size = recordSize(record);

        auto next = nextRecord(record);

        size_t to_copy = (uint8_t*)_end - (uint8_t*)next;
        memmove(record, next, to_copy);
        uint8_t* residue = (uint8_t*)_end - record_size;
        memset(residue, 0xFF, record_size);
    }

    // Write a record - updates buffer and commits affected pages
    bool write_record(const char* key, RecordType type, const void* value, size_t value_len) {
        auto record = find_record(key);
        if (record->type == type && value_len == record->value_len) {
            // If there is already a record with the same key, type, and value len,
            // we can overwrite it in-place
            if (!memcmp(toValue(record), value, value_len)) {
                // But if the value is the same, there is no need to overwrite
                return false;
            }
            memcpy(toValue(record), value, value_len);
            auto res = commit();
            return res;
        }
        if (record->type == RType_Error || record->type == RType_Full) {
            return true;
        }

        if (record->type != RType_Empty) {
            // There is an existing record with this key, but it is either the
            // wrong type or the wrong length, so we delete the old record
            delete_record(record);
            record = find_erased(record);
        }

        size_t key_len = strlen(key);
        if (key_len > UINT8_MAX || value_len > UINT16_MAX) {
            return true;
        }
        size_t record_size = sizeof(RecordHeader) + key_len + value_len;

        // Now record points past the last record
        if ((uint8_t*)record + record_size > (uint8_t*)_end) {
            // New record won't fit
            // If there was an old record with the same key, it was deleted
            // and the new record will not be written.  This is unlikely
            // since records of a given key tend to have the same length
            return true;
        }

        // Build the record in the buffer
        record->type      = type;
        record->key_len   = key_len;
        record->value_len = value_len;

        uint8_t* keyp   = toKey(record);
        uint8_t* valuep = toValue(record);

        memcpy(keyp, key, key_len);
        memcpy(valuep, value, value_len);

        auto res = commit();
        return res;
    }

    bool commit() {
        // getDataPtr() sets _dirty
        // The EEPROM class has no direct way to set it
        EEPROM.getDataPtr();
        return !EEPROM.commit();
    }

public:
    EEPROM_NVS(const char* name) : NVS(name) {}

    bool init() override {
        // Load the NVS sector from flash into the RAM buffer
        EEPROM.begin(4096);

        auto data = EEPROM.getDataPtr();
        _start    = reinterpret_cast<RecordHeader*>(data);
        _end      = reinterpret_cast<RecordHeader*>(data + EEPROM.length());

        // Check for corruption and auto-recover if needed
        if (detect_corruption()) {
            return erase_all();
        }

        return false;
    }

    bool get_str(const char* key, char* out_value, size_t* out_len) override {
        auto record = find_record(key);

        if (record->type != RType_String) {
            return true;  // Key not found or wrong type
        }

        if (!out_len) {
            return true;
        }

        if (out_value) {
            // Check if buffer has room for the value
            size_t copy_len = ::min(*out_len, record->value_len);
            if (copy_len > 0) {
                memcpy(out_value, toValue(record), copy_len);
            }
        }
        *out_len = record->value_len;

        return false;  // Success
    }

    bool set_str(const char* key, const char* value) override {
        if (!key || !value) {
            return true;
        }
        size_t value_len = strlen(value) + 1;  // Includes NULL terminator
        return write_record(key, RType_String, value, value_len);
    }

    bool get_i32(const char* key, int32_t* out_value) override {
        auto record = find_record(key);

        if (record->type != RType_I32 || record->value_len != sizeof(int32_t)) {
            return true;  // Key not found or wrong type
        }

        if (out_value) {
            memcpy(out_value, toValue(record), sizeof(int32_t));
        }

        return false;  // Success
    }

    bool set_i32(const char* key, int32_t value) override {
        if (!key) {
            return true;
        }
        return write_record(key, RType_I32, &value, sizeof(int32_t));
    }

    bool get_i8(const char* key, int8_t* out_value) override {
        auto record = find_record(key);

        if (record->type != RType_I8 || record->value_len != sizeof(int8_t)) {
            return true;  // Key not found or wrong type
        }

        if (out_value) {
            *out_value = *((int8_t*)toValue(record));
        }

        return false;  // Success
    }

    bool set_i8(const char* key, int8_t value) override {
        if (!key) {
            return true;
        }
        return write_record(key, RType_I8, &value, sizeof(int8_t));
    }

    bool get_blob(const char* key, void* out_value, size_t* out_len) override {
        auto record = find_record(key);

        if (record->type != RType_Blob) {
            return true;  // Key not found or wrong type
        }
        if (out_len) {
            if (out_value) {
                size_t copy_len = ::min(*out_len, record->value_len);
                memcpy(out_value, toValue(record), copy_len);
            }
            *out_len = record->value_len;
        }
        return false;  // Success
    }

    bool set_blob(const char* key, const void* value, size_t value_len) override {
        if (!key || !value) {
            return true;
        }
        return write_record(key, RType_Blob, value, value_len);
    }

    bool erase_key(const char* key) override {
        if (!key) {
            return true;
        }

        auto record = find_record(key);
        if (record->type == RType_Empty || record->type == RType_Error || record->type == RType_Full) {
            return true;
        }

        delete_record(record);

        return commit();
    }

    bool erase_all() override {
        memset(_start, 0xFF, (uint8_t*)_end - (uint8_t*)_start);
        return commit();
    }

    bool get_stats(size_t& used, size_t& free, size_t& total) override {
        auto   record     = (RecordHeader*)_start;
        size_t used_bytes = 0;

        while (record < _end) {
            if (is_erased(record)) {
                break;
            }

            auto next = nextRecord(record);
            if (next > _end) {
                break;
            }

            used_bytes += recordSize(record);
            record = next;
        }

        total = (uint8_t*)_end - (uint8_t*)_start;
        used  = used_bytes;
        free  = total - used;

        return false;  // Success
    }
};

EEPROM_NVS eeprom_nvs("FluidNC");
NVS&       nvs = eeprom_nvs;
