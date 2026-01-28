#include <gtest/gtest.h>

#include <cctype>
#include <cstring>

#include "Settings.h"
#include "Configuration/Configurable.h"

uint32_t num_initial_matches(const char* key, uint32_t keylen, uint32_t matchnum, char* matchname);

// Stub config tree root used by production num_initial_matches() when key starts with '/'.
Configuration::Configurable* config = nullptr;

// PlatformIO native test build intentionally compiles a reduced subset of FluidNC.
// Provide minimal stubs needed for linking Settings.h types used by production completion.
std::vector<Setting*> Setting::List = {};
std::vector<Command*> Command::List = {};

Word::Word(type_t type, permissions_t permissions, const char* description, const char* grblName, const char* fullName) :
    _description(description),
    _grblName(grblName),
    _fullName(fullName),
    _type(type),
    _permissions(permissions) {}

Command::Command(const char*   description,
                 type_t        type,
                 permissions_t permissions,
                 const char*   grblName,
                 const char*   fullName,
                 bool (*cmdChecker)(),
                 bool synchronous) :
    Word(type, permissions, description, grblName, fullName),
    _cmdChecker(cmdChecker),
    _synchronous(synchronous) {
    List.insert(List.begin(), this);
}

Setting::Setting(const char* description, type_t type, permissions_t permissions, const char* grblName, const char* fullName) :
    Word(type, permissions, description, grblName, fullName),
    _keyName(fullName) {
    List.insert(List.begin(), this);
}

namespace {
struct MinimalSetting final : public Setting {
    explicit MinimalSetting(const char* name) : Setting(nullptr, GRBL, WG, nullptr, name) {}

    void load() override {}
    void setDefault() override {}

    Error setStringValue(std::string_view) override { return Error::ReadOnlySetting; }
    const char* getStringValue() override { return ""; }
    const char* getDefaultString() override { return ""; }
};

static bool starts_with_ci(const char* s, const char* prefix) {
    if (!s || !prefix) {
        return false;
    }
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (s[i] == '\0') {
            return false;
        }
        unsigned char a = static_cast<unsigned char>(s[i]);
        unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

static bool equals_ci(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
        if (ca == '\0' && cb == '\0') {
            return true;
        }
    }
}
}  // namespace

TEST(CommandCompletion, SdSlashReturnsAtLeastOneMatch) {
    static MinimalSetting s("sd/list");

    char out[128] = { 0 };
    uint32_t nfound = num_initial_matches("sd/", 3, 0, out);
    EXPECT_GE(nfound, 1u);
}

TEST(CommandCompletion, SdLReturnsPrefixMatch) {
    static MinimalSetting s("sd/list");

    char out[128] = { 0 };
    uint32_t nfound = num_initial_matches("sd/l", 4, 0, out);

    EXPECT_GE(nfound, 1u);
    EXPECT_TRUE(starts_with_ci(out, "sd/l"));
}

TEST(CommandCompletion, CaseInsensitiveMatches) {
    static MinimalSetting s("sd/list");

    char out1[128] = { 0 };
    char out2[128] = { 0 };

    uint32_t n1 = num_initial_matches("sd/l", 4, 0, out1);
    uint32_t n2 = num_initial_matches("SD/L", 4, 0, out2);

    EXPECT_EQ(n1, n2);
    EXPECT_TRUE(starts_with_ci(out1, "sd/l"));
    EXPECT_TRUE(starts_with_ci(out2, "sd/l"));
}

TEST(CommandCompletion, ExactMatchIsIncluded) {
    static MinimalSetting s("sd/list");

    const char* key = "sd/list";
    uint32_t nfound = num_initial_matches(key, 7, 0, nullptr);
    EXPECT_GE(nfound, 1u);

    bool foundExact = false;
    for (uint32_t i = 0; i < nfound; ++i) {
        char out[128] = { 0 };
        (void)num_initial_matches(key, 7, i, out);
        if (equals_ci(out, "sd/list")) {
            foundExact = true;
            break;
        }
    }
    EXPECT_TRUE(foundExact);
}
