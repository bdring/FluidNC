// Copyright (c) 2026 - Gandalf van Schnaufenberg
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#include <cstdint>
#include <cstddef>
#include <string>

#include "Error.h"

class Channel;

// Periodically records enough state to pick a long job back up after the power
// goes out, and restores it on request.
//
// What is recorded is the state of the block the machine is *executing*, not
// the state of the file reader.  Those are far apart: a line is read, queued,
// parsed, planned, and only then stepped, so the read head runs ahead of the
// cutter by the whole cmd_queue and planner buffer.  Checkpointing the read
// head would resume past work that was never actually done.  plan_block_t
// carries file_offset for exactly this reason.
//
// The offset stored is the *start* of the interrupted line, so resuming re-runs
// it rather than skipping the part that never got cut.  Overlap is recoverable;
// a gap is not.
//
// The machine cannot know where it is after a power cut, and this build does
// not assume homing.  Resume therefore restores everything except position, and
// requires the operator to have re-established zero first.
//
// Only jobs on the SD card are checkpointed.  A job on the local filesystem is
// a macro - homing, tool change, a few lines of setup - and resuming one
// partway through is not something anybody wants.  It would also cost a flash
// erase/write cycle every interval on a small partition shared with config.yaml
// and the WebUI, and stop the flash cache on both cores each time while the
// step ISR is running.  The card has neither problem, and is where long jobs
// live.
namespace JobResume {
    struct Checkpoint {
        std::string path;      // job file on the card, canonical: "/sd/job.gcode"
        size_t      offset;    // byte offset to resume reading from
        int32_t     line;      // N word if the file had one, else 0
        uint32_t    file_size; // refuse to resume a file that has changed
        float       mpos[MAX_N_AXIS];  // machine position - meaningless after a reboot without homing
        float       wpos[MAX_N_AXIS];  // work position - what a re-zeroed machine can return to
        float       coord_offset[MAX_N_AXIS];  // G92
        uint8_t     coord_select;              // G54..G59
        float       feed_rate;
        float       spindle_speed;
        uint8_t     spindle;
        uint8_t     coolant;
        uint8_t     units;
        uint8_t     distance;
        uint32_t    tool;
    };

    // Called from the main loop.  Cheap when there is nothing to do: it writes
    // at most once per interval, and only while an SD job is actually running.
    void poll();

    // Drop the stored checkpoint - a job that finished has nothing to resume.
    void clear();

    // Clear only if the stored checkpoint describes this file.  A job
    // completing is not by itself a reason to discard someone else's
    // checkpoint.
    void finished(const std::string& path);

    // Most recent valid checkpoint, newest of the two slots. False if there is
    // none, or if both slots fail their CRC.
    bool read(Checkpoint& out);

    // How often to write, in milliseconds. 0 disables checkpointing.
    extern uint32_t interval_ms;

    // Describe the stored checkpoint on `out`, or say there is none.
    void describe(Channel& out);

    // Restore modal state, move to the recorded work position and continue the
    // job from the recorded offset.  Assumes the operator has already
    // re-established work zero: without homing the controller cannot know where
    // the tool is, so this trusts the current zero rather than pretending to.
    Error resume(Channel& out);
}
