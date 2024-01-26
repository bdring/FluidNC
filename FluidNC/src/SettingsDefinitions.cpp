#include "Machine/MachineConfig.h"
#include "SettingsDefinitions.h"
#include "Config.h"

#include <tuple>
#include <array>
#include <memory>

StringSetting* config_filename;

StringSetting* build_info;

StringSetting* start_message;

IntSetting* status_mask;

IntSetting* sd_fallback_cs;

EnumSetting* message_level;

std::vector<std::unique_ptr<MachineConfigProxySetting<float>>>   float_proxies;
std::vector<std::unique_ptr<MachineConfigProxySetting<int32_t>>> int_proxies;

enum_opt_t messageLevels = {
    // clang-format off
    { "None", MsgLevelNone },
    { "Error", MsgLevelError },
    { "Warning", MsgLevelWarning },
    { "Info", MsgLevelInfo },
    { "Debug", MsgLevelDebug },
    { "Verbose", MsgLevelVerbose },
    // clang-format on
};

enum_opt_t onoffOptions = { { "OFF", 0 }, { "ON", 1 } };

void make_coordinate(CoordIndex index, const char* name) {
    float coord_data[MAX_N_AXIS] = { 0.0 };
    auto  coord                  = new Coordinates(name);
    coords[index]                = coord;
    if (!coord->load()) {
        coords[index]->setDefault();
    }
}

#define FLOAT_PROXY(number, name, configvar)                                                                                               \
    float_proxies.emplace_back(                                                                                                            \
        std::make_unique<MachineConfigProxySetting<float>>(number, name, [](MachineConfig const& config) { return configvar; }));

#define INT_PROXY(number, name, configvar)                                                                                                 \
    int_proxies.emplace_back(                                                                                                              \
        std::make_unique<MachineConfigProxySetting<int>>(number, name, [](MachineConfig const& config) { return configvar; }));

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

    start_message = new StringSetting("Message issued at startup", EXTENDED, WG, NULL, "Start/Message", "Grbl \\V [FluidNC \\B (\\R) \\H]", 0, 40);

    // Some gcode senders expect Grbl to report certain numbered settings to improve
    // their reporting. The following macros set up various legacy numbered Grbl settings,
    // which are derived from MachineConfig settings.

    // 130-132: Max travel (mm)
    FLOAT_PROXY("130", "Grbl/MaxTravel/X", config._axes->_axis[0]->_maxTravel)
    FLOAT_PROXY("131", "Grbl/MaxTravel/Y", config._axes->_axis[1]->_maxTravel)
    FLOAT_PROXY("132", "Grbl/MaxTravel/Z", config._axes->_axis[2]->_maxTravel)

    // 120-122: Acceleration (mm/sec^2)
    FLOAT_PROXY("120", "Grbl/Acceleration/X", config._axes->_axis[0]->_acceleration)
    FLOAT_PROXY("121", "Grbl/Acceleration/Y", config._axes->_axis[1]->_acceleration)
    FLOAT_PROXY("122", "Grbl/Acceleration/Z", config._axes->_axis[2]->_acceleration)

    // 110-112: Max rate (mm/min)
    FLOAT_PROXY("110", "Grbl/MaxRate/X", config._axes->_axis[0]->_maxRate)
    FLOAT_PROXY("111", "Grbl/MaxRate/Y", config._axes->_axis[1]->_maxRate)
    FLOAT_PROXY("112", "Grbl/MaxRate/Z", config._axes->_axis[2]->_maxRate)

    // 100-102: Resolution (steps/mm)
    FLOAT_PROXY("100", "Grbl/Resolution/X", config._axes->_axis[0]->_stepsPerMm)
    FLOAT_PROXY("101", "Grbl/Resolution/Y", config._axes->_axis[1]->_stepsPerMm)
    FLOAT_PROXY("102", "Grbl/Resolution/Z", config._axes->_axis[2]->_stepsPerMm)

    INT_PROXY("20", "Grbl/SoftLimitsEnable", config._axes->_axis[0]->_softLimits)
    INT_PROXY("21", "Grbl/HardLimitsEnable", config._axes->hasHardLimits())
    INT_PROXY("22", "Grbl/HomingCycleEnable", (bool)Axes::homingMask)
}
