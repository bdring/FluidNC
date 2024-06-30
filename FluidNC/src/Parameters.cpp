// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Parameters.h"
#include "Settings.h"
#include "Report.h"
#include "NutsBolts.h"
#include "System.h"
#include "Configuration/GCodeParam.h"
#include "Machine/MachineConfig.h"
#include "MotionControl.h"
#include "GCode.h"
#include "Job.h"

#include <string>
#include <map>

#include "Expression.h"

// clang-format off
const std::map<const int, bool *> bool_params = {
    { 5070, &probe_succeeded },
    // { 5399, &m66okay },
};
typedef int ngc_param_id_t;

std::map<const ngc_param_id_t, float> user_params = {};

const std::map<const ngc_param_id_t, CoordIndex> axis_params = {
    { 5161, CoordIndex::G28 },
    { 5181, CoordIndex::G30 },
    // { 5211, CoordIndex::G92 },  // Non-persisent, handled specially
    { 5221, CoordIndex::G54 },
    { 5241, CoordIndex::G55 },
    { 5261, CoordIndex::G56 },
    { 5281, CoordIndex::G57 },
    { 5301, CoordIndex::G58 },
    { 5321, CoordIndex::G59 },
    // { 5341, CoordIndex::G59_1 },  // Not implemented
    // { 5361, CoordIndex::G59_2 },  // Not implemented
    // { 5381, CoordIndex::G59_3 },  // Not implemented
    // { 5401, CoordIndex::TLO },
};


const std::map<const std::string, int> work_positions = {
    { "_x", 0 },
    { "_y", 1 },
    { "_z", 2 },
    { "_a", 3 },
    { "_b", 4 },
    { "_c", 5 },
    //    { "_u", 0},
    //    { "_v", 0},
    //    { "_w", 0},
};
const std::map<const std::string, int> machine_positions = {
    { "_abs_x", 0 },
    { "_abs_y", 1 },
    { "_abs_z", 2 },
    { "_abs_a", 3 },
    { "_abs_b", 4 },
    { "_abs_c", 5 },
    //    { "_abs_u", 0},
    //    { "_abs_v", 0},
    //    { "_abs_w", 0},
};

const std::array<const std::string, 6> unsupported_sys = {
    "_spindle_rpm_mode",
    "_spindle_css_mode",
    "_ijk_absolute_mode",
    "_lathe_diameter_mode",
    "_lathe_radius_mode",
    "_adaptive_feed",
};

// clang-format on

std::map<std::string, float> global_named_params;

bool ngc_param_is_rw(ngc_param_id_t id) {
    return true;
}

bool set_numbered_param(ngc_param_id_t id, float value) {
    for (auto const& [key, coord_index] : axis_params) {
        if (key <= id && id < (key + MAX_N_AXIS)) {
            coords[coord_index]->set(id - key, value);
            gc_ngc_changed(coord_index);
            return true;
        }
    }
    // Non-volatile G92
    if (id >= 5211 && id < (5211 + MAX_N_AXIS)) {
        gc_state.coord_offset[id - 5211] = value;
        gc_ngc_changed(CoordIndex::G92);
        return true;
    }
    if (id == 5220) {
        gc_state.modal.coord_select = static_cast<CoordIndex>(value);
        return true;
    }
    if (id == 5400) {
        gc_state.tool = static_cast<uint32_t>(value);
        return true;
    }
    if (id >= 31 && id <= 5000) {
        user_params[id] = value;
        return true;
    }
    log_info("N " << id << " is not found");
    return false;
}

bool get_numbered_param(ngc_param_id_t id, float& result) {
    for (auto const& [key, coord_index] : axis_params) {
        if (key <= id && id < (key + MAX_N_AXIS)) {
            const float* p = coords[coord_index]->get();
            result         = coords[coord_index]->get(id - key);
            return true;
        }
    }

    // last probe
    if (id >= 5061 && id < (5061 + MAX_N_AXIS)) {
        float probe_position[MAX_N_AXIS];
        motor_steps_to_mpos(probe_position, probe_steps);
        result = probe_position[id - 5061];
        return true;
    }

    // Non-volatile G92
    if (id >= 5211 && id < (5211 + MAX_N_AXIS)) {
        result = gc_state.coord_offset[id - 5211];
        return true;
    }

    if (id == 5220) {
        result = static_cast<float>(gc_state.modal.coord_select + 1);
        return true;
    }
    if (id == 5400) {
        result = static_cast<float>(gc_state.tool);
        return true;
    }

    // Current relative position in the active coordinate system including all offsets
    if (id >= 5420 && id < (5420 + MAX_N_AXIS)) {
        float* my_position = get_mpos();        
        mpos_to_wpos(my_position);
        result = my_position[id - 5420];
        return true;
    }

    for (const auto& [key, valuep] : bool_params) {
        if (key == id) {
            result = *valuep;
            return true;
        }
    }
    if (id >= 31 && id <= 5000) {
        result = user_params[id];
        return true;
    }

    return false;
}

struct param_ref_t {
    std::string    name;  // If non-empty, the parameter is named
    ngc_param_id_t id;    // Valid if name is empty
};
std::vector<std::tuple<param_ref_t, float>> assignments;

void set_config_item(const std::string& name, float result) {
    try {
        Configuration::GCodeParam gci(name.c_str(), result, false);
        config->group(gci);
    } catch (...) {}
}

bool get_config_item(const std::string& name, float& result) {
    try {
        Configuration::GCodeParam gci(name.c_str(), result, true);
        config->group(gci);

        if (gci.isHandled_) {
            return true;
        }
        log_debug(name << " is missing");
        return false;
    } catch (...) { return false; }
}

int coord_values[] = { 540, 550, 560, 570, 580, 590, 591, 592, 593 };

bool get_system_param(const std::string& name, float& result) {
    std::string sysn;
    for (auto const& c : name) {
        sysn += tolower(c);
    }
    if (auto search = work_positions.find(sysn); search != work_positions.end()) {
        result = get_mpos()[search->second] - get_wco()[search->second];
        return true;
    }
    if (auto search = machine_positions.find(sysn); search != machine_positions.end()) {
        result = get_mpos()[search->second];
        return true;
    }
    if (std::find(unsupported_sys.begin(), unsupported_sys.end(), sysn) != unsupported_sys.end()) {
        result = 0.0;
        return true;
    }
    if (sysn == "_spindle_on") {
        result = gc_state.modal.spindle != SpindleState::Disable;
        return true;
    }
    if (sysn == "_spindle_cw") {
        result = gc_state.modal.spindle == SpindleState::Cw;
        return true;
    }
    if (sysn == "_spindle_m") {
        result = static_cast<int>(gc_state.modal.spindle);
        return true;
    }

    if (sysn == "_mist") {
        result = gc_state.modal.coolant.Mist;
        return true;
    }
    if (sysn == "_flood") {
        result = gc_state.modal.coolant.Flood;
        return true;
    }
    if (sysn == "_speed_override") {
        result = sys.spindle_speed_ovr != 100;
        return true;
    }
    if (sysn == "_feed_override") {
        result = sys.f_override != 100;
        return true;
    }
    if (sysn == "_feed_hold") {
        result = sys.state == State::Hold;
        return true;
    }
    if (sysn == "_feed") {
        result = gc_state.feed_rate;
        return true;
    }
    if (sysn == "_rpm") {
        result = gc_state.spindle_speed;
        return true;
    }
    if (sysn == "_current_tool" || sysn == "_selected_tool") {
        result = gc_state.tool;
        return true;
    }
    if (sysn == "_vmajor") {
        std::string version(grbl_version);
        auto        major = version.substr(0, version.find('.'));
        result            = atoi(major.c_str());
        return true;
    }
    if (sysn == "_vminor") {
        std::string version(grbl_version);
        auto        minor = version.substr(version.find('.') + 1);

        result = atoi(minor.c_str());
        return true;
    }
    if (sysn == "_line") {
        //XXX Implement me
        return true;
    }
    if (sysn == "_motion_mode") {
        result = static_cast<gcodenum_t>(gc_state.modal.motion);
        return true;
    }
    if (sysn == "_plane") {
        result = static_cast<gcodenum_t>(gc_state.modal.plane_select);
        return true;
    }
#if 0
    if (sysn == "_ccomp") {
        result = static_cast<gcodenum_t>(gc_state.modal.cutter_comp);
        return true;
    }
#endif
    if (sysn == "_coord_system") {
        result = coord_values[gc_state.modal.coord_select];
        return true;
    }

    if (sysn == "_metric") {
        result = gc_state.modal.units == Units::Mm;
        return true;
    }
    if (sysn == "_imperial") {
        result = gc_state.modal.units == Units::Inches;
        return true;
    }
    if (sysn == "_absolute") {
        result = gc_state.modal.distance == Distance::Absolute;
        return true;
    }
    if (sysn == "_incremental") {
        result = gc_state.modal.distance == Distance::Incremental;
        return true;
    }
    if (sysn == "_inverse_time") {
        result = gc_state.modal.feed_rate == FeedRate::InverseTime;
        return true;
    }
    if (sysn == "_units_per_minute") {
        result = gc_state.modal.feed_rate == FeedRate::UnitsPerMin;
        return true;
    }
    if (sysn == "_units_per_rev") {
        // result = gc_state.modal.feed_rate == FeedRate::UnitsPerRev;
        result = 0.0;
        return true;
    }

    return false;
}

// The LinuxCNC doc says that the EXISTS syntax is like EXISTS[#<_foo>]
// For convenience, we also allow EXISTS[_foo]
bool named_param_exists(std::string& name) {
    std::string search;
    if (name.length() > 3 && name[0] == '#' && name[1] == '<' && name.back() == '>') {
        search = name.substr(2, name.length() - 3);
    } else {
        search = name;
    }
    if (search.length() == 0) {
        return false;
    }
    if (search[0] == '/') {
        float dummy;
        return get_config_item(search, dummy);
    }
    if (search[0] == '_') {
        float dummy;
        bool  got = get_system_param(search, dummy);
        if (got) {
            return true;
        }
        got = Job::param_exists(search);
        if (got) {
            return true;
        }
    }
    return global_named_params.count(search) != 0;
}

bool get_param(const param_ref_t& param_ref, float& result) {
    auto name = param_ref.name;
    if (name.length()) {
        if (name[0] == '/') {
            return get_config_item(name, result);
        }
        bool got;
        if (name[0] == '_') {
            got = get_system_param(name, result);
            if (got) {
                return true;
            }
            result = global_named_params[name];
            return true;
        }
        result = Job::active() ? Job::get_param(name) : global_named_params[name];
        return true;
    }
    return get_numbered_param(param_ref.id, result);
}

bool get_param_ref(const char* line, size_t* pos, param_ref_t& param_ref) {
    // Entry condition - the previous character was #
    char  c = line[*pos];
    float result;

    // c is the first character and *pos still points to it
    switch (c) {
        case '#': {
            // Indirection resulting in param number
            param_ref_t next_param_ref;
            ++*pos;
            if (!get_param_ref(line, pos, next_param_ref)) {
                return false;
            }
            if (!get_param(next_param_ref, result)) {
                return false;
            }
        }
            param_ref.id = result;
            return true;
        case '<':
            // Named parameter
            ++*pos;
            while ((c = line[*pos]) && c != '>') {
                ++*pos;
                param_ref.name += c;
            }
            if (!c) {
                return false;
            }
            ++*pos;
            return true;
        case '[': {
            // Expression evaluating to param number
            ++*pos;
            Error status = expression(line, pos, result);
            if (status != Error::Ok) {
                log_debug(errorString(status));
                return false;
            }
            param_ref.id = result;
            return true;
        }
        default:
            // Param number
            if (!read_float(line, pos, result)) {
                return false;
            }
            param_ref.id = result;
            return true;
    }
}

void set_param(const param_ref_t& param_ref, float value) {
    if (param_ref.name.length()) {
        auto name = param_ref.name;
        if (name[0] == '/') {
            set_config_item(param_ref.name, value);
            return;
        }
        if (name[0] != '_' && Job::active()) {
            Job::set_param(name, value);
        } else {
            global_named_params[name] = value;
        }
        return;
    }

    if (ngc_param_is_rw(param_ref.id)) {
        set_numbered_param(param_ref.id, value);
    }
}

// Gets a numeric value, either a literal number or a #-prefixed parameter value
bool read_number(const char* line, size_t* pos, float& result, bool in_expression) {
    char c = line[*pos];
    if (c == '#') {
        ++*pos;
        param_ref_t param_ref;
        if (!get_param_ref(line, pos, param_ref)) {
            return false;
        }
        return get_param(param_ref, result);
    }
    if (c == '[') {
        Error status = expression(line, pos, result);
        if (status != Error::Ok) {
            log_debug(errorString(status));
            return false;
        }
        return true;
    }
    if (in_expression) {
        if (isalpha(c)) {
            // Functions are available only inside expressions because
            // their names conflict with GCode words
            return read_unary(line, pos, result) == Error::Ok;
        }
        if (c == '-') {
            ++*pos;
            if (!read_number(line, pos, result, in_expression)) {
                return false;
            }
            result = -result;
            return true;
        }
        if (c == '+') {
            ++*pos;
            return read_number(line, pos, result, in_expression);
        }
    }
    return read_float(line, pos, result);
}

// Process a #PREF=value assignment, with the initial # already consumed
bool assign_param(const char* line, size_t* pos) {
    param_ref_t param_ref;

    if (!get_param_ref(line, pos, param_ref)) {
        return false;
    }
    if (line[*pos] != '=') {
        log_debug("Missing =");
        return false;
    }
    ++*pos;

    float value;
    if (!read_number(line, pos, value)) {
        log_debug("Missing value");
        return false;
    }
    assignments.emplace_back(param_ref, value);

    return true;
}

void perform_assignments() {
    for (auto const& [ref, value] : assignments) {
        set_param(ref, value);
    }
    assignments.clear();
}
