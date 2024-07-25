// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Job.h"
#include <map>
#include <stack>

std::stack<JobSource*> job;

Channel* Job::leader      = nullptr;
Channel* Job::_new_leader = nullptr;

bool Job::active() {
    return !job.empty();
}

// save() and restore() are use to close/reopen an SD file atop the job stack
// before trying to open a nested SD file.  The reason for that is because
// the number of simultaneously-open SD files is limited to conserve RAM.
void Job::save() {
    if (active()) {
        job.top()->save();
    }
}
void Job::restore() {
    if (active()) {
        job.top()->restore();
    }
}
void Job::nest(Channel* in_channel, Channel* out_channel) {
    auto source = new JobSource(in_channel);
    if (job.empty()) {
        if (_new_leader) {
            leader      = _new_leader;
            _new_leader = nullptr;
        } else if (out_channel) {
            leader = out_channel;
        }
    }
    job.push(source);
}
void Job::pop() {
    auto source = job.top();
    job.pop();
    delete source;
    if (!active()) {
        leader = nullptr;
    }
}
void Job::unnest() {
    if (active()) {
        pop();
        restore();
    }
}

void Job::abort() {
    // Kill all active jobs
    while (active()) {
        pop();
    }
}

float Job::get_param(const std::string& name) {
    return job.top()->get_param(name);
}
void Job::set_param(const std::string& name, float value) {
    job.top()->set_param(name, value);
}
bool Job::param_exists(const std::string& name) {
    return job.top()->param_exists(name);
}
Channel* Job::channel() {
    return job.top()->channel();
}
