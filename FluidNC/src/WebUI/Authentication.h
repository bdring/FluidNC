#pragma once

#include <cstdint>

namespace WebUI {
    //Authentication level
    enum class AuthenticationLevel : uint8_t { LEVEL_GUEST = 0, LEVEL_USER = 1, LEVEL_ADMIN = 2 };

    static const int MIN_LOCAL_PASSWORD_LENGTH = 1;
    static const int MAX_LOCAL_PASSWORD_LENGTH = 16;

    void remove_password(char* str, AuthenticationLevel& auth_level);
}
