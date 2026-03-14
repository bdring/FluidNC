#include "Authentication.h"
#include "Config.h"

#include <cstring>

#ifdef ENABLE_AUTHENTICATION
#    include "Settings.h"

Error setUserPassword(const char* parameter, AuthenticationLevel auth_level, Channel& out);

// TODO Settings - need ADMIN_ONLY and if it is called without a parameter it sets the default
AuthPasswordSetting* user_password;
AuthPasswordSetting* admin_password;

void make_authentication_settings() {
    new WebCommand("password", WEBCMD, WA, "ESP555", "WebUI/SetUserPassword", setUserPassword);
    user_password  = new AuthPasswordSetting("User password", "WebUI/UserPassword", DEFAULT_USER_PWD);
    admin_password = new AuthPasswordSetting("Admin password", "WebUI/AdminPassword", DEFAULT_ADMIN_PWD);
}
#endif
