/*
    Enhanced VFS wrapper to allow POSIX FILE operations with directory support

    Based on: Copyright (c) 2024 Earle F. Philhower, III <earlephilhower@yahoo.com>
    Copyright (c) 2026 Mitch Bradley <wmb@firmworks.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

// Mods by Mitch Bradley to support directory ops

#include <Arduino.h>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <FSImpl.h>
#include <FS.h>

// Forward declarations for directory support
struct DIR;

// Global static to allow non-class POSIX calls to use this info
struct Entry {
    const char* path;  // Points to static string literal or pre-allocated buffer
    FS*         fs;
};

typedef struct {
    File dir;
    FS *fs;
    char *base_path;
    File current;
    bool valid;
} VFSDir;

// Use a simple array instead of std::list to avoid dynamic allocation issues
static const size_t MAX_MOUNTS = 4;
static Entry mount_table[MAX_MOUNTS] = {};
static size_t mount_count = 0;

static FS *root = nullptr;
static std::map<int, File> files;
static std::map<DIR *, VFSDir *> dirs;
static int fd = 3;
static int dir_id = 0;

class VFSClass {
public:
    VFSClass();
    void root(FS &fs);
    void map(const char *path, FS &fs);
};

VFSClass::VFSClass() {
}

void VFSClass::root(FS &fs) {
    ::root = &fs;
}

void VFSClass::map(const char *path, FS &fs) {
    // Use static mount table instead of dynamic allocation
    if (mount_count >= MAX_MOUNTS) {
        return;  // Mount table is full
    }
    mount_table[mount_count].path = path;  // Assume path is a static string literal
    mount_table[mount_count].fs = &fs;
    mount_count++;
}

extern VFSClass VFS;

static FS* pathToFS(const char** name) {
    const char* nm = *name;
    for (size_t i = 0; i < mount_count; i++) {
        if (!strncmp(mount_table[i].path, nm, strlen(mount_table[i].path))) {
            *name += strlen(mount_table[i].path);
            // If path is empty after stripping prefix, use root "/"
            if (**name == '\0') {
                *name = "/";
            }
            return mount_table[i].fs;
        }
    }
    return ::root;
}

extern "C" int _open(char* file, int flags, int mode) {
    (void)mode;  // No mode RWX here

    const char* nm = file;
    auto        fs = pathToFS(&nm);
    if (!fs) {
        return -1;
    }

    const char* md = "r";
    // Taken from table at https://pubs.opengroup.org/onlinepubs/9699919799/functions/fopen.html
    flags &= O_RDONLY | O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_RDWR;
    if (flags == O_RDONLY) {
        md = "r";
    } else if (flags == (O_WRONLY | O_CREAT | O_TRUNC)) {
        md = "w";
    } else if (flags == (O_WRONLY | O_CREAT | O_APPEND)) {
        md = "a";
    } else if (flags == O_RDWR) {
        md = "r+";
    } else if (flags == (O_RDWR | O_CREAT | O_TRUNC)) {
        md = "w+";
    } else if (flags == (O_RDWR | O_CREAT | O_APPEND)) {
        md = "a+";
    }
    File f = fs->open(nm, md);
    if (!f) {
        errno = EIO;
        return -1;
    }
    files.insert({ fd, f });
    return fd++;
}

extern "C" ssize_t _write(int fd, const void* buf, size_t count) {
#if defined DEBUG_RP2040_PORT
    if (fd < 3) {
        return DEBUG_RP2040_PORT.write((const char*)buf, count);
    }
#endif
    auto f = files.find(fd);
    if (f == files.end()) {
        //      return -1;
        return 0;
    }
    return f->second.write((const char*)buf, count);
}

extern "C" int _close(int fd) {
    auto f = files.find(fd);
    if (f == files.end()) {
        return -1;
    }
    f->second.close();
    files.erase(f);
    return 0;
}

extern "C" int _lseek(int fd, int ptr, int dir) {
    auto f = files.find(fd);
    if (f == files.end()) {
        return -1;
    }
    SeekMode d = SeekSet;
    if (dir == SEEK_CUR) {
        d = SeekCur;
    } else if (dir == SEEK_END) {
        d = SeekEnd;
    }
    // return f->second.seek(ptr, d) ? 0 : -1;
    return f->second.seek(ptr, d) ? 0 : 1;
}

extern "C" int _read(int fd, char* buf, int size) {
    auto f = files.find(fd);
    if (f == files.end()) {
        return -1;  // FD not found
    }
    return f->second.read((uint8_t*)buf, size);
}

extern "C" int _unlink(char* name) {
    auto f = pathToFS((const char**)&name);
    if (f) {
        return f->remove(name) ? 0 : -1;
    }
    return -1;
}

extern "C" int _stat(const char* name, struct stat* st) {
    auto f = pathToFS((const char**)&name);
    if (f) {
        fs::FSStat s;
        if (!f->stat(name, &s)) {
            return -1;
        }
        bzero(st, sizeof(*st));
        st->st_size        = s.size;
        st->st_blksize     = s.blocksize;
        st->st_ctim.tv_sec = s.ctime;
        st->st_atim.tv_sec = s.atime;
        st->st_mode        = s.isDir ? S_IFDIR : S_IFREG;
        return 0;
    }
    return -1;
}

extern "C" int _fstat(int fd, struct stat* st) {
    auto f = files.find(fd);
    if (f == files.end()) {
        return -1;  // FD not found
    }
    fs::FSStat s;
    if (!f->second.stat(&s)) {
        return -1;
    }
    bzero(st, sizeof(*st));
    st->st_size        = s.size;
    st->st_blksize     = s.blocksize;
    st->st_ctim.tv_sec = s.ctime;
    st->st_ctim.tv_sec = s.ctime;
    st->st_mode        = s.isDir ? S_IFDIR : S_IFREG;
    return 0;
}

extern "C" int _rmdir(const char* pathname) {
    if (!pathname) {
        return -1;
    }

    const char* nm = pathname;
    auto        fs = pathToFS(&nm);
    if (!fs) {
        return -1;
    }

    if (!fs->rmdir(nm)) {
        return -1;
    }
    return 0;
}

// Directory support for std::filesystem
// Define minimal dirent structures for compatibility
#ifndef DT_REG
#    define DT_REG 8
#    define DT_DIR 4
#endif

struct dirent {
    char          d_name[256];
    unsigned char d_type;
};

// POSIX mkdir() for directory creation - essential for std::filesystem::create_directory()
extern "C" int mkdir(const char* pathname, mode_t mode) {
    (void)mode;  // Mode is ignored for VFS-based filesystems

    if (!pathname) {
        errno = EINVAL;
        return -1;
    }

    const char* nm = pathname;
    auto        fs = pathToFS(&nm);
    if (!fs) {
        errno = ENODEV;
        return -1;
    }

    if (!fs->mkdir(nm)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

extern "C" DIR* opendir(const char* name) {
    if (!name) {
        errno = EINVAL;
        return nullptr;
    }

    const char* nm = name;
    auto        fs = pathToFS(&nm);
    if (!fs) {
        errno = ENODEV;
        return nullptr;
    }

    File dir = fs->open(nm, "r");
    if (!dir) {
        errno = ENOENT;
        return nullptr;
    }

    VFSDir* vfs_dir    = new VFSDir;
    vfs_dir->dir       = dir;
    vfs_dir->fs        = fs;
    vfs_dir->base_path = strdup(name);
    vfs_dir->valid     = true;

    DIR* dirp = (DIR*)(uintptr_t)(++dir_id);
    dirs.insert({ dirp, vfs_dir });
    return dirp;
}

extern "C" struct dirent* readdir(DIR* dirp) {
    auto it = dirs.find(dirp);
    if (it == dirs.end()) {
        errno = EBADF;
        return nullptr;
    }

    VFSDir* vfs_dir = it->second;
    if (!vfs_dir->valid) {
        return nullptr;
    }

    File f = vfs_dir->dir.openNextFile();
    if (!f) {
        vfs_dir->valid = false;
        return nullptr;
    }

    static struct dirent entry;
    bzero(&entry, sizeof(entry));
    strncpy(entry.d_name, f.name(), sizeof(entry.d_name) - 1);
    entry.d_type = f.isDirectory() ? DT_DIR : DT_REG;
    f.close();

    return &entry;
}

extern "C" int closedir(DIR* dirp) {
    auto it = dirs.find(dirp);
    if (it == dirs.end()) {
        errno = EBADF;
        return -1;
    }

    VFSDir* vfs_dir = it->second;
    vfs_dir->dir.close();
    free(vfs_dir->base_path);
    delete vfs_dir;
    dirs.erase(it);
    return 0;
}

extern "C" void rewinddir(DIR* dirp) {
    auto it = dirs.find(dirp);
    if (it == dirs.end()) {
        return;
    }

    VFSDir* vfs_dir = it->second;
    vfs_dir->dir.rewindDirectory();
    vfs_dir->valid = true;
}

extern "C" int statvfs(const char* name, statvfs_t* buf) {
    auto   fs = pathToFS(&name);
    FSInfo info;
    if (!fs->info(info)) {
        return -1;
    }

    buf->f_frsize = 1;
    buf->f_blocks = info.totalBytes;
    buf->f_bfree  = info.totalBytes - info.usedBytes;

    buf->f_bavail = buf->f_bfree;
    return 0;
}

VFSClass VFS;

// Force this module to be linked by making it a required symbol
extern "C" {
void __attribute__((used)) __link_vfs(void) {
    // No-op, just ensures VFS is linked
}
}
