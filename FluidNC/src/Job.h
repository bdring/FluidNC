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
    float get_param(const std::string& name) { return _local_params[name]; }
    void  set_param(const std::string& name, float value) { _local_params[name] = value; }
    bool  param_exists(const std::string& name) { return _local_params.count(name) != 0; }

    void     save() { _channel->save(); }
    void     restore() { _channel->restore(); }
    Channel* channel() { return _channel; }

    ~JobSource() { delete _channel; }
};

class Job {
private:
    static void pop();

    static Channel* _new_leader;

public:
    static Channel* leader;

    static bool active();

    static void save();
    static void restore();
    static void nest(Channel* in_channel, Channel* out_channel);
    static void unnest();
    static void abort();

    static float    get_param(const std::string& name);
    static void     set_param(const std::string& name, float value);
    static bool     param_exists(const std::string& name);
    static Channel* channel();

    static void set_leader(Channel* out) { _new_leader = out; }
};
