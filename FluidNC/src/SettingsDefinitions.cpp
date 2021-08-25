#include "SettingsDefinitions.h"
#include "Logging.h"

StringSetting* config_filename;

StringSetting* build_info;

IntSetting* status_mask;

EnumSetting* message_level;

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

    // GRBL Numbered Settings
    build_info = new StringSetting(EXTENDED, WG, NULL, "Firmware/Build", "");

    status_mask = new IntSetting(GRBL, WG, "10", "Report/Status", 1, 0, 3);

    message_level = new EnumSetting(NULL, EXTENDED, WG, NULL, "Message/Level", MsgLevelInfo, &messageLevels, NULL);

    config_filename = new StringSetting(EXTENDED, WG, NULL, "Config/Filename", "config.yaml");
}
