// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include <stack>

class JobSource {
private:
    Channel*                     _channel;
    std::map<std::string, float> _local_params;

public:
    JobSource(Channel* channel) : _channel(channel) {}
    bool get_param(const std::string& name, float& value) {
        auto it = _local_params.find(name);
        if (it == _local_params.end()) {
            return false;
        }
        value = it->second;
        return true;
    }
    bool set_param(const std::string& name, float value) {
        _local_params[name] = value;
        return true;
    }
    bool param_exists(const std::string& name) { return _local_params.count(name) != 0; }

    void   save() { _channel->save(); }
    void   restore() { _channel->restore(); }
    size_t position() { return _channel->position(); }
    void   set_position(size_t pos) { _channel->set_position(pos); }

    Channel* channel() { return _channel; }

    ~JobSource() { delete _channel; }
};

class Job {
private:
    static void pop();

public:
    static Channel* leader;

    static bool active();

    static void       save();
    static void       restore();
    static void       nest(Channel* in_channel, Channel* out_channel);
    static void       unnest();
    static void       abort();
    static JobSource* source();

    static bool     get_param(const std::string& name, float& value);
    static bool     set_param(const std::string& name, float value);
    static bool     param_exists(const std::string& name);
    static Channel* channel();
};
