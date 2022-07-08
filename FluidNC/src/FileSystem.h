#pragma once

// This FileSystem abstraction works around some problems in the Arduino
// framework's FS class.  In particular, FS does not abstract the totalBytes()
// and usedBytes() methods, leaving them to be defined in SD and SPIFFS
// derived classes with different return types.
// The FS class in the similar framework for Espressif8266 fixes that, but
// it is unclear when if ever it will be fixed for Espressif32.

// FileSystem also adds a method to create directory listings in JSON format.

#include <cstdint>

#include "WebUI/JSONEncoder.h"
#include "Uart.h"

#include "FS.h"

#include <dirent.h>

using namespace fs;
class FileSystem {
public:
    struct FsInfo {
        String name;
        int    fsindex;
        bool   hasSubdirs;
        FS&    theFS;
        void (*openfn)(void);
        void (*closefn)(void);
    };

private:
    int  _fsindex;
    bool _hasSubdirs = false;
    bool deleteRecursive(const String& path);

    String _fspath;

    const FsInfo* _realFs;

    static void openSD();
    static void closeSD();

public:
    FileSystem(const String& path, const FsInfo& fs);
    ~FileSystem();

    FS& theFS();

    const String path() { return _fspath; }

    String joinFile(const String& path, const String& filename);
    void   joinPath(const String& path, const FsInfo& fs);

    uint64_t totalBytes();
    uint64_t usedBytes();

    bool format();

    //    bool rename(const char* pathFrom, const char* pathTo) { return theFS().rename(pathFrom, pathTo); }
    //    bool rename(const String& pathFrom, const String& pathTo) { return theFS().rename(pathFrom, pathTo); }

    void listDirJSON(const String& path, size_t levels, WebUI::JSONencoder& j);
    void listJSON(const String& status, Print& out);

    void listDir(const String& path, String indent, size_t levels, Print& out);
    void list(Print& out);

    bool mkdir();
    bool mkdir(const String& filename);

    bool deleteFile();
    bool deleteFile(const String& filename);

    bool deleteDir();
    bool deleteDir(const String& filename);

    static FsInfo            filesystems[3];
    static constexpr FsInfo& sd       = filesystems[0];
    static constexpr FsInfo& spiffs   = filesystems[1];
    static constexpr FsInfo& littlefs = filesystems[2];
    static constexpr FsInfo& localfs  = filesystems[1];
};
