// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include <vector>

class JobSource {
private:
    Channel*                     _channel;
    std::map<std::string, float> _local_params;

    // Line number at which protocol_main_loop's pre-dispatch pause gate
    // should pause before dispatching this job's channel, in addition to (or
    // instead of) single block mode; 0 means no stop is requested. Scoped to
    // this JobSource (like _local_params) rather than shared across the whole
    // stack, so a nested job (e.g. a restart_macro) can never
    // collide with a stop line meant for the job underneath it -- it simply
    // isn't job.back() while the nested job is on top, so its own stop_line
    // (0, unless someone sets one) is the only one ever consulted.
    int32_t _stop_line = 0;

    // Channel owed a deferred ack/error reply for the command line that
    // nested this JobSource (M6's tool-change macro, $SD/Run, $LocalFS/Run),
    // once this JobSource is popped. nullptr if that line was already acked
    // immediately (the common case: informational commands, or nest() calls
    // that are not standing in for a line awaiting a reply). See
    // Job::defer_ack() and Job::pop().
    Channel* _ack_channel = nullptr;

public:
    JobSource(Channel* channel) : _channel(channel) {}
    int32_t stop_line() { return _stop_line; }
    void    set_stop_line(int32_t line) { _stop_line = line; }
    void     set_pending_ack(Channel* channel) { _ack_channel = channel; }
    Channel* ack_channel() const { return _ack_channel; }
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
    // Caller holds s_job_mutex.  Removes the top JobSource; if it had a
    // deferred ack pending (Job::defer_ack()), appends its channel to
    // acks_owed so unnest()/abort() can fire it once the lock is released --
    // ack()/release_processing_ref() may do channel I/O, which should not run
    // under s_job_mutex.
    static void pop(std::vector<Channel*>& acks_owed);

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
    static void       nest(Channel* in_channel, Channel* out_channel, int32_t stop_line = 0);
    static void       unnest();
    static void       abort(Error status = Error::Reset);
    static JobSource* source();  // nullptr when no job is active

    // Stack depth, for detecting that a line just dispatched pushed a new job
    // (see Protocol.cpp's cmd_queue consumer, which compares this before and
    // after execute_line()).
    static size_t depth();

    // Marks the top-of-stack JobSource as owing a deferred ack/error reply to
    // `channel` for the command line that just nested it. Job::nest() only
    // pushes the job source and returns -- the real work (opening the file,
    // running the macro's first line) has not happened yet -- so acking that
    // line immediately would tell the sender it is safe to resume sending
    // while the job is still starting up, racing this same JobSource's own
    // Job::unnest()/Job::abort() teardown. See FluidNC issue #1862. The reply
    // is sent once this JobSource is popped: Error::Ok from a normal
    // Job::unnest(), or whatever status is given to Job::abort().
    static void defer_ack(Channel* channel);

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

    // Delegate to the top-of-stack JobSource's own stop_line (see above); 0
    // (no stop requested) if no job is active. Set via $SD/Run and
    // $LocalFS/Run's optional ",line" argument, or nest()'s stop_line
    // parameter: preceded by $C, this stops a check-mode dry run at a
    // specific line; without $C first, it stops a normal run there instead.
    static int32_t stop_line();
    static void    set_stop_line(int32_t line);

    // True if the top-of-stack job's channel has reached its own configured
    // stop_line(). A pure query -- it does not clear the stop line. The
    // caller decides when the stop is truly consumed, via set_stop_line(0):
    // a nested restart_macro job needs the original job's stop line left
    // alone, so reaching it again re-triggers once the macro finishes and
    // unnests, rather than only firing once. Used by protocol_main_loop's
    // pre-dispatch pause gate (Protocol.cpp).
    static bool at_stop_line();

    // Rewinds the top-of-stack job's channel back to the start of the line
    // it just read (via Channel::lineStartPosition()/lineNumber()), so that
    // line is read again, unchanged, on a later pollLine() call -- e.g. to
    // defer it while a restart_macro runs first.
    static void rewind_current_line();

    // Snapshot of the stack for $Local/Params listing.  Not safe against a
    // concurrent unnest()/abort(); only meaningful for interactive use.
    static const std::vector<JobSource*>& jobs_stack();
};

void list_local_params(Channel& out);
