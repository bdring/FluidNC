#pragma once

#include <cstdint>

//Authentication level
enum class AuthenticationLevel : uint8_t { LEVEL_GUEST = 0, LEVEL_USER = 1, LEVEL_ADMIN = 2 };

static const int MIN_LOCAL_PASSWORD_LENGTH = 1;
static const int MAX_LOCAL_PASSWORD_LENGTH = 16;

#ifdef ENABLE_AUTHENTICATION
void remove_password(char* str, AuthenticationLevel& auth_level);
#else
inline void remove_password(char* str, AuthenticationLevel& auth_level) {
    auth_level = AuthenticationLevel::LEVEL_ADMIN;
}
#endif
