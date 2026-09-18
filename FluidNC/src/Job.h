// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include <vector>

class JobSource {
private:
    Channel*                     _channel;
    std::map<std::string, float> _local_params;

    // Channel owed a deferred ack/error reply for the command line that
    // nested this JobSource (M6's tool-change macro, $SD/Run, $LocalFS/Run),
    // once this JobSource is popped. nullptr if that line was already acked
    // immediately (the common case: informational commands, or nest() calls
    // that are not standing in for a line awaiting a reply, e.g. a
    // restart_macro or a pin-triggered macro event). Set atomically with the
    // push, from Job::nest()'s own ack_channel argument -- see the note
    // there for why this can't be inferred after the fact.
    Channel* _ack_channel = nullptr;

public:
    JobSource(Channel* channel) : _channel(channel) {}
    void     set_pending_ack(Channel* channel) { _ack_channel = channel; }
    Channel* ack_channel() const { return _ack_channel; }
    bool     get_param(const std::string& name, float& value) {
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
    // Caller holds s_job_mutex.  Removes the top JobSource; if it had a
    // deferred ack pending (set via nest()'s ack_channel argument), appends
    // its channel to acks_owed so unnest()/abort() can fire it once the lock
    // is released -- ack()/release_processing_ref() may do channel I/O,
    // which should not run under s_job_mutex.
    static void pop(std::vector<Channel*>& acks_owed);

    // Caller holds s_job_mutex.  Drops the processing reference taken in
    // nest() and clears the pointer.
    static void release_leader();  // caller holds the job mutex

public:
    // Prefer leader_channel() for a locked read; the bare pointer is retained
    // for existing call sites and is written only under the job mutex.
    static Channel* leader;

    static bool active();

    static void save();
    static void restore();
    // ack_channel, if non-null, marks the pushed JobSource as owing a
    // deferred ack/error reply to that channel once this same JobSource is
    // popped (Error::Ok from a normal Job::unnest(), or whatever status is
    // given to Job::abort()) -- see FluidNC issue #1862: the command line
    // that gets here (M6, $SD/Run, $LocalFS/Run) would otherwise be acked
    // immediately, before the job it just started has done anything, letting
    // the sender believe it is safe to resume sending while the job is still
    // starting up.
    //
    // This must be set here, atomically with the push under s_job_mutex, not
    // inferred afterward from whether the stack grew: nest() can also be
    // reached from a pin-triggered macro event serviced by
    // protocol_handle_events(), which execute_line() can call synchronously
    // (via protocol_buffer_synchronize()) while dispatching a completely
    // unrelated line. A post-hoc "did the depth change" check cannot tell
    // that unrelated push apart from this one.
    static void       nest(Channel* in_channel, Channel* out_channel, Channel* ack_channel = nullptr);
    static void       unnest();
    static void       abort(Error status = Error::Reset);
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
