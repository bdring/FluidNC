#include "Machine/MachineConfig.h"
#include "SettingsDefinitions.h"
#include "Config.h"
#include "Stepping.h"
#include "Machine/Homing.h"

#include <tuple>
#include <array>
#include <memory>

StringSetting* config_filename;

StringSetting* build_info;

StringSetting* start_message;

IntSetting* status_mask;

IntSetting* sd_fallback_cs;

EnumSetting* message_level;

const enum_opt_t messageLevels = {
    // clang-format off
    { "None", MsgLevelNone },
    { "Error", MsgLevelError },
    { "Warning", MsgLevelWarning },
    { "Info", MsgLevelInfo },
    { "Debug", MsgLevelDebug },
    { "Verbose", MsgLevelVerbose },
    // clang-format on
};

const enum_opt_t onoffOptions = { { "OFF", 0 }, { "ON", 1 } };

EnumSetting* gcode_echo;

void make_coordinate(CoordIndex index, const char* name) {
    float coord_data[MAX_N_AXIS] = { 0.0 };
    auto  coord                  = new Coordinates(name);
    coords[index]                = coord;
    if (!coord->load()) {
        coords[index]->setDefault();
    }
}

void float_proxy(int axis, int grbl_number, const char* name, float* varp) {
    // The two strings allocated below are intentionally not freed
    char* grbl_name = new char[4];
    snprintf(grbl_name, 4, "%d", grbl_number + axis);

    char* fluidnc_name = new char[strlen(name) + 2];
    sprintf(fluidnc_name, "%s%c", name, Axes::axisName(axis));

    // Creation of any setting inserts it into the settings list, so we
    // do not need to keep the pointer here
    auto proxy = new FloatProxySetting(grbl_name, fluidnc_name, varp);
}

#define INT_PROXY(number, name, configvar)                                                                                                 \
    {                                                                                                                                      \
        auto dummy = new IntProxySetting(number, name, [](MachineConfig const& config) { return configvar; });                             \
    }

void make_settings() {
    Setting::init();

    // Propagate old coordinate system data to the new format if necessary.
    // G54 - G59 work coordinate systems, G28, G30 reference positions, etc
    make_coordinate(CoordIndex::G54, "G54");
    make_coordinate(CoordIndex::G55, "G55");
    make_coordinate(CoordIndex::G56, "G56");
    make_coordinate(CoordIndex::G57, "G57");
    make_coordinate(CoordIndex::G58, "G58");
    make_coordinate(CoordIndex::G59, "G59");
    make_coordinate(CoordIndex::G28, "G28");
    make_coordinate(CoordIndex::G30, "G30");
    make_coordinate(CoordIndex::G92, "G92");
    make_coordinate(CoordIndex::TLO, "TLO");

    message_level = new EnumSetting("Which Messages", EXTENDED, WG, NULL, "Message/Level", MsgLevelInfo, &messageLevels);

    config_filename = new StringSetting("Name of Configuration File", EXTENDED, WG, NULL, "Config/Filename", "config.yaml", 1, 50);

    // GRBL Numbered Settings
    status_mask = new IntSetting("What to include in status report", GRBL, WG, "10", "Report/Status", 1, 0, 3);

    sd_fallback_cs = new IntSetting("SD CS pin if not configured", EXTENDED, WG, NULL, "SD/FallbackCS", -1, -1, 40);

    build_info = new StringSetting("OEM build info for $I command", EXTENDED, WG, NULL, "Firmware/Build", "", 0, 20);

    start_message =
        new StringSetting("Message issued at startup", EXTENDED, WG, NULL, "Start/Message", "Grbl \\V [FluidNC \\B (\\R) \\H]", 0, 40);

    gcode_echo = new EnumSetting("GCode Echo Enable", WEBSET, WG, NULL, "GCode/Echo", 0, &onoffOptions);
}

void make_proxies() {
    // Some gcode senders expect Grbl to report certain numbered settings to improve
    // their reporting. The following macros set up various legacy numbered Grbl settings,
    // which are derived from MachineConfig settings.

    // We do this with multiple loops so the setting numbers are displayed in the expected order
    auto n_axis = Axes::_numberAxis;
    for (int axis = n_axis - 1; axis >= 0; --axis) {
        float_proxy(axis, 130, "Grbl/MaxTravel/", &(config->_axes->_axis[axis]->_maxTravel));
    }

    for (int axis = n_axis - 1; axis >= 0; --axis) {
        float_proxy(axis, 120, "Grbl/Acceleration/", &(config->_axes->_axis[axis]->_acceleration));
    }

    for (int axis = n_axis - 1; axis >= 0; --axis) {
        float_proxy(axis, 110, "Grbl/MaxRate/", &(config->_axes->_axis[axis]->_maxRate));
    }

    for (int axis = n_axis - 1; axis >= 0; --axis) {
        float_proxy(axis, 100, "Grbl/Resolution/", &(config->_axes->_axis[axis]->_stepsPerMm));
    }

    INT_PROXY("32", "Grbl/LaserMode", spindle->isRateAdjusted())

    INT_PROXY("30", "Grbl/MaxSpindleSpeed", spindle->maxSpeed())

    INT_PROXY("23", "Grbl/HomingDirections", Homing::direction_mask);
    INT_PROXY("22", "Grbl/HomingCycleEnable", (bool)Axes::homingMask);
    INT_PROXY("21", "Grbl/HardLimitsEnable", config._axes->hasHardLimits());
    INT_PROXY("20", "Grbl/SoftLimitsEnable", config._axes->_axis[0]->_softLimits);
}
