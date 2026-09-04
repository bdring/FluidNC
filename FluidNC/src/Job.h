// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include <vector>

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

    // Expose local parameters for enumeration
    const std::map<std::string, float>& local_params() const { return _local_params; }

    void   save() { _channel->save(); }
    void   restore() { _channel->restore(); }
    size_t position() { return _channel->position(); }
    void   set_position(size_t pos) { _channel->set_position(pos); }
    size_t lineNumber() { return _channel->lineNumber(); }
    void   setLineNumber(size_t line_number) { _channel->setLineNumber(line_number); }

    Channel* channel() { return _channel; }

    ~JobSource();
};

// The job stack is mutated from two tasks: nest() runs on the protocol task
// (via execute_line -> $SD/Run / macro run), while unnest()/abort() run on the
// polling task.  Every method below takes an internal mutex, so individual
// calls are atomic; callers that need a consistent value across several fields
// should use one call (e.g. channel(), which returns nullptr when idle) rather
// than active() followed by a second call.
class Job {
private:
    static void pop();

    // Caller holds s_job_mutex.  Drops the processing reference taken in
    // nest() and clears the pointer.
    static void release_leader();  // caller holds the job mutex

public:
    // Prefer leader_channel() for a locked read; the bare pointer is retained
    // for existing call sites and is written only under the job mutex.
    static Channel* leader;

    static bool active();

    static void       save();
    static void       restore();
    static void       nest(Channel* in_channel, Channel* out_channel);
    static void       unnest();
    static void       abort();
    static JobSource* source();  // nullptr when no job is active

    static bool     get_param(const std::string& name, float& value);
    static bool     set_param(const std::string& name, float value);
    static bool     param_exists(const std::string& name);
    static Channel* channel();         // top-of-stack channel, or nullptr when idle
    static Channel* leader_channel();  // job leader, or nullptr when idle

    // Snapshot of the stack for $Local/Params listing.  Not safe against a
    // concurrent unnest()/abort(); only meaningful for interactive use.
    static const std::vector<JobSource*>& jobs_stack();
};

void list_local_params(Channel& out);
