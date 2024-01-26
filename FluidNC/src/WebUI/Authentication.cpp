#include "Authentication.h"

#include "../Config.h"  // ENABLE_*

#include <cstring>

namespace WebUI {
#ifdef ENABLE_AUTHENTICATION
    // TODO Settings - need ADMIN_ONLY and if it is called without a parameter it sets the default
    AuthPasswordSetting* user_password;
    AuthPassowrdSetting* admin_password;

    void remove_password(char* str, AuthenticationLevel& auth_level) {
        std::string paramStr(str);
        size_t      pos = paramStr.find("pwd=");
        if (pos == std::string::npos) {
            return;
        }

        // Truncate the str string at the pwd= .
        // If the pwd= is preceded by a space, take off that space too.
        int endpos = pos;
        if (endpos && str[endpos - 1] == ' ') {
            --endpos;
        }
        str[endpos] = '\0';

        // Upgrade the authentication level if a password
        // for a higher level is present.
        const char* password = str + pos + strlen("pwd=");
        if (auth_level < AuthenticationLevel::LEVEL_USER) {
            if (!strcmp(password, user_password->get())) {
                auth_level = AuthenticationLevel::LEVEL_USER;
            }
        }
        if (auth_level < AuthenticationLevel::LEVEL_ADMIN) {
            if (!strcmp(password, admin_password->get())) {
                auth_level = AuthenticationLevel::LEVEL_ADMIN;
            }
        }
    }
#else
    void remove_password(char* str, AuthenticationLevel& auth_level) { auth_level = AuthenticationLevel::LEVEL_ADMIN; }
#endif
}
