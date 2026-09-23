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

    // Status to report on a normal (Eof-driven) Job::unnest() instead of the
    // usual Error::Ok, if non-Ok. Covers a line that both starts this job
    // and fails afterward in some other way it already returned
    // Error::Deferred for -- e.g. a combined "M6 M62 P0" line where the M62
    // half is invalid: the M6 half already started the job by the time the
    // M62 half fails, so the failure has to ride along on the eventual
    // deferred ack instead of being returned directly (FluidNC issue
    // #1862). Job::abort()'s own status always takes priority over this.
    Error _ack_error = Error::Ok;

public:
    JobSource(Channel* channel) : _channel(channel) {}
    void     set_pending_ack(Channel* channel) { _ack_channel = channel; }
    Channel* ack_channel() const { return _ack_channel; }
    void     set_ack_error(Error err) { _ack_error = err; }
    Error    ack_error() const { return _ack_error; }
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
public:
    // A deferred ack (see nest()'s ack_channel argument) collected by pop(),
    // to be fired once s_job_mutex is released.
    struct PendingAck {
        Channel* channel;
        Error    status;
    };

private:
    // Caller holds s_job_mutex.  Removes the top JobSource; if it had a
    // deferred ack pending (set via nest()'s ack_channel argument), appends
    // it to acks_owed so unnest()/abort() can fire it once the lock is
    // released -- ack()/release_processing_ref() may do channel I/O, which
    // should not run under s_job_mutex.
    static void pop(std::vector<PendingAck>& acks_owed);

    // Caller holds s_job_mutex.  Drops the processing reference taken in
    // nest() and clears the pointer.
    static void release_leader();  // caller holds the job mutex

public:
    // Prefer leader_channel() for a locked read; the bare pointer is retained
    // for existing call sites and is written only under the job mutex.
    static Channel* leader;

    // The channel actually holding the processing ref for the command line
    // protocol_main_loop's cmd_queue consumer is currently dispatching on
    // the protocol task -- set there just before calling execute_line(),
    // valid only for the duration of that call, nullptr otherwise.
    //
    // This is deliberately NOT the same channel execute_line()/
    // gc_execute_line() are themselves called with: that one is
    // out_channel (the job leader, when the dispatched line is itself a
    // job's own line, e.g. M6/$SD/Run executed from inside an already-
    // running SD file), used for routing diagnostics to whoever is
    // watching the job. A deferred ack, in contrast, must go back to
    // whichever channel actually sent this specific line and holds cmd_queue's
    // matching processing ref -- using out_channel there instead would ack
    // the wrong party and release the wrong ref (FluidNC issue #1862).
    // nest()'s ack_channel argument (via Macro::run()'s defer_ack and
    // FileCommands.cpp's runFile()) must read this, not out_channel.
    static Channel* dispatch_channel;

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

    // Overrides the status a pending deferred ack reports on a normal
    // Job::unnest(), for the JobSource whose ack_channel is `ack_channel`
    // (a no-op if none matches -- nothing deferred there, or it already
    // fired). Found by channel identity rather than stack position because
    // more of the stack may have been pushed and popped by the time this is
    // called (e.g. a combined M6 M62 line: protocol_buffer_synchronize()
    // inside the M62 handling can pump a pin-triggered macro event in
    // between M6 starting this job and M62 failing). See JobSource::_ack_error.
    static void set_ack_error(Channel* ack_channel, Error err);

    // Atomically checks unwind_cause against the job stack and, if a job is
    // active, aborts it and clears the flag - all under the one job-mutex
    // critical section that nest() also uses to clear the flag when it
    // starts a fresh stack. Reading unwind_cause and deciding whether to
    // abort outside that lock (the old poll_once() shape) let nest() clear
    // the flag for a brand-new job in the gap between the read and the
    // abort, so the freshly nested job got killed anyway (FluidNC issue
    // #1861). Returns true if it aborted a job.
    static bool consume_unwind_cause();

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
