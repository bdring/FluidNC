// Copyright (c) 2026 - Gandalf van Schnaufenberg
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "JobResume.h"

#include "Job.h"
#include "Planner.h"
#include "GCode.h"
#include "System.h"
#include "Logging.h"
#include "FluidPath.h"
#include "Machine/MachineConfig.h"
#include "Report.h"      // mpos_to_wpos()
#include "InputFile.h"
#include "Protocol.h"
#include "Error.h"
#include <string>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <Arduino.h>  // millis()

namespace JobResume {
    uint32_t interval_ms = 10000;

    namespace {
        // Two slots written alternately.  A power cut during a write can only
        // damage the slot being written; the other still holds the previous
        // good record, so there is always something to come back to.  This is
        // why the record is not simply rewritten in place.
        // Names within the volume, not absolute paths.  FluidNC mounts SD on
        // demand and unmounts it when the last SDMountState goes away, so a
        // bare fopen("/sd/...") is only correct while somebody else happens to
        // be holding the card.  Every access below goes through a FluidPath,
        // which holds the mount for as long as it is alive.
        const char* slot_name[2] = { ".fnc_resume0", ".fnc_resume1" };

        // Only jobs on the card are checkpointed, and the checkpoint lives
        // beside them there.
        //
        // A job on LocalFS is a macro - a few lines of homing or tool change -
        // and resuming one partway through is not a thing anybody wants.  Nor
        // is it worth what it costs: LocalFS is the ESP32's own flash, so every
        // checkpoint would be a flash erase/write cycle on a small partition
        // shared with config.yaml and the WebUI, and each one stops the flash
        // cache on both cores while the step ISR is running.  The card has none
        // of those problems and is where long jobs live anyway.
        bool on_sd(const std::string& path) {
            return !SD.prefix.empty() && path.rfind(SD.prefix + "/", 0) == 0;
        }

        constexpr uint32_t kMagic   = 0x464e4352;  // "FNCR"
        constexpr uint16_t kVersion = 2;  // v2 added wpos

        struct __attribute__((packed)) Record {
            uint32_t magic;
            uint16_t version;
            uint16_t axes;
            uint32_t seq;  // higher wins
            uint64_t offset;
            int32_t  line;
            uint32_t file_size;
            char     path[128];
            float    mpos[MAX_N_AXIS];
            float    wpos[MAX_N_AXIS];
            float    coord_offset[MAX_N_AXIS];
            uint8_t  coord_select;
            float    feed_rate;
            float    spindle_speed;
            uint8_t  spindle;
            uint8_t  coolant;
            uint8_t  units;
            uint8_t  distance;
            uint32_t tool;
            uint32_t crc;  // over every byte above
        };

        uint32_t crc32(const uint8_t* data, size_t len) {
            uint32_t crc = 0xffffffff;
            for (size_t i = 0; i < len; ++i) {
                crc ^= data[i];
                for (int b = 0; b < 8; ++b) {
                    crc = (crc >> 1) ^ (0xedb88320u & (-(int32_t)(crc & 1)));
                }
            }
            return ~crc;
        }

        uint32_t record_crc(const Record& r) { return crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(Record, crc)); }

        uint32_t s_seq          = 0;
        uint32_t s_last_write   = 0;
        bool     s_have_written = false;
        bool     s_seq_primed   = false;

        bool load_slot(int slot, Record& r) {
            std::error_code ec;
            FluidPath       fpath { slot_name[slot], SD, ec };
            if (ec) {
                return false;
            }
            FILE* fd = fopen(fpath.string().c_str(), "rb");
            if (!fd) {
                return false;
            }
            size_t got = fread(&r, 1, sizeof r, fd);
            fclose(fd);
            return got == sizeof r && r.magic == kMagic && r.version == kVersion && r.crc == record_crc(r);
        }

        // Writing opens a second descriptor while the job file holds the first.
        // sd_mount() allows enough for both plus a WebUI request; see the note
        // where max_files is chosen.
        bool store(const Record& r, int slot) {
            std::error_code ec;
            FluidPath       fpath { slot_name[slot], SD, ec };
            if (ec) {
                log_warn("Resume checkpoint: cannot reach the SD card (" << ec.message() << ")");
                return false;
            }
            errno    = 0;
            FILE* fd = fopen(fpath.string().c_str(), "wb");
            if (!fd) {
                log_warn("Resume checkpoint: cannot create " << fpath.string() << " (" << strerror(errno) << ")");
                return false;
            }
            errno   = 0;
            bool ok = fwrite(&r, 1, sizeof r, fd) == sizeof r;
            ok      = (fflush(fd) == 0) && ok;
            ok      = (fclose(fd) == 0) && ok;
            if (!ok) {
                log_warn("Resume checkpoint: write to " << fpath.string() << " failed (" << strerror(errno) << ")");
            }
            return ok;
        }
    }

    void poll() {
        if (interval_ms == 0) {
            return;
        }

        // One line per job saying what checkpointing is doing, including when
        // it is doing nothing.  Every guard below used to return in silence,
        // which is why "no checkpoint was written" gave nothing to work from.
        static std::string last_job;
        static bool        explained    = false;
        static uint32_t    job_seen_at  = 0;

        Channel* job = Job::root_channel();
        if (!job) {
            last_job.clear();
            return;  // idle: not worth a message
        }
        if (job->path() != last_job) {
            last_job     = job->path();
            explained    = false;
            s_seq_primed = false;
            job_seen_at  = millis();
        }

        // Only jobs on the card are checkpointed.  A job with no backing file
        // is a startup line or a channel-fed macro, and a job on LocalFS is a
        // macro too; neither is something anybody resumes partway through, and
        // both are silent non-events rather than failures worth reporting.
        if (last_job.empty() || !on_sd(last_job)) {
            return;
        }

        uint32_t now = millis();

        // Complain only about a job that has gone a long time with nothing
        // written.  "No block is executing" is the normal state at the start of
        // a job and between moves; reporting it immediately turned a transient
        // into a warning that was followed one line later by success.
        auto explain = [&](const char* why) {
            if (!explained && !s_have_written && (now - job_seen_at) > 30000) {
                explained = true;
                log_warn("Resume checkpoint still not written for " << last_job << " after 30s: " << why);
            }
        };
        if (s_have_written && (now - s_last_write) < interval_ms) {
            return;  // simply not due yet
        }

        // The block the machine is executing now, which is what the recorded
        // position describes.  With the planner empty there is nothing in
        // flight worth a checkpoint - the previous one still stands.
        plan_block_t* block = plan_get_current_block();
        if (!block) {
            explain("no block is executing");
            return;
        }

        // Continue the sequence already on the card rather than restarting at
        // 1.  s_seq is zero after a reboot, and a power cut is exactly when a
        // reboot happens: without this, the first checkpoints of the next job
        // would be numbered below the stale ones still stored, and read() would
        // hand back the *old* job as the newer record.
        if (!s_seq_primed) {
            s_seq_primed = true;
            for (int slot = 0; slot < 2; ++slot) {
                Record prev;
                if (load_slot(slot, prev) && prev.seq > s_seq) {
                    s_seq = prev.seq;
                }
            }
        }

        Record r;
        memset(&r, 0, sizeof r);
        r.magic   = kMagic;
        r.version = kVersion;
        r.axes    = Machine::Axes::_numberAxis;
        r.seq     = ++s_seq;
        r.offset  = block->file_offset;
        r.line    = block->line_number;

        r.file_size = job->size();
        strncpy(r.path, job->path().c_str(), sizeof r.path - 1);

        float* mpos = get_mpos();
        float  wpos[MAX_N_AXIS];
        for (size_t i = 0; i < MAX_N_AXIS; ++i) {
            r.mpos[i]         = mpos[i];
            wpos[i]           = mpos[i];
            r.coord_offset[i] = gc_state.coord_offset[i];
        }
        // Machine position is worthless after a power cut on a machine that
        // cannot home - it is measured from wherever the controller happened to
        // boot.  Work position is what an operator can re-establish by jogging
        // to the workpiece and re-zeroing, so record that too and resume to it.
        mpos_to_wpos(wpos);
        for (size_t i = 0; i < MAX_N_AXIS; ++i) {
            r.wpos[i] = wpos[i];
        }
        r.coord_select  = static_cast<uint8_t>(gc_state.modal.coord_select);
        r.feed_rate     = gc_state.feed_rate;
        r.spindle_speed = gc_state.spindle_speed;
        r.spindle       = static_cast<uint8_t>(gc_state.modal.spindle);
        // CoolantState is a two-bit field, not an integer; pack it by hand.
        r.coolant = (gc_state.modal.coolant.Mist ? 1 : 0) | (gc_state.modal.coolant.Flood ? 2 : 0);
        r.units   = static_cast<uint8_t>(gc_state.modal.units);
        r.distance = static_cast<uint8_t>(gc_state.modal.distance);
        r.tool     = gc_state.selected_tool;
        r.crc      = record_crc(r);

        // Alternate slots so the previous good record always survives.
        if (store(r, s_seq & 1)) {
            if (!s_have_written) {
                log_info("Resume checkpoints being written for " << r.path);
            }
            s_last_write   = now;
            s_have_written = true;
        } else {
            // Do not retry every pass; a card that cannot be written now is
            // unlikely to recover within a few milliseconds, and hammering it
            // would slow the job down for nothing.
            s_last_write   = now;
            s_have_written = true;
        }
    }

    bool read(Checkpoint& out) {
        Record best;
        bool   found = false;
        for (int slot = 0; slot < 2; ++slot) {
            Record r;
            if (load_slot(slot, r) && (!found || r.seq > best.seq)) {
                best  = r;
                found = true;
            }
        }
        if (!found) {
            return false;
        }

        out.path      = best.path;
        out.offset    = static_cast<size_t>(best.offset);
        out.line      = best.line;
        out.file_size = best.file_size;
        for (size_t i = 0; i < MAX_N_AXIS; ++i) {
            out.mpos[i]         = best.mpos[i];
            out.wpos[i]         = best.wpos[i];
            out.coord_offset[i] = best.coord_offset[i];
        }
        out.coord_select  = best.coord_select;
        out.feed_rate     = best.feed_rate;
        out.spindle_speed = best.spindle_speed;
        out.spindle       = best.spindle;
        out.coolant       = best.coolant;
        out.units         = best.units;
        out.distance      = best.distance;
        out.tool          = best.tool;
        return true;
    }

    // Clear only when the job the checkpoint describes is the one that
    // finished.  Clearing on any job completion means the re-zeroing macro an
    // operator runs before $Job/Resume - which is a job like any other - wipes
    // the checkpoint it is about to be used with.
    void finished(const std::string& path) {
        Checkpoint cp;
        if (read(cp) && cp.path == path) {
            clear();
        }
    }

    void clear() {
        for (int slot = 0; slot < 2; ++slot) {
            std::error_code ec;
            FluidPath       fpath { slot_name[slot], SD, ec };
            if (!ec) {
                remove(fpath.string().c_str());
            }
        }
        s_have_written = false;
        s_seq          = 0;
        s_seq_primed   = false;
    }

    void describe(Channel& out) {
        Checkpoint cp;
        if (!read(cp)) {
            log_string(out, "No resume checkpoint stored");
            return;
        }
        log_string(out, "Resume checkpoint:");
        log_string(out, "  file    " + cp.path);
        log_string(out, "  offset  " + std::to_string(cp.offset) + " of " + std::to_string(cp.file_size) + " bytes");

        std::string pos = "  wpos   ";
        for (axis_t i = X_AXIS; i < Machine::Axes::_numberAxis; ++i) {
            pos += " ";
            pos += Machine::Axes::axisName(i);
            pos += std::to_string(cp.wpos[i]);
        }
        log_string(out, pos);
        log_string(out, "  feed    " + std::to_string(cp.feed_rate));
        log_string(out, "  spindle " + std::to_string(cp.spindle_speed));
        log_string(out, "Re-establish work zero, then $Job/Resume=go");
    }

    // Restoring modal state and moving back to the recorded position is done by
    // feeding ordinary G-code through the parser rather than by poking
    // gc_state.  That way every check the parser normally applies - soft
    // limits, feed rate validity, spindle handling - still applies to the
    // moves this makes.
    static Error run(const std::string& line, Channel& out) {
        Error e = gc_execute_line(line.c_str());
        if (e != Error::Ok) {
            log_error_to(out, "Resume step failed: " << line << " -> " << errorString(e));
        }
        return e;
    }

    Error resume(Channel& out) {
        if (Job::active()) {
            log_error_to(out, "A job is already running");
            return Error::IdleError;
        }
        if (state_is(State::Alarm) || state_is(State::ConfigAlarm)) {
            log_error_to(out, "Clear the alarm before resuming");
            return Error::IdleError;
        }

        Checkpoint cp;
        if (!read(cp)) {
            log_error_to(out, "No resume checkpoint stored");
            return Error::FsFailedOpenFile;
        }

        // Open first, so a missing or altered file is refused before anything
        // moves.  Resuming at a byte offset into a file that changed would drop
        // the reader into the middle of some unrelated line.
        // cp.path is canonical and always on the card - "/sd/job.gcode" - and
        // FluidPath::canonPath() resolves the volume from that prefix itself, so
        // no manual split is needed.
        InputFile* file;
        try {
            file = new InputFile(SD, cp.path.c_str());
        } catch (const ErrorException& ex) {
            log_error_to(out, "Cannot open " << cp.path << ": " << ex.what());
            return ex.error();
        } catch (std::filesystem::filesystem_error const& ex) {
            log_error_to(out, "Cannot open " << cp.path << ": " << ex.what());
            return Error::FsFailedOpenFile;
        }
        if (file->size() != cp.file_size) {
            log_error_to(out, "" << cp.path << " has changed since the checkpoint; refusing to resume");
            delete file;
            return Error::InvalidValue;
        }
        if (cp.offset > cp.file_size) {
            log_error_to(out, "Checkpoint offset is past the end of the file");
            delete file;
            return Error::InvalidValue;
        }

        // Positioning moves are emitted in millimetres, absolute, whatever the
        // job was using.  mpos_to_wpos() subtracts the work offset and does no
        // unit conversion, so a recorded position is always in mm: emitting it
        // under a restored G20 would be out by a factor of 25.4, and under a
        // restored G91 it would be read as a relative move.  The job's own
        // units and distance mode are restored below, after the machine is back
        // in place and before the file is handed to the parser.
        std::string setup = "G21 G90 G" + std::to_string(54 + cp.coord_select);
        if (run(setup, out) != Error::Ok) {
            delete file;
            return Error::InvalidValue;
        }

        // Travel at whatever height the operator left the tool at.  There is no
        // safe Z to retract to on a machine that cannot home - machine zero is
        // just wherever Z happened to be when the controller booted, which may
        // be down in the work - so this trusts the operator's clearance rather
        // than inventing one.
        const axis_t zaxis = Z_AXIS;
        std::string  move  = "G0";
        bool         moved = false;
        for (axis_t i = X_AXIS; i < Machine::Axes::_numberAxis; ++i) {
            if (i == zaxis) {
                continue;  // Z goes last, once XY is in place
            }
            move += " ";
            move += Machine::Axes::axisName(i);
            move += std::to_string(cp.wpos[i]);
            moved = true;
        }
        if (moved && run(move, out) != Error::Ok) {
            delete file;
            return Error::InvalidValue;
        }

        // Spindle up to speed before the tool goes back into the work.
        if (cp.spindle != static_cast<uint8_t>(SpindleState::Disable)) {
            std::string sp = (cp.spindle == static_cast<uint8_t>(SpindleState::Ccw)) ? "M4" : "M3";
            sp += " S" + std::to_string(cp.spindle_speed);
            run(sp, out);
        }
        if (cp.coolant & 1) {
            run("M7", out);
        }
        if (cp.coolant & 2) {
            run("M8", out);
        }

        if (Machine::Axes::_numberAxis > zaxis) {
            std::string plunge = "G1 Z" + std::to_string(cp.wpos[zaxis]);
            plunge += " F" + std::to_string(cp.feed_rate > 0 ? cp.feed_rate : 100.0f);
            if (run(plunge, out) != Error::Ok) {
                delete file;
                return Error::InvalidValue;
            }
        }

        // Feed rate while still in G21, so the value lands in gc_state as the
        // mm/min it was recorded as; switching units afterwards does not
        // reinterpret it.
        if (cp.feed_rate > 0) {
            run("F" + std::to_string(cp.feed_rate), out);
        }

        // Now hand the job back its own modal state.
        std::string restore = (cp.units == static_cast<uint8_t>(Units::Inches)) ? "G20" : "G21";
        restore += (cp.distance == static_cast<uint8_t>(Distance::Incremental)) ? " G91" : " G90";
        run(restore, out);

        file->set_position(cp.offset);
        log_info("Resuming " << cp.path << " at offset " << cp.offset);
        Job::save();
        Job::nest(file, &out);
        return Error::Ok;
    }
}
