#pragma once

// Custom dirent.h for RP2040 - ensures VFS implementations are used
// Must be included before system dirent.h

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    char d_name[256];
    unsigned char d_type;
};

typedef void DIR;

DIR*           opendir(const char* name);
struct dirent* readdir(DIR* dirp);
int            closedir(DIR* dirp);
void           rewinddir(DIR* dirp);

// Ensure these are used instead of system stubs
#ifdef __cplusplus
}
#endif
