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

// See documentation "4.1. Numbered Parameters" for list of numbered parameters
// that LinuxCNC supports.
// http://wiki.fluidnc.com/en/features/gcode_parameters_expressions
// https://linuxcnc.org/docs/stable/html/gcode/overview.html#sub:numbered-parameters

// clang-format off
const std::map<const int, bool *> bool_params = {
    { 5070, &probe_succeeded },
};

std::map<const ngc_param_id_t, float> float_params = {
    { 5399, 0.0 }, // M66 last immediate read input result
};

static bool can_write_float_param(ngc_param_id_t id) {
    if (id == 5399) {
        // M66 last immediate read input result
        return true;
    }
    if(id >= 1 && id <= 5000) {
        // User parameters
        return true;
    }
    return false;
}

static bool can_read_float_param(ngc_param_id_t id) {
    if (id == 5399) {
        // M66
        return true;
    }
    if (id >= 31 && id <= 5000) {
        // User parameters
        return true;
    }
    return false;
}

const std::map<const ngc_param_id_t, CoordIndex> axis_params = {
    { 5161, CoordIndex::G28 },
    { 5181, CoordIndex::G30 },
    { 5211, CoordIndex::G92 },  // Non-persisent, handled specially
    { 5221, CoordIndex::G54 },
    { 5241, CoordIndex::G55 },
    { 5261, CoordIndex::G56 },
    { 5281, CoordIndex::G57 },
    { 5301, CoordIndex::G58 },
    { 5321, CoordIndex::G59 },
    { 5341, CoordIndex::G59_1 },
    { 5361, CoordIndex::G59_2 },
    { 5381, CoordIndex::G59_3 },
    { 5401, CoordIndex::TLO },
};

const std::map<const std::string, axis_t> work_positions = {
    { "_x", X_AXIS },
    { "_y", Y_AXIS },
    { "_z", Z_AXIS },
    { "_a", A_AXIS },
    { "_b", B_AXIS },
    { "_c", C_AXIS },
    { "_u", U_AXIS },
    { "_v", V_AXIS },
    { "_w", W_AXIS },
};
const std::map<const std::string, axis_t> machine_positions = {
    { "_abs_x", X_AXIS },
    { "_abs_y", Y_AXIS },
    { "_abs_z", Z_AXIS },
    { "_abs_a", A_AXIS },
    { "_abs_b", B_AXIS },
    { "_abs_c", C_AXIS },
    { "_abs_u", U_AXIS },
    { "_abs_v", V_AXIS },
    { "_abs_w", W_AXIS },
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

static bool is_axis(axis_t axis) {
    return axis >= 0 && axis < MAX_N_AXIS;
}
static float to_inches(axis_t axis, float value) {
    if (is_linear(axis) && gc_state.modal.units == Units::Inches) {
        return value * INCH_PER_MM;
    }
    return value;
}
static float to_mm(axis_t axis, float value) {
    if (is_linear(axis) && gc_state.modal.units == Units::Inches) {
        return value * MM_PER_INCH;
    }
    return value;
}

axis_t axis_from_id(ngc_param_id_t id, ngc_param_id_t index) {
    return static_cast<axis_t>(id - index);
}

bool get_numbered_param(ngc_param_id_t id, float& result) {
    axis_t axis;
    for (auto const& [key, coord_index] : axis_params) {
        axis = axis_from_id(id, key);
        if (is_axis(axis)) {
            if (coord_index == CoordIndex::G92) {  //special case non-volatile G92
                result = to_inches(axis, gc_state.coord_offset[axis]);
                return true;
            }
            result = to_inches(axis, coords[coord_index]->get(axis));
            return true;
        }
    }

    // last probe
    axis = axis_from_id(id, 5061);
    if (is_axis(axis)) {
        float probe_position[MAX_N_AXIS];
        steps_to_mpos(probe_position, probe_steps);
        result = to_inches(axis, probe_position[axis]);
        return true;
    }

    if (id == 5220) {
        result = static_cast<float>(gc_state.modal.coord_select + 1);
        return true;
    }
    if (id == 5400) {
        result = static_cast<float>(gc_state.selected_tool);
        return true;
    }

    // Current relative position in the active coordinate system including all offsets
    axis = axis_from_id(id, 5420);
    if (is_axis(axis)) {
        float* my_position = get_mpos();
        mpos_to_wpos(my_position);
        result = to_inches(axis, my_position[axis]);
        return true;
    }

    if (auto param = bool_params.find(id); param != bool_params.end()) {
        result = *param->second ? 1.0 : 0.0;
        return true;
    }

    if (can_read_float_param(id)) {
        if (auto param = float_params.find(id); param != float_params.end()) {
            result = param->second;
            return true;
        } else {
            log_info("param #" << id << " is not found");
            return false;
        }
    }

    return false;
}

// TODO - make this a variant?
struct param_ref_t {
    std::string    name;  // If non-empty, the parameter is named
    ngc_param_id_t id;    // Valid if name is empty
};
std::vector<std::tuple<param_ref_t, float>> assignments;

bool set_config_item(const std::string& name, float result) {
    try {
        Configuration::GCodeParam gci(name.c_str(), result, false);
        config->group(gci);
        if (gci.isHandled_) {
            return true;
        }
    } catch (std::exception& ex) {
        log_debug(ex.what());
        return false;
    }
    log_debug("Failed to set " << name);
    return false;
}

bool get_config_item(const std::string& name, float& result) {
    try {
        Configuration::GCodeParam gci(name.c_str(), result, true);
        config->group(gci);
        if (gci.isHandled_) {
            return true;
        }
    } catch (std::exception& ex) {
        log_debug(ex.what());
        return false;
    }
    return false;
}

uint32_t coord_values[] = { 540, 550, 560, 570, 580, 590, 591, 592, 593 };

bool get_system_param(const std::string& name, float& result) {
    std::string sysn;
    for (auto const& c : name) {
        sysn += ::tolower(c);
    }
    if (auto search = work_positions.find(sysn); search != work_positions.end()) {
        auto axis = search->second;
        result    = to_inches(axis, get_mpos()[axis] - get_wco()[axis]);
        return true;
    }
    if (auto search = machine_positions.find(sysn); search != machine_positions.end()) {
        auto axis = search->second;
        result    = to_inches(axis, get_mpos()[axis]);
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
        result = sys.spindle_speed_ovr() != 100;
        return true;
    }
    if (sysn == "_feed_override") {
        result = sys.f_override() != 100;
        return true;
    }
    if (sysn == "_feed_hold") {
        result = state_is(State::Hold);
        return true;
    }
    if (sysn == "_feed") {
        result = to_inches(X_AXIS, gc_state.feed_rate);
        return true;
    }
    if (sysn == "_rpm") {
        result = gc_state.spindle_speed;
        return true;
    }
    if (sysn == "_selected_tool") {
        result = gc_state.selected_tool;
        return true;
    }

    if (sysn == "_current_tool") {
        result = gc_state.current_tool;
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

bool system_param_exists(const std::string& name) {
    float dummy;
    return get_system_param(name, dummy);
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
        return global_named_params.count(search) != 0;
    }
    // If the name does not start with _ it is local so we look for a job-local parameter
    // If no job is active, we treat the interpretive context like a local context
    return Job::active() ? Job::param_exists(search) : global_named_params.count(search) != 0;
}

bool get_global_named_param(const std::string& name, float& value) {
    auto it = global_named_params.find(name);
    if (it == global_named_params.end()) {
        return false;
    }
    value = it->second;
    return true;
}

bool get_param(const param_ref_t& param_ref, float& value) {
    auto name = param_ref.name;
    if (name.length()) {
        if (name[0] == '/') {
            return get_config_item(name, value);
        }
        if (name[0] == '_') {
            if (get_system_param(name, value)) {
                return true;
            }
            return get_global_named_param(name, value);
        }
        return Job::active() ? Job::get_param(name, value) : get_global_named_param(name, value);
    }
    return get_numbered_param(param_ref.id, value);
}

bool get_param_ref(const char* line, size_t& pos, param_ref_t& param_ref) {
    // Entry condition - the previous character was #
    char  c = line[pos];
    float result;

    // c is the first character and pos still points to it
    switch (c) {
        case '#': {
            // Indirection resulting in param number
            param_ref_t next_param_ref;
            ++pos;
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
            ++pos;
            while ((c = line[pos]) && c != '>') {
                ++pos;
                if (!isspace(c)) {
                    param_ref.name += toupper(c);
                }
            }
            if (!c) {
                log_debug("Missing >");
                return false;
            }
            ++pos;
            return true;
        case '[': {
            // Expression evaluating to param number
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

bool set_named_param(const std::string& name, float value) {
    global_named_params[name] = value;
    return true;
}

bool set_numbered_param(ngc_param_id_t id, float value) {
    axis_t axis;
    for (auto const& [key, coord_index] : axis_params) {
        axis = axis_from_id(id, key);
        if (is_axis(axis)) {
            coords[coord_index]->set(axis, to_mm(axis, value));
            gc_ngc_changed(coord_index);
            return true;
        }
    }
    // Non-volatile G92
    axis = axis_from_id(id, 5211);
    if (is_axis(axis)) {
        gc_state.coord_offset[axis] = to_mm(axis, value);
        gc_ngc_changed(CoordIndex::G92);
        return true;
    }
    if (id == 5220) {
        gc_state.modal.coord_select = static_cast<CoordIndex>(value);
        return true;
    }
    if (id == 5400) {
        gc_state.selected_tool = static_cast<uint32_t>(value);
        return true;
    }
    if (can_write_float_param(id)) {
        float_params[id] = value;
        return true;
    }
    log_info("param #" << id << " is not found");
    return false;
}

bool set_param(const param_ref_t& param_ref, float value) {
    if (param_ref.name.length()) {  // Named parameter
        auto name = param_ref.name;
        if (name[0] == '/') {
            return set_config_item(param_ref.name, value);
        }
        if (name[0] != '_' && Job::active()) {
            return Job::set_param(name, value);
        }
        if (name[0] == '_' && system_param_exists(name)) {
            log_debug("Attempt to set read-only parameter " << name);
            return false;
        }
        return set_named_param(name, value);
    }

    if (ngc_param_is_rw(param_ref.id)) {  // Numbered parameter
        return set_numbered_param(param_ref.id, value);
    }
    log_debug("Attempt to set read-only parameter " << param_ref.id);
    return false;
}

bool read_number(const std::string_view sv, float& result, bool in_expression) {
    std::string s(sv);
    size_t      pos = 0;
    return read_number(s.c_str(), pos, result);
}

// Gets a numeric value, either a literal number or a #-prefixed parameter value
bool read_number(const char* line, size_t& pos, float& result, bool in_expression) {
    char c = line[pos];
    if (c == '#') {
        ++pos;
        param_ref_t param_ref;
        if (!get_param_ref(line, pos, param_ref)) {
            return false;
        }
        if (get_param(param_ref, result)) {
            return true;
        }
        log_debug("Undefined parameter " << param_ref.name);
        return false;
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
            ++pos;
            if (!read_number(line, pos, result, in_expression)) {
                return false;
            }
            result = -result;
            return true;
        }
        if (c == '+') {
            ++pos;
            return read_number(line, pos, result, in_expression);
        }
    }
    return read_float(line, pos, result);
}

// Process a #PREF=value assignment, with the initial # already consumed
bool assign_param(const char* line, size_t& pos) {
    param_ref_t param_ref;

    if (!get_param_ref(line, pos, param_ref)) {
        return false;
    }
    if (line[pos] != '=') {
        log_debug("Missing =");
        return false;
    }
    ++pos;

    float value;
    if (!read_number(line, pos, value)) {
        log_debug("Missing value");
        return false;
    }
    assignments.emplace_back(param_ref, value);

    return true;
}

bool perform_assignments() {
    bool result = true;
    for (auto const& [ref, value] : assignments) {
        if (!set_param(ref, value)) {
            result = false;
        }
    }
    assignments.clear();
    return result;
}
