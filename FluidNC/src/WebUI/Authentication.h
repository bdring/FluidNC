#pragma once

#include <cstdint>

//Authentication level
enum class AuthenticationLevel : uint8_t { LEVEL_GUEST = 0, LEVEL_USER = 1, LEVEL_ADMIN = 2 };

#ifdef ENABLE_AUTHENTICATION
static const int MIN_LOCAL_PASSWORD_LENGTH = 1;
static const int MAX_LOCAL_PASSWORD_LENGTH = 16;

class StringSetting;

class AuthPasswordSetting : public StringSetting {
public:
    AuthPasswordSetting(const char* description, const char* name, const char* defVal) :
        StringSetting(description, WEBSET, WA, NULL, name, defVal, MIN_LOCAL_PASSWORD_LENGTH, MAX_LOCAL_PASSWORD_LENGTH) {}

    const char* getDefaultString() { return "********"; }
    const char* getStringValue() { return "********"; }
    Error       setStringValue(std::string_view s) {
              for (auto const& c : s) {  //no space allowed
            if (c == ' ') {
                      return Error::InvalidValue;
            }
        }
              return StringSetting::setStringValue(s);
    }
};

extern AuthPasswordSetting* user_password;
extern AuthPasswordSetting* admin_password;

#endif
