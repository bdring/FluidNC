#include "Authentication.h"

#include <cstring>

#ifdef ENABLE_AUTHENTICATION
// TODO Settings - need ADMIN_ONLY and if it is called without a parameter it sets the default
AuthPasswordSetting* user_password;
AuthPassowrdSetting* admin_password;

void make_authentication_settings() {
#    ifdef ENABLE_AUTHENTICATION
    new WebCommand("password", WEBCMD, WA, "ESP555", "WebUI/SetUserPassword", setUserPassword);
    user_password  = new AuthPasswordSetting("User password", "WebUI/UserPassword", DEFAULT_USER_PWD);
    admin_password = new AuthPasswordSetting("Admin password", "WebUI/AdminPassword", DEFAULT_ADMIN_PWD);
#    endif
}
#endif
