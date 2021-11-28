// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#include "Serial.h"
#include "Error.h"
#include "System.h"
#include "SpindleDatatypes.h"

#include <cstdint>

// Modal group internal numbers for checking multiple command violations and tracking the
// type of command that is called in the block. A modal group is a group of g-code commands that are
// mutually exclusive, or cannot exist on the same line, because they each toggle a state or execute
// a unique motion. These are defined in the NIST RS274-NGC v3 g-code standard, available online,
// and are similar/identical to other g-code interpreters by manufacturers (Haas,Fanuc,Mazak,etc).
// NOTE: Modal group values must be sequential and starting from zero.

enum class ModalGroup : uint8_t {
    MG0  = 0,   // [G4,G10,G28,G28.1,G30,G30.1,G53,G92,G92.1] Non-modal
    MG1  = 1,   // [G0,G1,G2,G3,G38.2,G38.3,G38.4,G38.5,G80] Motion
    MG2  = 2,   // [G17,G18,G19] Plane selection
    MG3  = 3,   // [G90,G91] Distance mode
    MG4  = 4,   // [G91.1] Arc IJK distance mode
    MG5  = 5,   // [G93,G94] Feed rate mode
    MG6  = 6,   // [G20,G21] Units
    MG7  = 7,   // [G40] Cutter radius compensation mode. G41/42 NOT SUPPORTED.
    MG8  = 8,   // [G43.1,G49] Tool length offset
    MG12 = 9,   // [G54,G55,G56,G57,G58,G59] Coordinate system selection
    MG13 = 10,  // [G61] Control mode
    MM4  = 11,  // [M0,M1,M2,M30] Stopping
    MM6  = 14,  // [M6] Tool change
    MM7  = 12,  // [M3,M4,M5] Spindle turning
    MM8  = 13,  // [M7,M8,M9] Coolant control
    MM9  = 14,  // [M56] Override control
    MM10 = 15,  // [M62, M63, M64, M65, M67, M68] User Defined http://linuxcnc.org/docs/html/gcode/overview.html#_modal_groups
};

// Command actions for within execution-type modal groups (motion, stopping, non-modal). Used
// internally by the parser to know which command to execute.
// NOTE: Some macro values are assigned specific values to make g-code state reporting and parsing
// compile a litte smaller. Necessary due to being completely out of flash on the 328p. Although not
// ideal, just be careful with values that state 'do not alter' and check both report.c and gcode.c
// to see how they are used, if you need to alter them.

// Modal Group G0: Non-modal actions

enum class NonModal : uint8_t {
    NoAction              = 0,    // (Default: Must be zero)
    Dwell                 = 4,    // G4 (Do not alter value)
    SetCoordinateData     = 10,   // G10 (Do not alter value)
    GoHome0               = 28,   // G28 (Do not alter value)
    SetHome0              = 38,   // G28.1 (Do not alter value)
    GoHome1               = 30,   // G30 (Do not alter value)
    SetHome1              = 40,   // G30.1 (Do not alter value)
    AbsoluteOverride      = 53,   // G53 (Do not alter value)
    SetCoordinateOffset   = 92,   // G92 (Do not alter value)
    ResetCoordinateOffset = 102,  //G92.1 (Do not alter value)
};

// Modal Group G1: Motion modes
enum class Motion : uint8_t {
    Seek               = 0,    // G0 (Default: Must be zero)
    Linear             = 1,    // G1 (Do not alter value)
    CwArc              = 2,    // G2 (Do not alter value)
    CcwArc             = 3,    // G3 (Do not alter value)
    ProbeToward        = 140,  // G38.2 (Do not alter value)
    ProbeTowardNoError = 141,  // G38.3 (Do not alter value)
    ProbeAway          = 142,  // G38.4 (Do not alter value)
    ProbeAwayNoError   = 143,  // G38.5 (Do not alter value)
    None               = 80,   // G80 (Do not alter value)
};

// Modal Group G2: Plane select
enum class Plane : uint8_t {
    XY = 0,  // G17 (Default: Must be zero)
    ZX = 1,  // G18 (Do not alter value)
    YZ = 2,  // G19 (Do not alter value)
};

// Modal Group G3: Distance mode
enum class Distance : uint8_t {
    Absolute    = 0,  // G90 (Default: Must be zero)
    Incremental = 1,  // G91 (Do not alter value)
};

// Modal Group G4: Arc IJK distance mode
enum class ArcDistance : uint8_t {
    Incremental = 0,  // G91.1 (Default: Must be zero)
    Absolute    = 1,
};

// Modal Group M4: Program flow
enum class ProgramFlow : uint8_t {
    Running      = 0,   // (Default: Must be zero)
    Paused       = 3,   // M0
    OptionalStop = 1,   // M1 NOTE: Not supported, but valid and ignored.
    CompletedM2  = 2,   // M2 (Do not alter value)
    CompletedM30 = 30,  // M30 (Do not alter value)
};

// Modal Group G5: Feed rate mode
enum class FeedRate : uint8_t {
    UnitsPerMin = 0,  // G94 (Default: Must be zero)
    InverseTime = 1,  // G93 (Do not alter value)
};

// Modal Group G6: Units mode
enum class Units : uint8_t {
    Mm     = 0,  // G21 (Default: Must be zero)
    Inches = 1,  // G20 (Do not alter value)
};

// Modal Group G7: Cutter radius compensation mode
enum class CutterCompensation : uint8_t {
    Disable = 0,  // G40 (Default: Must be zero)
    Enable  = 1,
};

// Modal Group G13: Control mode
enum class ControlMode : uint8_t {
    ExactPath = 0,  // G61 (Default: Must be zero)
};

// GCodeCoolant is used by the parser, where at most one of
// M7, M8, M9 may be present in a GCode block
enum class GCodeCoolant : uint8_t {
    None = 0,
    M7,
    M8,
    M9,
};

// CoolantState is used for the runtime state, where either of
// the Mist and Flood state bits can be set independently.
// Unlike GCode, overrides permit individual turn-off.
struct CoolantState {
    uint8_t Mist : 1;
    uint8_t Flood : 1;
};

// Modal Group M8: Coolant control
// Modal Group M9: Override control

// Modal Group M10: User I/O control
enum class IoControl : uint8_t {
    DigitalOnSync       = 1,  // M62
    DigitalOffSync      = 2,  // M63
    DigitalOnImmediate  = 3,  // M64
    DigitalOffImmediate = 4,  // M65
    SetAnalogSync       = 5,  // M67
    SetAnalogImmediate  = 6,  // M68
};

static const int MaxUserDigitalPin = 4;
static const int MaxUserAnalogPin  = 4;

// Modal Group G8: Tool length offset
enum class ToolLengthOffset : uint8_t {
    Cancel        = 0,  // G49 (Default: Must be zero)
    EnableDynamic = 1,  // G43.1
};

enum class ToolChange : uint8_t {
    Disable = 0,
    Enable  = 1,
};

// Modal Group G12: Active work coordinate system
// N/A: Stores coordinate system value (54-59) to change to.

// Parameter word mapping.
enum class GCodeWord : uint8_t {
    E = 0,
    F = 1,
    I = 2,
    J = 3,
    K = 4,
    L = 5,
    N = 6,
    P = 7,
    Q = 8,
    R = 9,
    S = 10,
    T = 11,
    X = 12,
    Y = 13,
    Z = 14,
    A = 15,
    B = 16,
    C = 17,
};

// GCode parser position updating flags
enum class GCUpdatePos : uint8_t {
    Target = 0,  // Must be zero
    System = 1,
    None   = 2,
};

// GCode parser flags for handling special cases.
enum GCParserFlags {
    GCParserNone           = 0,  // Must be zero.
    GCParserJogMotion      = bitnum_to_mask(0),
    GCParserCheckMantissa  = bitnum_to_mask(1),
    GCParserArcIsClockwise = bitnum_to_mask(2),
    GCParserProbeIsAway    = bitnum_to_mask(3),
    GCParserProbeIsNoError = bitnum_to_mask(4),
    GCParserLaserForceSync = bitnum_to_mask(5),
    GCParserLaserDisable   = bitnum_to_mask(6),  // disable laser when motion stops
    GCParserLaserIsMotion  = bitnum_to_mask(7),  //
};

// Various places in the code access saved coordinate system data
// by a small integer index according to the values below.
enum CoordIndex : uint8_t {
    Begin = 0,
    G54   = Begin,
    G55,
    G56,
    G57,
    G58,
    G59,
    // To support 9 work coordinate systems it would be necessary to define
    // the following 3 and modify GCode.cpp to support G59.1, G59.2, G59.3
    // G59_1,
    // G59_2,
    // G59_3,
    NWCSystems,
    G28 = NWCSystems,
    G30,
    // G92_2,
    // G92_3,
    End,
};

// Allow iteration over CoordIndex values
CoordIndex& operator++(CoordIndex& i);

// NOTE: When this struct is zeroed, the 0 values in the above types set the system defaults.
struct gc_modal_t {
    Motion   motion;     // {G0,G1,G2,G3,G38.2,G80}
    FeedRate feed_rate;  // {G93,G94}
    Units    units;      // {G20,G21}
    Distance distance;   // {G90,G91}
    // ArcDistance distance_arc; // {G91.1} NOTE: Don't track. Only default supported.
    Plane plane_select;  // {G17,G18,G19}
    // CutterCompensation cutter_comp;  // {G40} NOTE: Don't track. Only default supported.
    ToolLengthOffset tool_length;   // {G43.1,G49}
    CoordIndex       coord_select;  // {G54,G55,G56,G57,G58,G59}
    // uint8_t control;      // {G61} NOTE: Don't track. Only default supported.
    ProgramFlow  program_flow;  // {M0,M1,M2,M30}
    CoolantState coolant;       // {M7,M8,M9}
    SpindleState spindle;       // {M3,M4,M5}
    ToolChange   tool_change;   // {M6}
    IoControl    io_control;    // {M62, M63, M67}
    Override     override;      // {M56}
};

struct gc_values_t {
    uint8_t e;                // M67
    float   f;                // Feed
    float   ijk[3];           // I,J,K Axis arc offsets - only 3 are possible
    uint8_t l;                // G10 or canned cycles parameters
    int32_t n;                // Line number
    float   p;                // G10 or dwell parameters
    float   q;                // M67
    float   r;                // Arc radius
    float   s;                // Spindle speed
    uint8_t t;                // Tool selection
    float   xyz[MAX_N_AXIS];  // X,Y,Z Translational axes
};

struct parser_state_t {
    gc_modal_t modal;

    float   spindle_speed;  // RPM
    float   feed_rate;      // Millimeters/min
    uint8_t tool;           // Tracks tool number. NOT USED.
    int32_t line_number;    // Last line number sent

    float position[MAX_N_AXIS];  // Where the interpreter considers the tool to be at this point in the code

    float coord_system[MAX_N_AXIS];  // Current work coordinate system (G54+). Stores offset from absolute machine
    // position in mm. Loaded from non-volatile storage when called.
    float coord_offset[MAX_N_AXIS];  // Retains the G92 coordinate offset (work coordinates) relative to
    // machine zero in mm. Non-persistent. Cleared upon reset and boot.
    float tool_length_offset;  // Tracks tool length offset value when enabled.
};

extern parser_state_t gc_state;

struct parser_block_t {
    NonModal     non_modal_command;
    gc_modal_t   modal;
    gc_values_t  values;
    GCodeCoolant coolant;
};

enum class AxisCommand : uint8_t {
    None             = 0,
    NonModal         = 1,
    MotionMode       = 2,
    ToolLengthOffset = 3,
};

// Initialize the parser
void gc_init();

// Execute one block of rs275/ngc/g-code
Error gc_execute_line(char* line, Channel& channel);

// Set g-code parser position. Input in steps.
void gc_sync_position();

void user_tool_change(uint8_t new_tool);
void user_m30();
