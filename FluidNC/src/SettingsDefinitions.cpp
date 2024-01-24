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

    message_level = new EnumSetting("Which Messages", EXTENDED, WG, NULL, "Message/Level", MsgLevelInfo, &messageLevels, NULL);

    config_filename = new StringSetting("Name of Configuration File", EXTENDED, WG, NULL, "Config/Filename", "config.yaml", 1, 50, NULL);

    // GRBL Numbered Settings
    status_mask = new IntSetting("What to include in status report", GRBL, WG, "10", "Report/Status", 1, 0, 3, NULL);

    sd_fallback_cs = new IntSetting("SD CS pin if not configured", EXTENDED, WG, NULL, "SD/FallbackCS", -1, -1, 40, NULL);

    build_info = new StringSetting("OEM build info for $I command", EXTENDED, WG, NULL, "Firmware/Build", "", 0, 20, NULL);

    start_message =
        new StringSetting("Message issued at startup", EXTENDED, WG, NULL, "Start/Message", "Grbl \\V [FluidNC \\B (\\R) \\H]", 0, 40, NULL);

    // Some gcode senders expect Grbl to report certain numbered settings to improve
    // their reporting. The following macros set up various legacy numbered Grbl settings,
    // which are derived from MachineConfig settings.

    // 130-132: Max travel (mm)
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "130", "Grbl/MaxTravel/X", [](MachineConfig const& config) { return config._axes->_axis[0]->_maxTravel; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "131", "Grbl/MaxTravel/Y", [](MachineConfig const& config) { return config._axes->_axis[1]->_maxTravel; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "132", "Grbl/MaxTravel/Z", [](MachineConfig const& config) { return config._axes->_axis[2]->_maxTravel; }));

    // 120-122: Acceleration (mm/sec^2)
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "120", "Grbl/Acceleration/X", [](MachineConfig const& config) { return config._axes->_axis[0]->_acceleration; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "121", "Grbl/Acceleration/Y", [](MachineConfig const& config) { return config._axes->_axis[1]->_acceleration; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "122", "Grbl/Acceleration/Z", [](MachineConfig const& config) { return config._axes->_axis[2]->_acceleration; }));

    // 110-112: Max rate (mm/min)
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "110", "Grbl/MaxRate/X", [](MachineConfig const& config) { return config._axes->_axis[0]->_maxRate; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "111", "Grbl/MaxRate/Y", [](MachineConfig const& config) { return config._axes->_axis[1]->_maxRate; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "112", "Grbl/MaxRate/Z", [](MachineConfig const& config) { return config._axes->_axis[2]->_maxRate; }));

    // 100-102: Resolution (steps/mm)
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "100", "Grbl/Resolution/X", [](MachineConfig const& config) { return config._axes->_axis[0]->_stepsPerMm; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "101", "Grbl/Resolution/Y", [](MachineConfig const& config) { return config._axes->_axis[1]->_stepsPerMm; }));
    float_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<float>>(
        "102", "Grbl/Resolution/Z", [](MachineConfig const& config) { return config._axes->_axis[2]->_stepsPerMm; }));

    int_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<int32_t>>(
        "20", "Grbl/SoftLimitsEnable", [](MachineConfig const& config) { return config._axes->_axis[0]->_softLimits ? 1 : 0; }));
    int_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<int32_t>>(
        "21", "Grbl/HardLimitsEnable", [](MachineConfig const& config) { return config._axes->hasHardLimits() ? 1 : 0; }));
    int_proxies.emplace_back(std::make_unique<MachineConfigProxySetting<int32_t>>(
        "22", "Grbl/HomingCycleEnable", [](MachineConfig const& config) { return Axes::homingMask ? 1 : 0; }));
}
