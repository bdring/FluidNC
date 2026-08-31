// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Job.h"
#include <map>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

std::vector<JobSource*> job;

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
        leader = out_channel;
    }
    job.push_back(source);
}
// Caller holds s_job_mutex.
void Job::pop() {
    auto source = job.back();
    job.pop_back();
    delete source;
    if (job.empty()) {
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
