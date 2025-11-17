#ifdef ENABLE_AUTHENTICATION
AuthenticationIP* WebUI_Server::_head  = NULL;
uint8_t           WebUI_Server::_nb_ip = 0;
const int         MAX_AUTH_IP          = 10;

void WebUI_Server::auth_deinit() {
    while (_head) {
        AuthenticationIP* current = _head;
        _head                     = _head->_next;
        delete current;
    }
    _nb_ip = 0;
}
//login status check
void WebUI_Server::handle_login(AsyncWebServerRequest* request) {
    const char* smsg;
    std::string sUser, sPassword;
    const char* auths;
    uint16_t    code            = 200;
    bool        msg_alert_error = false;
    //disconnect can be done anytime no need to check credential
    if (_webserver->hasArg("DISCONNECT")) {
        std::string cookie(_webserver->header("Cookie").c_str());
        size_t      pos = cookie.find("ESPSESSIONID=");
        std::string sessionID;
        if (pos != std::string::npos) {
            size_t pos2 = cookie.find(";", pos);
            sessionID   = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
        }
        ClearAuthIP(_webserver->client().remoteIP(), sessionID);
        _webserver->sendHeader("Set-Cookie", "ESPSESSIONID=0");
        _webserver->sendHeader("Cache-Control", "no-cache");
        sendAuth("Ok", "guest", "");
        //_webserver->client().stop();
        return;
    }

    AuthenticationLevel auth_level = is_authenticated();
    if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
        auths = "guest";
    } else if (auth_level == AuthenticationLevel::LEVEL_USER) {
        auths = "user";
    } else if (auth_level == AuthenticationLevel::LEVEL_ADMIN) {
        auths = "admin";
    } else {
        auths = "???";
    }

    //check is it is a submission or a query
    if (_webserver->hasArg("SUBMIT")) {
        //is there a correct list of query?
        if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER")) {
            //USER
            sUser = _webserver->arg("USER").c_str();
            if (!((sUser == DEFAULT_ADMIN_LOGIN) || (sUser == DEFAULT_USER_LOGIN))) {
                msg_alert_error = true;
                smsg            = "Error : Incorrect User";
                code            = 401;
            }

            if (msg_alert_error == false) {
                //Password
                sPassword = _webserver->arg("PASSWORD").c_str();
                std::string sadminPassword(admin_password->get());
                std::string suserPassword(user_password->get());

                if (!(sUser == DEFAULT_ADMIN_LOGIN && sPassword == sadminPassword) ||
                    (sUser == DEFAULT_USER_LOGIN && sPassword == suserPassword)) {
                    msg_alert_error = true;
                    smsg            = "Error: Incorrect password";
                    code            = 401;
                }
            }
        } else {
            msg_alert_error = true;
            smsg            = "Error: Missing data";
            code            = 500;
        }
        //change password
        if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER") && _webserver->hasArg("NEWPASSWORD") && (msg_alert_error == false)) {
            std::string newpassword(_webserver->arg("NEWPASSWORD").c_str());

            char pwdbuf[MAX_LOCAL_PASSWORD_LENGTH + 1];
            newpassword.toCharArray(pwdbuf, MAX_LOCAL_PASSWORD_LENGTH + 1);

            Error err;

            if (sUser == DEFAULT_ADMIN_LOGIN) {
                err = admin_password->setStringValue(pwdbuf);
            } else {
                err = user_password->setStringValue(pwdbuf);
            }
            if (err != Error::Ok) {
                msg_alert_error = true;
                smsg            = "Error: Password cannot contain spaces";
                code            = 500;
            }
        }
        if ((code == 200) || (code == 500)) {
            AuthenticationLevel current_auth_level;
            if (sUser == DEFAULT_ADMIN_LOGIN) {
                current_auth_level = AuthenticationLevel::LEVEL_ADMIN;
            } else if (sUser == DEFAULT_USER_LOGIN) {
                current_auth_level = AuthenticationLevel::LEVEL_USER;
            } else {
                current_auth_level = AuthenticationLevel::LEVEL_GUEST;
            }
            //create Session
            if ((current_auth_level != auth_level) || (auth_level == AuthenticationLevel::LEVEL_GUEST)) {
                AuthenticationIP* current_auth = new AuthenticationIP;
                current_auth->level            = current_auth_level;
                current_auth->ip               = _webserver->client().remoteIP();
                strcpy(current_auth->sessionID, create_session_ID());
                strcpy(current_auth->userID, sUser.c_str());
                current_auth->last_time = millis();
                if (AddAuthIP(current_auth)) {
                    std::string tmps = "ESPSESSIONID=";
                    tmps += current_auth->sessionID.c_str();
                    _webserver->sendHeader("Set-Cookie", tmps);
                    _webserver->sendHeader("Cache-Control", "no-cache");
                    switch (current_auth->level) {
                        case AuthenticationLevel::LEVEL_ADMIN:
                            auths = "admin";
                            break;
                        case AuthenticationLevel::LEVEL_USER:
                            auths = "user";
                            break;
                        default:
                            auths = "guest";
                            break;
                    }
                } else {
                    delete current_auth;
                    msg_alert_error = true;
                    code            = 500;
                    smsg            = "Error: Too many connections";
                }
            }
        }
        if (code == 200) {
            smsg = "Ok";
        }

        sendAuth("Ok", "guest", "");
    } else {
        if (auth_level != AuthenticationLevel::LEVEL_GUEST) {
            std::string cookie(_webserver->header("Cookie").c_str());
            size_t      pos = cookie.find("ESPSESSIONID=");
            std::string sessionID;
            if (pos != std::string::npos) {
                size_t pos2                         = cookie.find(";", pos);
                sessionID                           = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
                AuthenticationIP* current_auth_info = GetAuth(_webserver->client().remoteIP(), sessionID.c_str());
                if (current_auth_info != NULL) {
                    sUser = current_auth_info->userID;
                }
            }
        }
        sendAuth(smsg, auths, "");
    }
}

//check authentication
AuthenticationLevel WebUI_Server::is_authenticated() {
    if (_webserver->hasHeader("Cookie")) {
        std::string cookie(_webserver->header("Cookie").c_str());
        size_t      pos = cookie.find("ESPSESSIONID=");
        if (pos != std::string::npos) {
            size_t      pos2      = cookie.find(";", pos);
            std::string sessionID = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
            IPAddress   ip        = _webserver->client().remoteIP();
            //check if cookie can be reset and clean table in same time
            return ResetAuthIP(ip, sessionID.c_str());
        }
    }
    return AuthenticationLevel::LEVEL_GUEST;
}

//add the information in the linked list if possible
bool WebUI_Server::AddAuthIP(AuthenticationIP* item) {
    if (_nb_ip > MAX_AUTH_IP) {
        return false;
    }
    item->_next = _head;
    _head       = item;
    _nb_ip++;
    return true;
}

//Session ID based on IP and time using 16 char
const char* WebUI_Server::create_session_ID() {
    static char sessionID[17];
    //reset SESSIONID
    for (size_t i = 0; i < 17; i++) {
        sessionID[i] = '\0';
    }
    //get time
    uint32_t now = millis();
    //get remote IP
    IPAddress remoteIP = _webserver->client().remoteIP();
    //generate SESSIONID
    if (0 > sprintf(sessionID,
                    "%02X%02X%02X%02X%02X%02X%02X%02X",
                    remoteIP[0],
                    remoteIP[1],
                    remoteIP[2],
                    remoteIP[3],
                    (uint8_t)((now >> 0) & 0xff),
                    (uint8_t)((now >> 8) & 0xff),
                    (uint8_t)((now >> 16) & 0xff),
                    (uint8_t)((now >> 24) & 0xff))) {
        strcpy(sessionID, "NONE");
    }
    return sessionID;
}

bool WebUI_Server::ClearAuthIP(IPAddress ip, const char* sessionID) {
    AuthenticationIP* current  = _head;
    AuthenticationIP* previous = NULL;
    bool              done     = false;
    while (current) {
        if ((ip == current->ip) && (strcmp(sessionID, current->sessionID) == 0)) {
            //remove
            done = true;
            if (current == _head) {
                _head = current->_next;
                _nb_ip--;
                delete current;
                current = _head;
            } else {
                previous->_next = current->_next;
                _nb_ip--;
                delete current;
                current = previous->_next;
            }
        } else {
            previous = current;
            current  = current->_next;
        }
    }
    return done;
}

//Get info
AuthenticationIP* WebUI_Server::GetAuth(IPAddress ip, const char* sessionID) {
    AuthenticationIP* current = _head;
    //AuthenticationIP * previous = NULL;
    while (current) {
        if (ip == current->ip) {
            if (strcmp(sessionID, current->sessionID) == 0) {
                //found
                return current;
            }
        }
        //previous = current;
        current = current->_next;
    }
    return NULL;
}

//Review all IP to reset timers
AuthenticationLevel WebUI_Server::ResetAuthIP(IPAddress ip, const char* sessionID) {
    AuthenticationIP* current  = _head;
    AuthenticationIP* previous = NULL;
    while (current) {
        if ((millis() - current->last_time) > 360000) {
            //remove
            if (current == _head) {
                _head = current->_next;
                _nb_ip--;
                delete current;
                current = _head;
            } else {
                previous->_next = current->_next;
                _nb_ip--;
                delete current;
                current = previous->_next;
            }
        } else {
            if (ip == current->ip && strcmp(sessionID, current->sessionID) == 0) {
                //reset time
                current->last_time = millis();
                return (AuthenticationLevel)current->level;
            }
            previous = current;
            current  = current->_next;
        }
    }
    return AuthenticationLevel::LEVEL_GUEST;
}
#endif
