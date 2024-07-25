#include "HTTPChannel.h"

#include <HTTPClient.h>

static std::string urlEncode(const char* msg) {
    const char* hex        = "0123456789ABCDEF";
    std::string encodedMsg = "";

    while (*msg != '\0') {
        if (('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '_' ||
            *msg == '.' || *msg == '~') {
            encodedMsg += *msg;
        } else {
            encodedMsg += '%';
            encodedMsg += hex[(unsigned char)*msg >> 4];
            encodedMsg += hex[*msg & 0xf];
        }
        msg++;
    }
    return encodedMsg;
}

std::string HTTPChannel::_url;

Channel* HTTPChannel::set_responder(const std::string& server, const std::string& port) {
    _url = "http://";
    _url += server;
    _url += ":";
    _url += port;
    _url += "/?client=";
    _url += WiFi.getHostname();
    _url += "&message=";
    log_debug("Httpresponder " << _url);
    return pinstance();
}

void HTTPChannel::print_msg(MsgLevel level, const char* msg) {
    std::string combined(_url);
    combined += urlEncode(msg);
    HTTPClient http;
    http.begin(combined.c_str());

    int httpCode = http.GET();
    if (httpCode < 200 || httpCode > 299) {
        // XXX maybe retry
        log_error("HTTP notification failed: code " << httpCode << " message " << msg);
    }
}
