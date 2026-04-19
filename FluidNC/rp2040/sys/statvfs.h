#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct statvfs {
    // Include only fields that matter to std::filesystem do_space()
    // unsigned long f_bsize;
    unsigned long f_frsize;
    fsblkcnt_t    f_blocks;
    fsblkcnt_t    f_bfree;
    fsblkcnt_t    f_bavail;
    // fsfilcnt_t    f_files;
    // fsfilcnt_t    f_ffree;
    // fsfilcnt_t    f_favail;
    // unsigned long f_fsid;
    // unsigned long f_flag;
    // unsigned long f_namemax;
} statvfs_t;

enum FFlag {
    ST_RDONLY = 1, /* Mount read-only.  */
    ST_NOSUID = 2
};

int statvfs(const char* file, statvfs_t* buf);
int fstatvfs(int fildes, statvfs_t* buf);

#ifdef __cplusplus
}
#endif
