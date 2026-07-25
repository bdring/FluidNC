* filesystem/ directory

This directory supports the code in the stdfs17 folder, which includes
../filesystem/ops-common.h and ../filesystem/dir-common.h .
This mimics the directory layout of the gcc / libstdc++-v3 tree
at github.com/gcc-mirror/gcc, which has the following files relevant
to std::filesystem:

libstc++-v3/
    src/filesystem/
        dir-common.h
        ops-common.h
        dir.cc
        ops.cc
        path.cc
    src/c++17/
        fs_dir.cc
        fs_ops.cc
        fs_path.cc
    include/bits/
        fs_dir.h
        fs_fwd.h
        fs_ops.h
        fs_path.h
