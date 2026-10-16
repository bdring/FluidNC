// Tests for the filesystem probe used by the WebDAV handlers: a missing path
// must come out as NONE (a normal answer), a path error as NONE (permanent,
// not worth a retry), and a volume that does not answer as UNAVAILABLE.
#include <gtest/gtest.h>

#include "WebUI/FsProbe.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

// A scratch directory holding one file and one subdirectory, removed afterwards.
class FsProbeTest : public ::testing::Test {
protected:
    fs::path root;
    fs::path file;
    fs::path dir;

    void SetUp() override {
        root = fs::temp_directory_path() / "fluidnc_fsprobe_test";
        fs::remove_all(root);
        fs::create_directories(root);
        file = root / "file.txt";
        dir  = root / "dir";
        std::ofstream(file.string()) << "hello";
        fs::create_directory(dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // What the handlers do: status() with an error_code, then classify.
    FsProbe probe(const fs::path& p, std::error_code& ec) {
        auto st = fs::status(p, ec);
        return fs_probe(st, ec);
    }
};

// --- Real filesystem: what std::filesystem::status() actually reports ---

TEST_F(FsProbeTest, RegularFileIsFile) {
    std::error_code ec;
    EXPECT_EQ(probe(file, ec), FsProbe::FILE);
    EXPECT_FALSE(ec);
}

TEST_F(FsProbeTest, DirectoryIsDir) {
    std::error_code ec;
    EXPECT_EQ(probe(dir, ec), FsProbe::DIR);
    EXPECT_FALSE(ec);
}

// ENOENT: the usual missing file.  status() reports it as not_found and
// still sets ec to the errno; the probe must go by the type, not by ec.
TEST_F(FsProbeTest, MissingPathIsNone) {
    std::error_code ec;
    EXPECT_EQ(probe(root / "absent.txt", ec), FsProbe::NONE);
    EXPECT_TRUE(ec == std::errc::no_such_file_or_directory) << ec.message();
}

// ENOTDIR (ENOENT on Windows): a path that goes through a regular file.
TEST_F(FsProbeTest, PathThroughFileIsNone) {
    std::error_code ec;
    EXPECT_EQ(probe(file / "child", ec), FsProbe::NONE);
    EXPECT_TRUE(ec == std::errc::not_a_directory || ec == std::errc::no_such_file_or_directory) << ec.message();
}

// --- Synthetic errors: what the volume reports when it does not answer ---

fs::file_status none_status() {
    return fs::file_status { fs::file_type::none };
}

TEST(FsProbe, VolumeErrorsAreUnavailable) {
    EXPECT_EQ(fs_probe(none_status(), std::make_error_code(std::errc::io_error)), FsProbe::UNAVAILABLE);
    EXPECT_EQ(fs_probe(none_status(), std::make_error_code(std::errc::no_such_device)), FsProbe::UNAVAILABLE);
    EXPECT_EQ(fs_probe(none_status(), std::make_error_code(std::errc::timed_out)), FsProbe::UNAVAILABLE);
}

// libstdc++ builds its filesystem error codes from errno with the generic
// category, which is what the ESP32 build uses as well.
TEST(FsProbe, ErrnoValuesInGenericCategoryAreUnavailable) {
    EXPECT_EQ(fs_probe(none_status(), std::error_code(EIO, std::generic_category())), FsProbe::UNAVAILABLE);
    EXPECT_EQ(fs_probe(none_status(), std::error_code(ENODEV, std::generic_category())), FsProbe::UNAVAILABLE);
    EXPECT_EQ(fs_probe(none_status(), std::error_code(ETIMEDOUT, std::generic_category())), FsProbe::UNAVAILABLE);
}

TEST(FsProbe, PathErrorsAreNone) {
    EXPECT_EQ(fs_probe(none_status(), std::make_error_code(std::errc::permission_denied)), FsProbe::NONE);
    EXPECT_EQ(fs_probe(none_status(), std::make_error_code(std::errc::filename_too_long)), FsProbe::NONE);
    EXPECT_EQ(fs_probe(none_status(), std::make_error_code(std::errc::invalid_argument)), FsProbe::NONE);
}

TEST(FsProbe, NotFoundWinsOverTheErrorCode) {
    // status() sets ec alongside not_found; the type decides, never the code.
    fs::file_status st { fs::file_type::not_found };
    EXPECT_EQ(fs_probe(st, std::make_error_code(std::errc::no_such_file_or_directory)), FsProbe::NONE);
    EXPECT_EQ(fs_probe(st, std::make_error_code(std::errc::not_a_directory)), FsProbe::NONE);
    EXPECT_EQ(fs_probe(st, std::make_error_code(std::errc::io_error)), FsProbe::NONE);
}

TEST(FsProbe, ExistsButNeitherFileNorDirIsNone) {
    EXPECT_EQ(fs_probe(fs::file_status { fs::file_type::character }, std::error_code {}), FsProbe::NONE);
    EXPECT_EQ(fs_probe(fs::file_status { fs::file_type::socket }, std::error_code {}), FsProbe::NONE);
}

TEST(FsProbe, VolumeUnavailablePredicate) {
    EXPECT_TRUE(fs_volume_unavailable(std::make_error_code(std::errc::io_error)));
    EXPECT_FALSE(fs_volume_unavailable(std::make_error_code(std::errc::no_such_file_or_directory)));
    EXPECT_FALSE(fs_volume_unavailable(std::error_code {}));
}
