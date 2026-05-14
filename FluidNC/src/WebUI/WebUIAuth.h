#ifdef ENABLE_AUTHENTICATION
namespace WebUI {

    struct AuthenticationIP {
        IPAddress           ip;
        AuthenticationLevel level;
        char                userID[17];
        char                sessionID[17];
        uint32_t            last_time;
        AuthenticationIP*   _next;
    };
};

static AuthenticationIP*   _head;
static uint8_t             _nb_ip;
static bool                AddAuthIP(AuthenticationIP* item);
static const char*         create_session_ID();
static bool                ClearAuthIP(IPAddress ip, const char* sessionID);
static AuthenticationIP*   GetAuth(IPAddress ip, const char* sessionID);
static AuthenticationLevel ResetAuthIP(IPAddress ip, const char* sessionID);
#endif
