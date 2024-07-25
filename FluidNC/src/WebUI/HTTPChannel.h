#pragma once

#include "src/Channel.h"

class HTTPChannel : public Channel {
private:
    static std::string _url;

public:
    HTTPChannel() : Channel("http") {}

    static Channel* set_responder(const std::string& server, const std::string& port);

    void   print_msg(MsgLevel level, const char* msg) override;
    size_t write(uint8_t c) override { return 0; }

    // singleton
    static HTTPChannel* pinstance() {
        static HTTPChannel instance;
        return &instance;
    }
};
