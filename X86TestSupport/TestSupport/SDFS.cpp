#include "SD.h"

namespace fs {

    SDFS::SDFS(FSImplPtr impl) : fs::FS(impl) {}
    bool SDFS::begin(uint8_t ssPin, SPIClass& spi, uint32_t frequency, const char* mountpoint, uint8_t max_files, bool format_if_empty) {
        return true;
    }
    void     SDFS::end() {}
    uint64_t SDFS::cardSize() { return 0; }
    uint64_t SDFS::totalBytes() { return 0; }
    uint64_t SDFS::usedBytes() { return 0; }
    bool     SDFS::readRAW(uint8_t* buffer, uint32_t sector) { return false; }
    bool     SDFS::writeRAW(uint8_t* buffer, uint32_t sector) { return false; }

    FSImplPtr inst;
}

fs::SDFS SD(fs::inst);
