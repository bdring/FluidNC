#include "SPIFFS.h"

namespace fs {

    SPIFFSFS::SPIFFSFS() : fs::FS(nullptr), partitionLabel_("spiffs") {}
    SPIFFSFS::~SPIFFSFS() {}
    bool SPIFFSFS::begin(bool        formatOnFail /*   = false*/,
                         const char* basePath /*= "/spiffs"*/,
                         uint8_t     maxOpenFiles /*= 10*/,
                         const char* partitionLabel /*= NULL*/) {
        return true;
    }
    bool   SPIFFSFS::format() { return true; }
    size_t SPIFFSFS::totalBytes() { return 1024 * 1024; /* 1 MB */ }
    size_t SPIFFSFS::usedBytes() { return 0; }
    void   SPIFFSFS::end() {}
}

fs::SPIFFSFS SPIFFS;
