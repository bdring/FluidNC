// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Job.h"
#include "Logging.h"
#include "Serial.h"  // allChannels
#include <map>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

std::vector<JobSource*> job;

// A job's channel used to be the one Channel in the system deleted directly
// rather than handed to AllChannels::kill() and its reaper, so nothing checked
// whether anyone still held a reference to it.
//
// Two kinds of reference matter.  Channel::sendLine() takes a *log* reference
// and queues the message with a bare pointer, so freeing the channel before
// output_loop() drains the queue calls a virtual method on freed memory:
//
//   output_loop -> Channel::print_msg -> Print::write -> __cxa_pure_virtual
//
// protocol_main_loop() holds a *processing* reference across execute_line(),
// which is where a job's channel gets closed, and afterwards calls
// channel->ack() on it.  Freeing the channel in between leaves that call
// reading a zeroed vtable:
//
//   LoadProhibited, EXCVADDR 0x3c, in protocol_main_loop at channel->ack()
//
// Waiting on the log references alone missed the second case, so hand the
// channel to AllChannels::kill() instead.  Its reaper deletes the channel only
// once *both* counts reach zero, which is the check this teardown needs and
// already exists there.
JobSource::~JobSource() {
    if (!_channel) {
        return;
    }
    // begin_closing() makes try_acquire_log_ref() fail, so no further messages
    // can be queued against this channel while it waits to be reaped.
    _channel->begin_closing();
    if (!allChannels.kill(_channel)) {
        // The kill queue is full.  Leaking one channel is bounded and
        // recoverable; deleting it with references outstanding is not.
        log_error("Could not queue job channel for reaping; leaking it");
    }
    _channel = nullptr;
}


Channel* Job::leader = nullptr;

// Guards `job` and `leader`.  See the note in Job.h.
static SemaphoreHandle_t s_job_mutex = xSemaphoreCreateMutex();

namespace {
    struct JobLock {
        JobLock() { xSemaphoreTake(s_job_mutex, portMAX_DELAY); }
        ~JobLock() { xSemaphoreGive(s_job_mutex); }
    };

    // Unlocked internals; the caller holds s_job_mutex.
    bool active_nl() {
        return !job.empty();
    }
    void save_nl() {
        if (active_nl()) {
            job.back()->save();
        }
    }
    void restore_nl() {
        if (active_nl()) {
            job.back()->restore();
        }
    }
}

bool Job::active() {
    JobLock lock;
    return active_nl();
}

JobSource* Job::source() {
    JobLock lock;
    return job.empty() ? nullptr : job.back();
}

// save() and restore() are use to close/reopen an SD file atop the job stack
// before trying to open a nested SD file.  The reason for that is because
// the number of simultaneously-open SD files is limited to conserve RAM.
void Job::save() {
    JobLock lock;
    save_nl();
}
void Job::restore() {
    JobLock lock;
    restore_nl();
}
void Job::nest(Channel* in_channel, Channel* out_channel) {
    JobLock lock;
    auto source = new JobSource(in_channel);
    if (out_channel && job.empty()) {
        // Hold a processing reference for the duration of the job.  A leader
        // can die while the job runs - a WebSocket or an HTTP client
        // disappears as soon as WiFi connectivity is lost - and the channel is
        // then deregistered and deleted as soon as its reference counts reach
        // zero, which between GCode lines is immediately.  The reference keeps
        // the object alive, so leader stays a valid pointer; writes to a
        // channel that is closing are discarded by the channel itself.
        //
        // leader_channel() returning nullptr once the stack is empty guards
        // against reading it after the job, but not against the object being
        // freed during the job.
        if (out_channel->try_acquire_processing_ref()) {
            leader = out_channel;
        }
    }
    job.push_back(source);
}
// Caller holds s_job_mutex.
void Job::pop() {
    auto source = job.back();
    job.pop_back();
    delete source;
    if (job.empty()) {
        release_leader();
    }
}

// Caller holds s_job_mutex.
void Job::release_leader() {
    if (leader) {
        leader->release_processing_ref();
        leader = nullptr;
    }
}
void Job::unnest() {
    JobLock lock;
    if (active_nl()) {
        pop();
        restore_nl();
    }
}

void Job::abort() {
    JobLock lock;
    // Kill all active jobs
    while (active_nl()) {
        pop();
    }
}

bool Job::get_param(const std::string& name, float& value) {
    JobLock lock;
    return active_nl() && job.back()->get_param(name, value);
}
bool Job::set_param(const std::string& name, float value) {
    JobLock lock;
    return active_nl() && job.back()->set_param(name, value);
}
bool Job::param_exists(const std::string& name) {
    JobLock lock;
    return active_nl() && job.back()->param_exists(name);
}
Channel* Job::channel() {
    JobLock lock;
    return job.empty() ? nullptr : job.back()->channel();
}
Channel* Job::leader_channel() {
    JobLock lock;
    if (leader && leader->is_closing()) {
        // The channel that launched the job has gone away.  Stop reporting to
        // it and let it be reaped, but keep the job running: losing the WebUI
        // connection is not a reason to abandon a cut already underway.
        log_info("Job output channel " << leader->name() << " closed; the job continues");
        release_leader();
    }
    return job.empty() ? nullptr : leader;
}

const std::vector<JobSource*>& Job::jobs_stack() {
    return job;
}

void list_local_params(Channel& out) {
    const auto& job_stack = Job::jobs_stack();
    if (job_stack.empty()) {
        log_info_to(out, "No active jobs - no local parameters");
        return;
    }

    int depth = 0;
    for (auto source : job_stack) {
        const auto& local_params = source->local_params();
        if (local_params.empty()) {
            log_info_to(out, "Job depth " << depth << " - No local parameters");
        } else {
            log_info_to(out, "Job depth " << depth << " - Local Parameters");
            for (const auto& param : local_params) {
                // Format: parameter_name = value
                log_info_to(out, param.first << " = " << param.second);
            }
        }
        depth++;
    }
}
