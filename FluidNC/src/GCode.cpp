// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RS274/NGC parser.

#include "GCode.h"
#include "Settings.h"
#include "Config.h"
#include "Report.h"
#include "Jog.h"
#include "Protocol.h"             // protocol_buffer_synchronize
#include "MotionControl.h"        // mc_override_ctrl_update
#include "Machine/UserOutputs.h"  // setAnalogPercent
#include "Platform.h"             // WEAK_LINK

#include "Machine/MachineConfig.h"

#include <string.h>  // memset
#include <math.h>    // sqrt etc.

// Allow iteration over CoordIndex values
CoordIndex& operator++(CoordIndex& i) {
    i = static_cast<CoordIndex>(static_cast<size_t>(i) + 1);
    return i;
}

// NOTE: Max line number is defined by the g-code standard to be 99999. It seems to be an
// arbitrary value, and some GUIs may require more. So we increased it based on a max safe
// value when converting a float (7.2 digit precision)s to an integer.
static const int32_t MaxLineNumber = 10000000;

// Declare gc extern struct
parser_state_t gc_state;
parser_block_t gc_block;

#define FAIL(status) return (status);

void gc_init() {
    // Reset parser state:
    memset(&gc_state, 0, sizeof(parser_state_t));

    // Load default G54 coordinate system.
    gc_state.modal.coord_select = CoordIndex::G54;
    gc_state.modal.override     = config->_start->_deactivateParking ? Override::Disabled : Override::ParkingMotion;
    coords[gc_state.modal.coord_select]->get(gc_state.coord_system);
}

// Sets g-code parser position in mm. Input in steps. Called by the system abort and hard
// limit pull-off routines.
void gc_sync_position() {
    motor_steps_to_mpos(gc_state.position, get_motor_steps());
}

static void gcode_comment_msg(char* comment) {
    char         msg[80];
    const size_t offset = 4;  // ignore "MSG_" part of comment
    size_t       index  = offset;
    if (strstr(comment, "MSG")) {
        while (index < strlen(comment)) {
            msg[index - offset] = comment[index];
            index++;
        }
        msg[index - offset] = 0;  // null terminate
        log_info("GCode Comment..." << msg);
    }
}

// Edit GCode line in-place, removing whitespace and comments and
// converting to uppercase
void collapseGCode(char* line) {
    // parenPtr, if non-NULL, is the address of the character after (
    char* parenPtr = NULL;
    // outPtr is the address where newly-processed characters will be placed.
    // outPtr is alway less than or equal to inPtr.
    char* outPtr = line;
    char  c;
    for (char* inPtr = line; (c = *inPtr) != '\0'; inPtr++) {
        if (isspace(c)) {
            continue;
        }
        switch (c) {
            case ')':
                if (parenPtr) {
                    // Terminate comment by replacing ) with NUL
                    *inPtr = '\0';
                    gcode_comment_msg(parenPtr);
                    parenPtr = NULL;
                }
                // Strip out ) that does not follow a (
                break;
            case '(':
                // Start the comment at the character after (
                parenPtr = inPtr + 1;
                break;
            case ';':
                // NOTE: ';' comment to EOL is a LinuxCNC definition. Not NIST.
                // gcode_comment_msg(inPtr + 1);
                *outPtr = '\0';
                return;
            case '%':
                // TODO: Install '%' feature
                // Program start-end percent sign NOT SUPPORTED.
                // NOTE: This may be installed to distinguish between program running vs manual input,
                // where, during a program, the system auto-cycle start will continue to execute
                // everything until the next '%' sign. This will help fix resuming issues with certain
                // functions that empty the planner buffer to execute its task on-time.
                break;
            case '\r':
                // In case one sneaks in
                break;
            default:
                if (!parenPtr) {
                    *outPtr++ = toupper(c);  // make upper case
                }
        }
    }
    // On loop exit, *inPtr is '\0'
    if (parenPtr) {
        // Handle unterminated ( comments
        gcode_comment_msg(parenPtr);
    }
    *outPtr = '\0';
}

static void gc_ngc_changed(CoordIndex coord) {
    allChannels.notifyNgc(coord);
}

static void gc_wco_changed() {
    if (FORCE_BUFFER_SYNC_DURING_WCO_CHANGE) {
        protocol_buffer_synchronize();
    }
    allChannels.notifyWco();
}

// Executes one line of NUL-terminated G-Code.
// The line may contain whitespace and comments, which are first removed,
// and lower case characters, which are converted to upper case.
// In this function, all units and positions are converted and
// exported to internal functions in terms of (mm, mm/min) and absolute machine
// coordinates, respectively.
Error gc_execute_line(char* line) {
    // Step 0 - remove whitespace and comments and convert to upper case
    collapseGCode(line);

    /* -------------------------------------------------------------------------------------
       STEP 1: Initialize parser block struct and copy current g-code state modes. The parser
       updates these modes and commands as the block line is parser and will only be used and
       executed after successful error-checking. The parser block struct also contains a block
       values struct, word tracking variables, and a non-modal commands tracker for the new
       block. This struct contains all of the necessary information to execute the block. */
    memset(&gc_block, 0, sizeof(parser_block_t));                  // Initialize the parser block struct.
    memcpy(&gc_block.modal, &gc_state.modal, sizeof(gc_modal_t));  // Copy current modes
    AxisCommand axis_command = AxisCommand::None;
    size_t      axis_0, axis_1, axis_linear;
    CoordIndex  coord_select = CoordIndex::G54;  // Tracks G10 P coordinate selection for execution
    // Initialize bitflag tracking variables for axis indices compatible operations.
    size_t axis_words = 0;  // XYZ tracking
    size_t ijk_words  = 0;  // IJK tracking
    // Initialize command and value words and parser flags variables.
    uint32_t command_words = 0;  // Tracks G and M command words. Also used for modal group violations.
    uint32_t value_words   = 0;  // Tracks value words.

    bool jogMotion     = false;
    bool checkMantissa = false;
    bool clockwiseArc  = false;
    bool probeExplicit = false;
    bool probeAway     = false;
    bool probeNoError  = false;
    bool syncLaser     = false;
    bool disableLaser  = false;
    bool laserIsMotion = false;
    bool nonmodalG38   = false;  // Used for G38.6-9

    auto    n_axis = config->_axes->_numberAxis;
    float   coord_data[MAX_N_AXIS];  // Used by WCO-related commands
    uint8_t pValue;                  // Integer value of P word

    // Determine if the line is a jogging motion or a normal g-code block.
    if (line[0] == '$') {  // NOTE: `$J=` already parsed when passed to this function.
        // Set G1 and G94 enforced modes to ensure accurate error checks.
        jogMotion                = true;
        gc_block.modal.motion    = Motion::Linear;
        gc_block.modal.feed_rate = FeedRate::UnitsPerMin;
        if (config->_useLineNumbers) {
            gc_block.values.n = JOG_LINE_NUMBER;  // Initialize default line number reported during jog.
        }
    }

    /* -------------------------------------------------------------------------------------
       STEP 2: Import all g-code words in the block line. A g-code word is a letter followed by
       a number, which can either be a 'G'/'M' command or sets/assigns a command value. Also,
       perform initial error-checks for command word modal group violations, for any repeated
       words, and for negative values set for the value words F, N, P, T, and S. */
    ModalGroup mg_word_bit;  // Bit-value for assigning tracking variables
    uint32_t   bitmask = 0;
    size_t     char_counter;
    char       letter;
    float      value;
    uint8_t    int_value = 0;
    uint16_t   mantissa  = 0;
    char_counter         = jogMotion ? 3 : 0;  // Start parsing after `$J=` if jogging
    while (line[char_counter] != 0) {          // Loop until no more g-code words in line.
        // Import the next g-code word, expecting a letter followed by a value. Otherwise, error out.
        letter = line[char_counter];
        if ((letter < 'A') || (letter > 'Z')) {
            FAIL(Error::ExpectedCommandLetter);  // [Expected word letter]
        }
        char_counter++;
        if (!read_float(line, &char_counter, &value)) {
            FAIL(Error::BadNumberFormat);  // [Expected word value]
        }
        // Convert values to smaller uint8 significand and mantissa values for parsing this word.
        // NOTE: Mantissa is multiplied by 100 to catch non-integer command values. This is more
        // accurate than the NIST gcode requirement of x10 when used for commands, but not quite
        // accurate enough for value words that require integers to within 0.0001. This should be
        // a good enough compromise and catch most all non-integer errors. To make it compliant,
        // we would simply need to change the mantissa to int16, but this add compiled flash space.
        // Maybe update this later.
        int_value = int8_t(truncf(value));
        mantissa  = lroundf(100 * (value - int_value));  // Compute mantissa for Gxx.x commands.
        // NOTE: Rounding must be used to catch small floating point errors.
        // Check if the g-code word is supported or errors due to modal group violations or has
        // been repeated in the g-code block. If ok, update the command or record its value.
        switch (letter) {
            /* 'G' and 'M' Command Words: Parse commands and check for modal group violations.
           NOTE: Modal group numbers are defined in Table 4 of NIST RS274-NGC v3, pg.20 */
            case 'G':
                // Determine 'G' command and its modal group
                switch (int_value) {
                    // Modal Group G0 - non-modal actions
                    case 10:
                        gc_block.non_modal_command = NonModal::SetCoordinateData;
                        if (mantissa == 0) {
                            if (axis_command != AxisCommand::None) {
                                FAIL(Error::GcodeAxisCommandConflict);  // [Axis word/command conflict]
                            }
                            axis_command = AxisCommand::NonModal;
                        }
                        mg_word_bit = ModalGroup::MG0;
                        break;

                    case 28:
                        gc_block.non_modal_command = mantissa ? NonModal::SetHome0 : NonModal::GoHome0;
                        goto check_mantissa;
                    case 30:
                        gc_block.non_modal_command = mantissa ? NonModal::SetHome1 : NonModal::GoHome1;
                        goto check_mantissa;
                    case 92:
                        gc_block.non_modal_command = mantissa ? NonModal::ResetCoordinateOffset : NonModal::SetCoordinateOffset;
                    check_mantissa:
                        // Check for G10/28/30/92 being called with G0/1/2/3/38 on same block.
                        // * G43.1 is also an axis command but is not explicitly defined this way.
                        switch (mantissa) {
                            case 0:  // Ignore G28.1, G30.1, and G92.1
                                if (axis_command != AxisCommand::None) {
                                    FAIL(Error::GcodeAxisCommandConflict);  // [Axis word/command conflict]
                                }
                                axis_command = AxisCommand::NonModal;
                                break;
                            case 10:
                                mantissa = 0;  // Set to zero to indicate valid non-integer G command.
                                break;
                            default:
                                log_info("M4 requires laser mode or a reversable spindle");
                                FAIL(Error::GcodeUnsupportedCommand);
                                // not reached
                                break;
                        }
                        mg_word_bit = ModalGroup::MG0;
                        break;
                    case 4:
                        gc_block.non_modal_command = NonModal::Dwell;
                        mg_word_bit                = ModalGroup::MG0;
                        break;
                    case 53:
                        gc_block.non_modal_command = NonModal::AbsoluteOverride;
                        mg_word_bit                = ModalGroup::MG0;
                        break;

                    // Modal Group G1 - motion commands
                    case 0:  // G0 - linear rapid traverse
                        axis_command          = AxisCommand::MotionMode;
                        gc_block.modal.motion = Motion::Seek;
                        mg_word_bit           = ModalGroup::MG1;
                        break;
                    case 1:  // G1 - linear feedrate move
                        axis_command          = AxisCommand::MotionMode;
                        gc_block.modal.motion = Motion::Linear;
                        mg_word_bit           = ModalGroup::MG1;
                        break;
                    case 2:  // G2 - clockwise arc
                        axis_command          = AxisCommand::MotionMode;
                        gc_block.modal.motion = Motion::CwArc;
                        mg_word_bit           = ModalGroup::MG1;
                        break;
                    case 3:  // G3 - counterclockwise arc
                        axis_command          = AxisCommand::MotionMode;
                        gc_block.modal.motion = Motion::CcwArc;
                        mg_word_bit           = ModalGroup::MG1;
                        break;
                    case 38:  // G38 - probe
                        //only allow G38 "Probe" commands if a probe pin is defined.
                        if (!config->_probe->exists()) {
                            log_info("No probe pin defined");
                            FAIL(Error::GcodeUnsupportedCommand);  // [Unsupported G command]
                        }
                        // Check for G0/1/2/3/38 being called with G10/28/30/92 on same block.
                        // * G43.1 is also an axis command but is not explicitly defined this way.
                        if (axis_command != AxisCommand::None) {
                            FAIL(Error::GcodeAxisCommandConflict);  // [Axis word/command conflict]
                        }

                        // Indicate that the block contains an explicit G38.n word.  This lets us
                        // allow "G53 G38.n ..." without also allowing a sequence like
                        // "G38.n F10 Z-5 \n G53 X5".  In other words, we can use G53 to make the
                        // probe limit be in machine coordinates, without causing a later G53 that
                        // has no motion word to infer G38.n mode.
                        probeExplicit = true;

                        axis_command = AxisCommand::MotionMode;
                        if (mantissa >= 60) {
                            nonmodalG38 = true;
                            mantissa -= 40;
                        }
                        switch (mantissa) {
                            case 20:
                                gc_block.modal.motion = Motion::ProbeToward;
                                break;
                            case 30:
                                gc_block.modal.motion = Motion::ProbeTowardNoError;
                                break;
                            case 40:
                                gc_block.modal.motion = Motion::ProbeAway;
                                break;
                            case 50:
                                gc_block.modal.motion = Motion::ProbeAwayNoError;
                                break;
                            default:
                                FAIL(Error::GcodeUnsupportedCommand);
                                break;  // [Unsupported G38.x command]
                        }
                        mantissa    = 0;  // Set to zero to indicate valid non-integer G command.
                        mg_word_bit = ModalGroup::MG1;
                        break;

                    case 80:  // G80 - cancel canned cycle
                        gc_block.modal.motion = Motion::None;
                        mg_word_bit           = ModalGroup::MG1;
                        break;
                    case 17:
                        gc_block.modal.plane_select = Plane::XY;
                        mg_word_bit                 = ModalGroup::MG2;
                        break;
                    case 18:
                        gc_block.modal.plane_select = Plane::ZX;
                        mg_word_bit                 = ModalGroup::MG2;
                        break;
                    case 19:
                        gc_block.modal.plane_select = Plane::YZ;
                        mg_word_bit                 = ModalGroup::MG2;
                        break;
                    case 90:
                        switch (mantissa) {
                            case 0:
                                gc_block.modal.distance = Distance::Absolute;
                                mg_word_bit             = ModalGroup::MG3;
                                break;
                            case 10:
                                FAIL(Error::GcodeUnsupportedCommand);  // [G90.1 not supported]
                                // mg_word_bit = ModalGroup::MG4;
                                // gc_block.modal.distance_arc = ArcDistance::Absolute;
                                break;
                            default:
                                FAIL(Error::GcodeUnsupportedCommand);
                                break;
                        }
                        break;
                    case 91:
                        switch (mantissa) {
                            case 0:
                                gc_block.modal.distance = Distance::Incremental;
                                mg_word_bit             = ModalGroup::MG3;
                                break;
                            case 10:
                                mantissa = 0;  // Set to zero to indicate valid non-integer G command.
                                // Arc incremental mode is the default and only supported mode
                                // gc_block.modal.distance_arc = ArcDistance::Incremental;
                                mg_word_bit = ModalGroup::MG4;
                                break;
                            default:
                                FAIL(Error::GcodeUnsupportedCommand);
                                break;
                        }
                        break;
                    case 93:
                        gc_block.modal.feed_rate = FeedRate::InverseTime;
                        mg_word_bit              = ModalGroup::MG5;
                        break;
                    case 94:
                        gc_block.modal.feed_rate = FeedRate::UnitsPerMin;
                        mg_word_bit              = ModalGroup::MG5;
                        break;
                    case 20:
                        gc_block.modal.units = Units::Inches;
                        mg_word_bit          = ModalGroup::MG6;
                        break;
                    case 21:
                        gc_block.modal.units = Units::Mm;
                        mg_word_bit          = ModalGroup::MG6;
                        break;
                    case 40:
                        // NOTE: Not required since cutter radius compensation is always disabled. Only here
                        // to support G40 commands that often appear in g-code program headers to setup defaults.
                        // gc_block.modal.cutter_comp = CutterCompensation::Disable; // G40
                        mg_word_bit = ModalGroup::MG7;
                        break;
                    case 43:
                    case 49:
                        // NOTE: The NIST g-code standard vaguely states that when a tool length offset is changed,
                        // there cannot be any axis motion or coordinate offsets updated. Meaning G43, G43.1, and G49
                        // all are explicit axis commands, regardless if they require axis words or not.
                        if (axis_command != AxisCommand::None) {
                            FAIL(Error::GcodeAxisCommandConflict);
                        }
                        // [Axis word/command conflict] }
                        axis_command = AxisCommand::ToolLengthOffset;
                        if (int_value == 49) {  // G49
                            gc_block.modal.tool_length = ToolLengthOffset::Cancel;
                        } else if (mantissa == 10) {  // G43.1
                            gc_block.modal.tool_length = ToolLengthOffset::EnableDynamic;
                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);  // [Unsupported G43.x command]
                        }
                        mantissa    = 0;  // Set to zero to indicate valid non-integer G command.
                        mg_word_bit = ModalGroup::MG8;
                        break;
                    case 54:
                        gc_block.modal.coord_select = CoordIndex::G54;
                        mg_word_bit                 = ModalGroup::MG12;
                        break;
                    case 55:
                        gc_block.modal.coord_select = CoordIndex::G55;
                        mg_word_bit                 = ModalGroup::MG12;
                        break;
                    case 56:
                        gc_block.modal.coord_select = CoordIndex::G56;
                        mg_word_bit                 = ModalGroup::MG12;
                        break;
                    case 57:
                        gc_block.modal.coord_select = CoordIndex::G57;
                        mg_word_bit                 = ModalGroup::MG12;
                        break;
                    case 58:
                        gc_block.modal.coord_select = CoordIndex::G58;
                        mg_word_bit                 = ModalGroup::MG12;
                        break;
                    case 59:
                        gc_block.modal.coord_select = CoordIndex::G59;
                        mg_word_bit                 = ModalGroup::MG12;
                        break;
                        // NOTE: G59.x are not supported.
                    case 61:
                        if (mantissa != 0) {
                            FAIL(Error::GcodeUnsupportedCommand);  // [G61.1 not supported]
                        }
                        // gc_block.modal.control = ControlMode::ExactPath; // G61
                        mg_word_bit = ModalGroup::MG13;
                        break;
                    default:
                        FAIL(Error::GcodeUnsupportedCommand);  // [Unsupported G command]
                }
                if (mantissa > 0) {
                    FAIL(Error::GcodeCommandValueNotInteger);  // [Unsupported or invalid Gxx.x command]
                }
                // Check for more than one command per modal group violations in the current block
                // NOTE: Variable 'mg_word_bit' is always assigned, if the command is valid.
                bitmask = bitnum_to_mask(mg_word_bit);
                if (bits_are_true(command_words, bitmask)) {
                    FAIL(Error::GcodeModalGroupViolation);
                }
                command_words |= bitmask;
                break;
            case 'M':
                // Determine 'M' command and its modal group
                if (mantissa > 0 && !(int_value == 7 || int_value == 8)) {
                    FAIL(Error::GcodeCommandValueNotInteger);  // [No Mxx.x commands]
                }
                switch (int_value) {
                    case 0:
                        // M0 - Pause
                        gc_block.modal.program_flow = ProgramFlow::Paused;
                        mg_word_bit                 = ModalGroup::MM4;
                        break;
                    case 1:
                        // M1 - Optional Stop not supported
                        mg_word_bit = ModalGroup::MM4;
                        break;
                    case 2:
                        // M2 - Stop
                        gc_block.modal.program_flow = ProgramFlow::CompletedM2;
                        mg_word_bit                 = ModalGroup::MM4;
                        break;
                    case 30:
                        // M30 - End
                        gc_block.modal.program_flow = ProgramFlow::CompletedM30;
                        mg_word_bit                 = ModalGroup::MM4;
                        break;
                    case 3:
                    case 4:
                    case 5:
                        switch (int_value) {
                            case 3:
                                gc_block.modal.spindle = SpindleState::Cw;
                                break;
                            case 4:  // Supported if the spindle can be reversed or laser mode is on.
                                if (spindle->is_reversable || spindle->isRateAdjusted()) {
                                    gc_block.modal.spindle = SpindleState::Ccw;
                                } else {
                                    FAIL(Error::GcodeUnsupportedCommand);
                                }
                                break;
                            case 5:
                                gc_block.modal.spindle = SpindleState::Disable;
                                break;
                        }
                        mg_word_bit = ModalGroup::MM7;
                        break;
                    case 6:  // tool change
                        gc_block.modal.tool_change = ToolChange::Enable;
                        // user_tool_change(gc_state.tool);
                        mg_word_bit = ModalGroup::MM6;
                        break;
                    case 7:
                    case 8:
                    case 9:
                        switch (int_value) {
                            case 7:
                                if (mantissa && mantissa != 10) {
                                    FAIL(Error::GcodeUnsupportedCommand);  // M7 and M7.1 are supported
                                }
                                if (config->_coolant->hasMist()) {
                                    gc_block.coolant = GCodeCoolant::M7;
                                }
                                break;
                            case 8:
                                if (mantissa && mantissa != 10) {
                                    FAIL(Error::GcodeUnsupportedCommand);  // M8 and M8.1 are supported
                                }
                                if (config->_coolant->hasFlood()) {
                                    gc_block.coolant = GCodeCoolant::M8;
                                }
                                break;
                            case 9:
                                if (config->_coolant->hasFlood() || config->_coolant->hasMist()) {
                                    gc_block.coolant = GCodeCoolant::M9;
                                }
                                break;
                        }
                        mg_word_bit = ModalGroup::MM8;
                        break;
                    case 56:
                        if (config->_enableParkingOverrideControl) {
                            gc_block.modal.override = Override::ParkingMotion;
                            mg_word_bit             = ModalGroup::MM9;
                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);  // [Unsupported M command]
                        }
                        break;
                    case 62:
                        gc_block.modal.io_control = IoControl::DigitalOnSync;
                        mg_word_bit               = ModalGroup::MM10;
                        break;
                    case 63:
                        gc_block.modal.io_control = IoControl::DigitalOffSync;
                        mg_word_bit               = ModalGroup::MM10;
                        break;
                    case 64:
                        gc_block.modal.io_control = IoControl::DigitalOnImmediate;
                        mg_word_bit               = ModalGroup::MM10;
                        break;
                    case 65:
                        gc_block.modal.io_control = IoControl::DigitalOffImmediate;
                        mg_word_bit               = ModalGroup::MM10;
                        break;
                    case 67:
                        gc_block.modal.io_control = IoControl::SetAnalogSync;
                        mg_word_bit               = ModalGroup::MM10;
                        break;
                    case 68:
                        gc_block.modal.io_control = IoControl::SetAnalogImmediate;
                        mg_word_bit               = ModalGroup::MM10;
                        break;
                    default:
                        FAIL(Error::GcodeUnsupportedCommand);  // [Unsupported M command]
                }
                // Check for more than one command per modal group violations in the current block
                // NOTE: Variable 'mg_word_bit' is always assigned, if the command is valid.
                bitmask = bitnum_to_mask(mg_word_bit);
                if (bits_are_true(command_words, bitmask)) {
                    FAIL(Error::GcodeModalGroupViolation);
                }
                command_words |= bitmask;
                break;
            // NOTE: All remaining letters assign values.
            default:
                /* Non-Command Words: This initial parsing phase only checks for repeats of the remaining
               legal g-code words and stores their value. Error-checking is performed later since some
               words (I,J,K,L,P,R) have multiple connotations and/or depend on the issued commands. */
                GCodeWord axis_word_bit;
                switch (letter) {
                    case 'A':
                        if (n_axis > A_AXIS) {
                            axis_word_bit               = GCodeWord::A;
                            gc_block.values.xyz[A_AXIS] = value;
                            set_bitnum(axis_words, A_AXIS);
                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);
                        }
                        break;
                    case 'B':
                        if (n_axis > B_AXIS) {
                            axis_word_bit               = GCodeWord::B;
                            gc_block.values.xyz[B_AXIS] = value;
                            set_bitnum(axis_words, B_AXIS);
                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);
                        }
                        break;
                    case 'C':
                        if (n_axis > C_AXIS) {
                            axis_word_bit               = GCodeWord::C;
                            gc_block.values.xyz[C_AXIS] = value;
                            set_bitnum(axis_words, C_AXIS);
                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);
                        }
                        break;
                    // case 'D': // Not supported
                    case 'E':
                        axis_word_bit     = GCodeWord::E;
                        gc_block.values.e = int_value;
                        //log_info("E " << gc_block.values.e);
                        break;
                    case 'F':
                        axis_word_bit     = GCodeWord::F;
                        gc_block.values.f = value;
                        break;
                    // case 'H': // Not supported
                    case 'I':
                        axis_word_bit               = GCodeWord::I;
                        gc_block.values.ijk[X_AXIS] = value;
                        set_bitnum(ijk_words, X_AXIS);
                        break;
                    case 'J':
                        axis_word_bit               = GCodeWord::J;
                        gc_block.values.ijk[Y_AXIS] = value;
                        set_bitnum(ijk_words, Y_AXIS);
                        break;
                    case 'K':
                        axis_word_bit               = GCodeWord::K;
                        gc_block.values.ijk[Z_AXIS] = value;
                        set_bitnum(ijk_words, Z_AXIS);
                        break;
                    case 'L':
                        axis_word_bit     = GCodeWord::L;
                        gc_block.values.l = int_value;
                        break;
                    case 'N':
                        axis_word_bit     = GCodeWord::N;
                        gc_block.values.n = int32_t(truncf(value));
                        break;
                    case 'P':
                        axis_word_bit     = GCodeWord::P;
                        gc_block.values.p = value;
                        break;
                    case 'Q':
                        axis_word_bit     = GCodeWord::Q;
                        gc_block.values.q = value;
                        //log_info("Q " << value);
                        break;
                    case 'R':
                        axis_word_bit     = GCodeWord::R;
                        gc_block.values.r = value;
                        break;
                    case 'S':
                        axis_word_bit     = GCodeWord::S;
                        gc_block.values.s = value;
                        break;
                    case 'T':
                        axis_word_bit = GCodeWord::T;
                        if (value > MaxToolNumber) {
                            FAIL(Error::GcodeMaxValueExceeded);
                        }
                        log_info("Tool No: " << int_value);
                        gc_state.tool = int_value;
                        break;
                    case 'X':
                        if (n_axis > X_AXIS) {
                            axis_word_bit               = GCodeWord::X;
                            gc_block.values.xyz[X_AXIS] = value;
                            set_bitnum(axis_words, X_AXIS);

                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);
                        }
                        break;
                    case 'Y':
                        if (n_axis > Y_AXIS) {
                            axis_word_bit               = GCodeWord::Y;
                            gc_block.values.xyz[Y_AXIS] = value;
                            set_bitnum(axis_words, Y_AXIS);
                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);
                        }
                        break;
                    case 'Z':
                        if (n_axis > Z_AXIS) {
                            axis_word_bit               = GCodeWord::Z;
                            gc_block.values.xyz[Z_AXIS] = value;
                            set_bitnum(axis_words, Z_AXIS);
                        } else {
                            FAIL(Error::GcodeUnsupportedCommand);
                        }
                        break;
                    default:
                        FAIL(Error::GcodeUnsupportedCommand);
                }
                // NOTE: Variable 'axis_word_bit' is always assigned, if the non-command letter is valid.
                uint32_t bitmask = bitnum_to_mask(axis_word_bit);
                if (bits_are_true(value_words, bitmask)) {
                    FAIL(Error::GcodeWordRepeated);  // [Word repeated]
                }
                // Check for invalid negative values for words F, N, P, T, and S.
                // NOTE: Negative value check is done here simply for code-efficiency.
                if (bitmask & (bitnum_to_mask(GCodeWord::F) | bitnum_to_mask(GCodeWord::N) | bitnum_to_mask(GCodeWord::P) |
                               bitnum_to_mask(GCodeWord::T) | bitnum_to_mask(GCodeWord::S))) {
                    if (value < 0.0) {
                        FAIL(Error::NegativeValue);  // [Word value cannot be negative]
                    }
                }
                value_words |= bitmask;  // Flag to indicate parameter assigned.
        }
    }
    // Parsing complete!
    /* -------------------------------------------------------------------------------------
       STEP 3: Error-check all commands and values passed in this block. This step ensures all of
       the commands are valid for execution and follows the NIST standard as closely as possible.
       If an error is found, all commands and values in this block are dumped and will not update
       the active system g-code modes. If the block is ok, the active system g-code modes will be
       updated based on the commands of this block, and signal for it to be executed.

       Also, we have to pre-convert all of the values passed based on the modes set by the parsed
       block. There are a number of error-checks that require target information that can only be
       accurately calculated if we convert these values in conjunction with the error-checking.
       This relegates the next execution step as only updating the system g-code modes and
       performing the programmed actions in order. The execution step should not require any
       conversion calculations and would only require minimal checks necessary to execute.
    */
    /* NOTE: At this point, the g-code block has been parsed and the block line can be freed.
       NOTE: It's also possible, at some future point, to break up STEP 2, to allow piece-wise
       parsing of the block on a per-word basis, rather than the entire block. This could remove
       the need for maintaining a large string variable for the entire block and free up some memory.
       To do this, this would simply need to retain all of the data in STEP 1, such as the new block
       data struct, the modal group and value bitflag tracking variables, and axis array indices
       compatible variables. This data contains all of the information necessary to error-check the
       new g-code block when the EOL character is received. However, this would break startup
       lines in how it currently works and would require some refactoring to make it compatible.
    */
    // [0. Non-specific/common error-checks and miscellaneous setup]:
    // Determine implicit axis command conditions. Axis words have been passed, but no explicit axis
    // command has been sent. If so, set axis command to current motion mode.
    if (axis_words) {
        if (axis_command == AxisCommand::None) {
            axis_command = AxisCommand::MotionMode;  // Assign implicit motion-mode
        }
    }
    // Check for valid line number N value.
    if (bitnum_is_true(value_words, GCodeWord::N)) {
        // Line number value cannot be less than zero (done) or greater than max line number.
        if (gc_block.values.n > MaxLineNumber) {
            FAIL(Error::GcodeInvalidLineNumber);  // [Exceeds max line number]
        }
    }
    // clear_bitnum(value_words, GCodeWord::N); // NOTE: Single-meaning value word. Set at end of error-checking.
    // Track for unused words at the end of error-checking.
    // NOTE: Single-meaning value words are removed all at once at the end of error-checking, because
    // they are always used when present. This was done to save a few bytes of flash. For clarity, the
    // single-meaning value words may be removed as they are used. Also, axis words are treated in the
    // same way. If there is an explicit/implicit axis command, XYZ words are always used and are
    // are removed at the end of error-checking.
    // [1. Comments ]: MSG's NOT SUPPORTED. Comment handling performed by protocol.
    // [2. Set feed rate mode ]: G93 F word missing with G1,G2/3 active, implicitly or explicitly. Feed rate
    //   is not defined after switching to G94 from G93.
    // NOTE: For jogging, ignore prior feed rate mode. Enforce G94 and check for required F word.
    if (jogMotion) {
        if (bitnum_is_false(value_words, GCodeWord::F)) {
            FAIL(Error::GcodeUndefinedFeedRate);
        }
        if (!nonmodalG38 && gc_block.modal.units == Units::Inches) {
            gc_block.values.f *= MM_PER_INCH;
        }
    } else {
        if (gc_block.modal.feed_rate == FeedRate::InverseTime) {  // = G93
            // NOTE: G38 can also operate in inverse time, but is undefined as an error. Missing F word check added here.
            if (axis_command == AxisCommand::MotionMode) {
                if ((gc_block.modal.motion != Motion::None) || (gc_block.modal.motion != Motion::Seek)) {
                    if (bitnum_is_false(value_words, GCodeWord::F)) {
                        FAIL(Error::GcodeUndefinedFeedRate);  // [F word missing]
                    }
                }
            }
            // NOTE: It seems redundant to check for an F word to be passed after switching from G94 to G93. We would
            // accomplish the exact same thing if the feed rate value is always reset to zero and undefined after each
            // inverse time block, since the commands that use this value already perform undefined checks. This would
            // also allow other commands, following this switch, to execute and not error out needlessly. This code is
            // combined with the above feed rate mode and the below set feed rate error-checking.
            // [3. Set feed rate ]: F is negative (done.)
            // - In inverse time mode: Always implicitly zero the feed rate value before and after block completion.
            // NOTE: If in G93 mode or switched into it from G94, just keep F value as initialized zero or passed F word
            // value in the block. If no F word is passed with a motion command that requires a feed rate, this will error
            // out in the motion modes error-checking. However, if no F word is passed with NO motion command that requires
            // a feed rate, we simply move on and the state feed rate value gets updated to zero and remains undefined.
        } else {  // = G94
            // - In units per mm mode: If F word passed, ensure value is in mm/min, otherwise push last state value.
            if (gc_state.modal.feed_rate == FeedRate::UnitsPerMin) {  // Last state is also G94
                if (bitnum_is_true(value_words, GCodeWord::F)) {
                    if (!nonmodalG38 && gc_block.modal.units == Units::Inches) {
                        gc_block.values.f *= MM_PER_INCH;
                    }
                } else {
                    gc_block.values.f = gc_state.feed_rate;  // Push last state feed rate
                }
            }  // Else, switching to G94 from G93, so don't push last state feed rate. Its undefined or the passed F word value.
        }
    }
    // clear_bitnum(value_words, GCodeWord::F); // NOTE: Single-meaning value word. Set at end of error-checking.
    // [4. Set spindle speed ]: S is negative (done.)
    if (bitnum_is_false(value_words, GCodeWord::S)) {
        gc_block.values.s = gc_state.spindle_speed;
        // clear_bitnum(value_words, GCodeWord::S); // NOTE: Single-meaning value word. Set at end of error-checking.
        // [5. Select tool ]: NOT SUPPORTED. Only tracks value. T is negative (done.) Not an integer. Greater than max tool value.
        // clear_bitnum(value_words, GCodeWord::T); // NOTE: Single-meaning value word. Set at end of error-checking.
        // [6. Change tool ]: N/A
        // [7. Spindle control ]: N/A
        // [8. Coolant control ]: N/A
        // [9. Enable/disable feed rate or spindle overrides ]: NOT SUPPORTED.
    }

    if (config->_enableParkingOverrideControl) {
        if (bitnum_is_true(command_words, ModalGroup::MM9)) {  // Already set as enabled in parser.
            if (bitnum_is_true(value_words, GCodeWord::P)) {
                if (gc_block.values.p == 0.0) {
                    gc_block.modal.override = Override::Disabled;
                }
                clear_bits(value_words, bitnum_to_mask(GCodeWord::P));
            }
        }
    }
    // [10. Dwell ]: P value missing. P is negative (done.) NOTE: See below.
    if (gc_block.non_modal_command == NonModal::Dwell) {
        if (bitnum_is_false(value_words, GCodeWord::P)) {
            FAIL(Error::GcodeValueWordMissing);  // [P word missing]
        }
        clear_bitnum(value_words, GCodeWord::P);
    }
    if ((gc_block.modal.io_control == IoControl::DigitalOnSync) || (gc_block.modal.io_control == IoControl::DigitalOffSync) ||
        (gc_block.modal.io_control == IoControl::DigitalOnImmediate) || (gc_block.modal.io_control == IoControl::DigitalOffImmediate)) {
        if (bitnum_is_false(value_words, GCodeWord::P)) {
            FAIL(Error::GcodeValueWordMissing);  // [P word missing]
        }
        clear_bitnum(value_words, GCodeWord::P);
    }
    if ((gc_block.modal.io_control == IoControl::SetAnalogSync) || (gc_block.modal.io_control == IoControl::SetAnalogImmediate)) {
        if (bitnum_is_false(value_words, GCodeWord::E) || bitnum_is_false(value_words, GCodeWord::Q)) {
            FAIL(Error::GcodeValueWordMissing);
        }
        clear_bitnum(value_words, GCodeWord::E);
        clear_bitnum(value_words, GCodeWord::Q);
    }
    // [11. Set active plane ]: N/A
    switch (gc_block.modal.plane_select) {
        case Plane::XY:
            axis_0      = X_AXIS;
            axis_1      = Y_AXIS;
            axis_linear = Z_AXIS;
            break;
        case Plane::ZX:
            axis_0      = Z_AXIS;
            axis_1      = X_AXIS;
            axis_linear = Y_AXIS;
            break;
        default:  // case Plane::YZ:
            axis_0      = Y_AXIS;
            axis_1      = Z_AXIS;
            axis_linear = X_AXIS;
    }

    // [12. Set length units ]: N/A
    // Pre-convert XYZ coordinate values to millimeters, if applicable.
    if (!nonmodalG38 && gc_block.modal.units == Units::Inches) {
        for (size_t idx = 0; idx < n_axis; idx++) {  // Axes indices are consistent, so loop may be used.
            if ((idx < A_AXIS || idx > C_AXIS) && bitnum_is_true(axis_words, idx)) {
                gc_block.values.xyz[idx] *= MM_PER_INCH;
            }
        }
    }

    // [13. Cutter radius compensation ]: G41/42 NOT SUPPORTED. Error, if enabled while G53 is active.
    // [G40 Errors]: G2/3 arc is programmed after a G40. The linear move after disabling is less than tool diameter.
    //   NOTE: Since cutter radius compensation is never enabled, these G40 errors don't apply. G40 is supported
    //   only for the purpose of not erroring when G40 is sent with a g-code program header to setup the default modes.
    // [14. Cutter length compensation ]: G43 NOT SUPPORTED, but G43.1 and G49 are.
    // [G43.1 Errors]: Motion command in same line.
    //   NOTE: Although not explicitly stated so, G43.1 should be applied to only one valid
    //   axis that is configured (in config.h). There should be an error if the configured axis
    //   is absent or if any of the other axis words are present.
    if (axis_command == AxisCommand::ToolLengthOffset) {  // Indicates called in block.
        gc_ngc_changed(CoordIndex::TLO);
        if (gc_block.modal.tool_length == ToolLengthOffset::EnableDynamic) {
            if (axis_words ^ bitnum_to_mask(TOOL_LENGTH_OFFSET_AXIS)) {
                FAIL(Error::GcodeG43DynamicAxisError);
            }
        }
    }
    // [15. Coordinate system selection ]: *N/A. Error, if cutter radius comp is active.
    // TODO: Reading the coordinate data may require a buffer sync when the cycle
    // is active. The read pauses the processor temporarily and may cause a rare crash. For
    // future versions on processors with enough memory, all coordinate data should be stored
    // in memory and written to non-volatile storage only when there is not a cycle active.
    float block_coord_system[MAX_N_AXIS];
    copyAxes(block_coord_system, gc_state.coord_system);
    if (bitnum_is_true(command_words, ModalGroup::MG12)) {  // Check if called in block
        // This error probably cannot happen because preceding code sets
        // gc_block.modal.coord_select only to specific supported values
        if (gc_block.modal.coord_select >= CoordIndex::NWCSystems) {
            FAIL(Error::GcodeUnsupportedCoordSys);  // [Greater than N sys]
        }
        if (gc_state.modal.coord_select != gc_block.modal.coord_select) {
            coords[gc_block.modal.coord_select]->get(block_coord_system);
        }
    }
    // [16. Set path control mode ]: N/A. Only G61. G61.1 and G64 NOT SUPPORTED.
    // [17. Set distance mode ]: N/A. Only G91.1. G90.1 NOT SUPPORTED.
    // [18. Set retract mode ]: NOT SUPPORTED.
    // [19. Remaining non-modal actions ]: Check go to predefined position, set G10, or set axis offsets.
    // NOTE: We need to separate the non-modal commands that are axis word-using (G10/G28/G30/G92), as these
    // commands all treat axis words differently. G10 as absolute offsets or computes current position as
    // the axis value, G92 similarly to G10 L20, and G28/30 as an intermediate target position that observes
    // all the current coordinate system and G92 offsets.
    switch (gc_block.non_modal_command) {
        case NonModal::SetCoordinateData:
            // [G10 Errors]: L missing and is not 2 or 20. P word missing. (Negative P value done.)
            // [G10 L2 Errors]: R word NOT SUPPORTED. P value not 0 to nCoordSys(max 9). Axis words missing.
            // [G10 L20 Errors]: P must be 0 to nCoordSys(max 9). Axis words missing.
            if (!axis_words) {
                FAIL(Error::GcodeNoAxisWords)
            };  // [No axis words]
            if (bits_are_false(value_words, (bitnum_to_mask(GCodeWord::P) | bitnum_to_mask(GCodeWord::L)))) {
                FAIL(Error::GcodeValueWordMissing);  // [P/L word missing]
            }
            if (gc_block.values.l != 20) {
                if (gc_block.values.l == 2) {
                    if (bitnum_is_true(value_words, GCodeWord::R)) {
                        FAIL(Error::GcodeUnsupportedCommand);  // [G10 L2 R not supported]
                    }
                } else {
                    FAIL(Error::GcodeUnsupportedCommand);  // [Unsupported L]
                }
            }
            // Select the coordinate system based on the P word
            pValue = int8_t(truncf(gc_block.values.p));  // Convert p value to integer
            if (pValue > 0) {
                // P1 means G54, P2 means G55, etc.
                coord_select = static_cast<CoordIndex>(pValue - 1 + int(CoordIndex::G54));
            } else {
                // P0 means use currently-selected system
                coord_select = gc_block.modal.coord_select;
            }
            if (coord_select >= CoordIndex::NWCSystems) {
                FAIL(Error::GcodeUnsupportedCoordSys);  // [Greater than N sys]
            }
            clear_bits(value_words, (bitnum_to_mask(GCodeWord::L) | bitnum_to_mask(GCodeWord::P)));
            coords[coord_select]->get(coord_data);

            // Pre-calculate the coordinate data changes.
            for (size_t idx = 0; idx < n_axis; idx++) {  // Axes indices are consistent, so loop may be used.
                // Update axes defined only in block. Always in machine coordinates. Can change non-active system.
                if (bitnum_is_true(axis_words, idx)) {
                    if (gc_block.values.l == 20) {
                        // L20: Update coordinate system axis at current position (with modifiers) with programmed value
                        // WPos = MPos - WCS - G92 - TLO  ->  WCS = MPos - G92 - TLO - WPos
                        coord_data[idx] = gc_state.position[idx] - gc_state.coord_offset[idx] - gc_block.values.xyz[idx];
                        if (idx == TOOL_LENGTH_OFFSET_AXIS) {
                            coord_data[idx] -= gc_state.tool_length_offset;
                        }
                    } else {
                        // L2: Update coordinate system axis to programmed value.
                        coord_data[idx] = gc_block.values.xyz[idx];
                    }
                }  // Else, keep current stored value.
            }
            gc_ngc_changed(static_cast<CoordIndex>(coord_select));
            break;
        case NonModal::SetCoordinateOffset:
            // [G92 Errors]: No axis words.
            if (!axis_words) {
                FAIL(Error::GcodeNoAxisWords);  // [No axis words]
            }
            // Update axes defined only in block. Offsets current system to defined value. Does not update when
            // active coordinate system is selected, but is still active unless G92.1 disables it.
            for (size_t idx = 0; idx < n_axis; idx++) {  // Axes indices are consistent, so loop may be used.
                if (bitnum_is_true(axis_words, idx)) {
                    // WPos = MPos - WCS - G92 - TLO  ->  G92 = MPos - WCS - TLO - WPos
                    gc_block.values.xyz[idx] = gc_state.position[idx] - block_coord_system[idx] - gc_block.values.xyz[idx];
                    if (idx == TOOL_LENGTH_OFFSET_AXIS) {
                        gc_block.values.xyz[idx] -= gc_state.tool_length_offset;
                    }
                } else {
                    gc_block.values.xyz[idx] = gc_state.coord_offset[idx];
                }
            }
            gc_ngc_changed(CoordIndex::G92);
            break;
        default:
            // At this point, the rest of the explicit axis commands treat the axis values as the traditional
            // target position with the coordinate system offsets, G92 offsets, absolute override, and distance
            // modes applied. This includes the motion mode commands. We can now pre-compute the target position.
            // NOTE: Tool offsets may be appended to these conversions when/if this feature is added.
            if (axis_command != AxisCommand::ToolLengthOffset) {  // TLO block any axis command.
                if (axis_words) {
                    for (size_t idx = 0; idx < n_axis; idx++) {  // Axes indices are consistent, so loop may be used to save flash space.
                        if (bitnum_is_false(axis_words, idx)) {
                            gc_block.values.xyz[idx] = gc_state.position[idx];  // No axis word in block. Keep same axis position.
                        } else {
                            // Update specified value according to distance mode or ignore if absolute override is active.
                            // NOTE: G53 is never active with G28/30 since they are in the same modal group.
                            if (gc_block.non_modal_command != NonModal::AbsoluteOverride) {
                                // Apply coordinate offsets based on distance mode.
                                if (!nonmodalG38 && gc_block.modal.distance == Distance::Absolute) {
                                    gc_block.values.xyz[idx] += block_coord_system[idx] + gc_state.coord_offset[idx];
                                    if (idx == TOOL_LENGTH_OFFSET_AXIS) {
                                        gc_block.values.xyz[idx] += gc_state.tool_length_offset;
                                    }
                                } else {  // Incremental mode
                                    gc_block.values.xyz[idx] += gc_state.position[idx];
                                }
                            }
                        }
                    }
                }
            }
            // Check remaining non-modal commands for errors.
            switch (gc_block.non_modal_command) {
                case NonModal::GoHome0:  // G28
                case NonModal::GoHome1:  // G30
                    // [G28/30 Errors]: Cutter compensation is enabled.
                    // Retreive G28/30 go-home position data (in machine coordinates) from non-volatile storage
                    if (gc_block.non_modal_command == NonModal::GoHome0) {
                        coords[CoordIndex::G28]->get(coord_data);
                    } else {  // == NonModal::GoHome1
                        coords[CoordIndex::G30]->get(coord_data);
                    }
                    if (axis_words) {
                        // Move only the axes specified in secondary move.
                        for (size_t idx = 0; idx < n_axis; idx++) {
                            if (!(axis_words & bitnum_to_mask(idx))) {
                                coord_data[idx] = gc_state.position[idx];
                            }
                        }
                    } else {
                        axis_command = AxisCommand::None;  // Set to none if no intermediate motion.
                    }
                    break;
                case NonModal::SetHome0:  // G28.1
                case NonModal::SetHome1:  // G30.1
                    // [G28.1/30.1 Errors]: Cutter compensation is enabled.
                    // NOTE: If axis words are passed here, they are interpreted as an implicit motion mode.
                    break;
                case NonModal::ResetCoordinateOffset:
                    // NOTE: If axis words are passed here, they are interpreted as an implicit motion mode.
                    break;
                case NonModal::AbsoluteOverride:
                    // [G53 Errors]: G0 and G1 are not active. Cutter compensation is enabled.
                    // NOTE: All explicit axis word commands are in this modal group. So no implicit check necessary.
                    if (!(probeExplicit || gc_block.modal.motion == Motion::Seek || gc_block.modal.motion == Motion::Linear)) {
                        FAIL(Error::GcodeG53InvalidMotionMode);  // [G53 G0/1 not active]
                    }
                    break;
                default:
                    break;
            }
    }
    // [20. Motion modes ]:
    if (gc_block.modal.motion == Motion::None) {
        // [G80 Errors]: Axis word are programmed while G80 is active.
        // NOTE: Even non-modal commands or TLO that use axis words will throw this strict error.
        if (axis_words) {
            FAIL(Error::GcodeAxisWordsExist);  // [No axis words allowed]
        }
        // Check remaining motion modes, if axis word are implicit (exist and not used by G10/28/30/92), or
        // was explicitly commanded in the g-code block.
    } else if (axis_command == AxisCommand::MotionMode) {
        if (gc_block.modal.motion == Motion::Seek) {
            // [G0 Errors]: Axis letter not configured or without real value (done.)
            // Axis words are optional. If missing, set axis command flag to ignore execution.
            if (!axis_words) {
                axis_command = AxisCommand::None;
            }
            // All remaining motion modes (all but G0 and G80), require a valid feed rate value. In units per mm mode,
            // the value must be positive. In inverse time mode, a positive value must be passed with each block.
        } else {
            // Check if feed rate is defined for the motion modes that require it.
            if (gc_block.values.f == 0.0) {
                FAIL(Error::GcodeUndefinedFeedRate);  // [Feed rate undefined]
            }
            switch (gc_block.modal.motion) {
                case Motion::None:
                    break;  // Feed rate is unnecessary
                case Motion::Seek:
                    break;  // Feed rate is unnecessary
                case Motion::Linear:
                    // [G1 Errors]: Feed rate undefined. Axis letter not configured or without real value.
                    // Axis words are optional. If missing, set axis command flag to ignore execution.
                    if (!axis_words) {
                        axis_command = AxisCommand::None;
                    }
                    break;
                case Motion::CwArc:
                    clockwiseArc = true;  // No break intentional.
                case Motion::CcwArc:
                    // [G2/3 Errors All-Modes]: Feed rate undefined.
                    // [G2/3 Radius-Mode Errors]: No axis words in selected plane. Target point is same as current.
                    // [G2/3 Offset-Mode Errors]: No axis words and/or offsets in selected plane. The radius to the current
                    //   point and the radius to the target point differs more than 0.002mm (EMC def. 0.5mm OR 0.005mm and 0.1% radius).
                    // [G2/3 Full-Circle-Mode Errors]: NOT SUPPORTED. Axis words exist. No offsets programmed. P must be an integer.
                    // NOTE: Both radius and offsets are required for arc tracing and are pre-computed with the error-checking.
                    if (!axis_words) {
                        FAIL(Error::GcodeNoAxisWords);  // [No axis words]
                    }
                    if (!(axis_words & (bitnum_to_mask(axis_0) | bitnum_to_mask(axis_1)))) {
                        FAIL(Error::GcodeNoAxisWordsInPlane);  // [No axis words in plane]
                    }
                    if (gc_block.values.p != truncf(gc_block.values.p) || gc_block.values.p < 0.0) {
                        FAIL(Error::GcodeCommandValueNotInteger);  // [P word is not an integer]
                    }

                    // Calculate the change in position along each selected axis
                    float x, y;
                    x = gc_block.values.xyz[axis_0] - gc_state.position[axis_0];  // Delta x between current position and target
                    y = gc_block.values.xyz[axis_1] - gc_state.position[axis_1];  // Delta y between current position and target
                    if (value_words & bitnum_to_mask(GCodeWord::R)) {             // Arc Radius Mode
                        clear_bits(value_words, bitnum_to_mask(GCodeWord::R));
                        if (isequal_position_vector(gc_state.position, gc_block.values.xyz)) {
                            FAIL(Error::GcodeInvalidTarget);  // [Invalid target]
                        }
                        // Convert radius value to proper units.
                        if (!nonmodalG38 && gc_block.modal.units == Units::Inches) {
                            gc_block.values.r *= MM_PER_INCH;
                        }
                        /*  We need to calculate the center of the circle that has the designated radius and passes
                        through both the current position and the target position. This method calculates the following
                        set of equations where [x,y] is the vector from current to target position, d == magnitude of
                        that vector, h == hypotenuse of the triangle formed by the radius of the circle, the distance to
                        the center of the travel vector. A vector perpendicular to the travel vector [-y,x] is scaled to the
                        length of h [-y/d*h, x/d*h] and added to the center of the travel vector [x/2,y/2] to form the new point
                        [i,j] at [x/2-y/d*h, y/2+x/d*h] which will be the center of our arc.

                        d^2 == x^2 + y^2
                        h^2 == r^2 - (d/2)^2
                        i == x/2 - y/d*h
                        j == y/2 + x/d*h

                                                                             O <- [i,j]
                                                                          -  |
                                                                r      -     |
                                                                    -        |
                                                                 -           | h
                                                              -              |
                                                [0,0] ->  C -----------------+--------------- T  <- [x,y]
                                                          | <------ d/2 ---->|

                        C - Current position
                        T - Target position
                        O - center of circle that pass through both C and T
                        d - distance from C to T
                        r - designated radius
                        h - distance from center of CT to O

                        Expanding the equations:

                        d -> sqrt(x^2 + y^2)
                        h -> sqrt(4 * r^2 - x^2 - y^2)/2
                        i -> (x - (y * sqrt(4 * r^2 - x^2 - y^2)) / sqrt(x^2 + y^2)) / 2
                        j -> (y + (x * sqrt(4 * r^2 - x^2 - y^2)) / sqrt(x^2 + y^2)) / 2

                        Which can be written:

                        i -> (x - (y * sqrt(4 * r^2 - x^2 - y^2))/sqrt(x^2 + y^2))/2
                        j -> (y + (x * sqrt(4 * r^2 - x^2 - y^2))/sqrt(x^2 + y^2))/2

                        Which we for size and speed reasons optimize to:

                        h_x2_div_d = sqrt(4 * r^2 - x^2 - y^2)/sqrt(x^2 + y^2)
                        i = (x - (y * h_x2_div_d))/2
                        j = (y + (x * h_x2_div_d))/2
                    */
                        // First, use h_x2_div_d to compute 4*h^2 to check if it is negative or r is smaller
                        // than d. If so, the sqrt of a negative number is complex and error out.
                        float h_x2_div_d = 4.0f * gc_block.values.r * gc_block.values.r - x * x - y * y;
                        if (h_x2_div_d < 0) {
                            FAIL(Error::GcodeArcRadiusError);  // [Arc radius error]
                        }
                        // Finish computing h_x2_div_d.
                        h_x2_div_d = -sqrt(h_x2_div_d) / hypot_f(x, y);  // == -(h * 2 / d)
                        // Invert the sign of h_x2_div_d if the circle is counter clockwise (see sketch below)
                        if (gc_block.modal.motion == Motion::CcwArc) {
                            h_x2_div_d = -h_x2_div_d;
                        }
                        /* The counter clockwise circle lies to the left of the target direction. When offset is positive,
                       the left hand circle will be generated - when it is negative the right hand circle is generated.

                                                                           T  <-- Target position

                                                                           ^
                                Clockwise circles with this center         |          Clockwise circles with this center will have
                                will have > 180 deg of angular travel      |          < 180 deg of angular travel, which is a good thing!
                                                                 \         |          /
                    center of arc when h_x2_div_d is positive ->  x <----- | -----> x <- center of arc when h_x2_div_d is negative
                                                                           |
                                                                           |

                                                                           C  <-- Current position
                    */
                        // Negative R is g-code-alese for "I want a circle with more than 180 degrees of travel" (go figure!),
                        // even though it is advised against ever generating such circles in a single line of g-code. By
                        // inverting the sign of h_x2_div_d the center of the circles is placed on the opposite side of the line of
                        // travel and thus we get the unadvisably long arcs as prescribed.
                        if (gc_block.values.r < 0) {
                            h_x2_div_d        = -h_x2_div_d;
                            gc_block.values.r = -gc_block.values.r;  // Finished with r. Set to positive for mc_arc
                        }
                        // Complete the operation by calculating the actual center of the arc
                        gc_block.values.ijk[axis_0] = 0.5f * (x - (y * h_x2_div_d));
                        gc_block.values.ijk[axis_1] = 0.5f * (y + (x * h_x2_div_d));
                    } else {  // Arc Center Format Offset Mode
                        if (!(ijk_words & (bitnum_to_mask(axis_0) | bitnum_to_mask(axis_1)))) {
                            FAIL(Error::GcodeNoOffsetsInPlane);  // [No offsets in plane]
                        }
                        clear_bits(value_words, (bitnum_to_mask(GCodeWord::I) | bitnum_to_mask(GCodeWord::J) | bitnum_to_mask(GCodeWord::K)));
                        // Convert IJK values to proper units.
                        if (!nonmodalG38 && gc_block.modal.units == Units::Inches) {
                            for (size_t idx = 0; idx < n_axis; idx++) {  // Axes indices are consistent, so loop may be used to save flash space.
                                if (ijk_words & bitnum_to_mask(idx)) {
                                    gc_block.values.ijk[idx] *= MM_PER_INCH;
                                }
                            }
                        }
                        // Arc radius from center to target
                        x -= gc_block.values.ijk[axis_0];  // Delta x between circle center and target
                        y -= gc_block.values.ijk[axis_1];  // Delta y between circle center and target
                        float target_r = hypot_f(x, y);
                        // Compute arc radius for mc_arc. Defined from current location to center.
                        gc_block.values.r = hypot_f(gc_block.values.ijk[axis_0], gc_block.values.ijk[axis_1]);
                        // Compute difference between current location and target radii for final error-checks.
                        float delta_r = fabsf(target_r - gc_block.values.r);
                        if (delta_r > 0.005) {
                            if (delta_r > 0.5) {
                                FAIL(Error::GcodeInvalidTarget);  // [Arc definition error] > 0.5mm
                            }
                            if (delta_r > (0.001 * gc_block.values.r)) {
                                FAIL(Error::GcodeInvalidTarget);  // [Arc definition error] > 0.005mm AND 0.1% radius
                            }
                        }
                    }
                    clear_bitnum(value_words, GCodeWord::P);
                    break;
                case Motion::ProbeTowardNoError:
                case Motion::ProbeAwayNoError:
                    probeNoError = true;  // No break intentional.
                case Motion::ProbeToward:
                case Motion::ProbeAway:
                    if ((gc_block.modal.motion == Motion::ProbeAway) || (gc_block.modal.motion == Motion::ProbeAwayNoError)) {
                        probeAway = true;
                    }
                    // [G38 Errors]: Target is same current. No axis words. Cutter compensation is enabled. Feed rate
                    //   is undefined. Probe is triggered. NOTE: Probe check moved to probe cycle. Instead of returning
                    //   an error, it issues an alarm to prevent further motion to the probe. It's also done there to
                    //   allow the planner buffer to empty and move off the probe trigger before another probing cycle.
                    if (bitnum_is_true(value_words, GCodeWord::P)) {
                        if (multiple_bits_set(axis_words)) {  // There should only be one axis word given
                            FAIL(Error::GcodeUnusedWords);    // we have more axis words than allowed.
                        }
                    } else {
                        gc_block.values.p = __FLT_MAX__;  // This is a hack to signal the probe cycle that not to auto offset.
                    }
                    clear_bitnum(value_words, GCodeWord::P);  // allow P to be used

                    if (!axis_words) {
                        FAIL(Error::GcodeNoAxisWords);  // [No axis words]
                    }
                    if (isequal_position_vector(gc_state.position, gc_block.values.xyz)) {
                        FAIL(Error::GcodeInvalidTarget);  // [Invalid target]
                    }
                    break;
            }
        }
    }
    // [21. Program flow ]: No error checks required.
    // [0. Non-specific error-checks]: Complete unused value words check, i.e. IJK used when in arc
    // radius mode, or axis words that aren't used in the block.
    if (jogMotion) {
        // Jogging only uses the F feed rate and XYZ value words. N is valid, but S and T are invalid.
        clear_bits(value_words, (bitnum_to_mask(GCodeWord::N) | bitnum_to_mask(GCodeWord::F)));
    } else {
        clear_bits(value_words,
                   (bitnum_to_mask(GCodeWord::N) | bitnum_to_mask(GCodeWord::F) | bitnum_to_mask(GCodeWord::S) |
                    bitnum_to_mask(GCodeWord::T)));  // Remove single-meaning value words.
    }
    if (axis_command != AxisCommand::None) {
        clear_bits(value_words,
                   (bitnum_to_mask(GCodeWord::X) | bitnum_to_mask(GCodeWord::Y) | bitnum_to_mask(GCodeWord::Z) |
                    bitnum_to_mask(GCodeWord::A) | bitnum_to_mask(GCodeWord::B) | bitnum_to_mask(GCodeWord::C)));  // Remove axis words.
    }
    if (value_words) {
        FAIL(Error::GcodeUnusedWords);  // [Unused words]
    }
    /* -------------------------------------------------------------------------------------
       STEP 4: EXECUTE!!
       Assumes that all error-checking has been completed and no failure modes exist. We just
       need to update the state and execute the block according to the order-of-execution.
    */
    // Initialize planner data struct for motion blocks.
    plan_line_data_t  plan_data;
    plan_line_data_t* pl_data = &plan_data;
    memset(pl_data, 0, sizeof(plan_line_data_t));  // Zero pl_data struct
    // Intercept jog commands and complete error checking for valid jog commands and execute.
    // NOTE: G-code parser state is not updated, except the position to ensure sequential jog
    // targets are computed correctly. The final parser position after a jog is updated in
    // protocol_execute_realtime() when jogging completes or is canceled.
    if (jogMotion) {
        // Only distance and unit modal commands and G53 absolute override command are allowed.
        // NOTE: Feed rate word and axis word checks have already been performed in STEP 3.
        if (command_words & ~(bitnum_to_mask(ModalGroup::MG3) | bitnum_to_mask(ModalGroup::MG6) | bitnum_to_mask(ModalGroup::MG0))) {
            FAIL(Error::InvalidJogCommand)
        };
        if (!(gc_block.non_modal_command == NonModal::AbsoluteOverride || gc_block.non_modal_command == NonModal::NoAction)) {
            FAIL(Error::InvalidJogCommand);
        }
        // Initialize planner data to current spindle and coolant modal state.
        pl_data->spindle_speed  = gc_state.spindle_speed;
        pl_data->spindle        = gc_state.modal.spindle;
        pl_data->coolant        = gc_state.modal.coolant;
        bool  cancelledInflight = false;
        Error status            = jog_execute(pl_data, &gc_block, &cancelledInflight);
        if (status == Error::Ok && !cancelledInflight) {
            copyAxes(gc_state.position, gc_block.values.xyz);
        }
        // JogCancelled is not reported as a GCode error
        return status == Error::JogCancelled ? Error::Ok : status;
    }
    // If in laser mode, setup laser power based on current and past parser conditions.
    if (spindle->isRateAdjusted()) {
        bool blockIsFeedrateMotion = (gc_block.modal.motion == Motion::Linear) || (gc_block.modal.motion == Motion::CwArc) ||
                                     (gc_block.modal.motion == Motion::CcwArc);
        bool stateIsFeedrateMotion = (gc_state.modal.motion == Motion::Linear) || (gc_state.modal.motion == Motion::CwArc) ||
                                     (gc_state.modal.motion == Motion::CcwArc);

        if (!blockIsFeedrateMotion) {
            // If the new mode is not a feedrate move (G1/2/3) we want the laser off
            disableLaser = true;
        }
        // Any motion mode with axis words is allowed to be passed from a spindle speed update.
        // NOTE: G1 and G0 without axis words sets axis_command to none. G28/30 are intentionally omitted.
        // TODO: Check sync conditions for M3 enabled motions that don't enter the planner. (zero length).
        if (axis_words && (axis_command == AxisCommand::MotionMode)) {
            laserIsMotion = true;
        } else {
            // M3 constant power laser requires planner syncs to update the laser when changing between
            // a G1/2/3 motion mode state and vice versa when there is no motion in the line.
            if (gc_state.modal.spindle == SpindleState::Cw) {
                if (stateIsFeedrateMotion) {
                    if (disableLaser) {
                        syncLaser = true;  // Change from G1/2/3 motion mode.
                    }
                } else {
                    // When changing to a G1 motion mode without axis words from a non-G1/2/3 motion mode.
                    if (!disableLaser) {
                        syncLaser = true;
                    }
                }
            }
        }
    }
    // [0. Non-specific/common error-checks and miscellaneous setup]:
    // NOTE: If no line number is present, the value is zero.
    gc_state.line_number = gc_block.values.n;
    pl_data->line_number = gc_state.line_number;  // Record data for planner use.

    // [1. Comments feedback ]:  NOT SUPPORTED
    // [2. Set feed rate mode ]:
    gc_state.modal.feed_rate = gc_block.modal.feed_rate;
    if (gc_state.modal.feed_rate == FeedRate::InverseTime) {
        pl_data->motion.inverseTime = 1;  // Set condition flag for planner use.
    }
    // [3. Set feed rate ]:
    gc_state.feed_rate = gc_block.values.f;   // Always copy this value. See feed rate error-checking.
    pl_data->feed_rate = gc_state.feed_rate;  // Record data for planner use.
    // [4. Set spindle speed ]:
    if ((gc_state.spindle_speed != gc_block.values.s) || syncLaser) {
        if (gc_state.modal.spindle != SpindleState::Disable && !laserIsMotion && sys.state != State::CheckMode) {
            protocol_buffer_synchronize();
            spindle->setState(gc_state.modal.spindle, disableLaser ? 0 : (uint32_t)gc_block.values.s);
            report_ovr_counter = 0;  // Set to report change immediately
        }
        gc_state.spindle_speed = gc_block.values.s;  // Update spindle speed state.
    }
    // NOTE: Pass zero spindle speed for all restricted laser motions.
    if (!disableLaser) {
        pl_data->spindle_speed = gc_state.spindle_speed;  // Record data for planner use.
    }                                                     // else { pl_data->spindle_speed = 0.0; } // Initialized as zero already.
    // [5. Select tool ]: NOT SUPPORTED. Only tracks tool value.
    //	gc_state.tool = gc_block.values.t;
    // [6. Change tool ]: NOT SUPPORTED
    if (gc_block.modal.tool_change == ToolChange::Enable) {
        user_tool_change(gc_state.tool);
    }
    // [7. Spindle control ]:
    if (gc_state.modal.spindle != gc_block.modal.spindle) {
        // Update spindle control and apply spindle speed when enabling it in this block.
        // NOTE: All spindle state changes are synced, even in laser mode. Also, pl_data,
        // rather than gc_state, is used to manage laser state for non-laser motions.
        if (sys.state != State::CheckMode) {
            protocol_buffer_synchronize();
            spindle->setState(gc_block.modal.spindle, (uint32_t)pl_data->spindle_speed);
        }
        report_ovr_counter     = 0;  // Set to report change immediately
        gc_state.modal.spindle = gc_block.modal.spindle;
    }
    pl_data->spindle = gc_state.modal.spindle;
    // [8. Coolant control ]:
    // At most one of M7, M8, M9 can appear in a GCode block, but the overall coolant
    // state can have both mist (M7) and flood (M8) on at once, by issuing M7 and M8
    // in separate blocks.  There is no GCode way to turn them off separately, but
    // you can turn them off simultaneously with M9.  You can turn them off separately
    // with real-time overrides, but that is out of the scope of GCode.
    if (gc_block.coolant != GCodeCoolant::None) {
        switch (gc_block.coolant) {
            case GCodeCoolant::None:
                break;
            case GCodeCoolant::M7:
                gc_state.modal.coolant.Mist = mantissa == 10 ? 0 : 1;
                break;
            case GCodeCoolant::M8:
                gc_state.modal.coolant.Flood = mantissa == 10 ? 0 : 1;
                break;
            case GCodeCoolant::M9:
                gc_state.modal.coolant = {};
                break;
        }
        if (sys.state != State::CheckMode) {
            protocol_buffer_synchronize();
            config->_coolant->set_state(gc_state.modal.coolant);
            report_ovr_counter = 0;  // Set to report change immediately
        }
    }

    pl_data->coolant = gc_state.modal.coolant;  // Set state for planner use.
    // turn on/off an i/o pin
    if ((gc_block.modal.io_control == IoControl::DigitalOnSync) || (gc_block.modal.io_control == IoControl::DigitalOffSync) ||
        (gc_block.modal.io_control == IoControl::DigitalOnImmediate) || (gc_block.modal.io_control == IoControl::DigitalOffImmediate)) {
        if (gc_block.values.p < MaxUserDigitalPin) {
            if ((gc_block.modal.io_control == IoControl::DigitalOnSync) || (gc_block.modal.io_control == IoControl::DigitalOffSync)) {
                protocol_buffer_synchronize();
            }
            bool turnOn = gc_block.modal.io_control == IoControl::DigitalOnSync || gc_block.modal.io_control == IoControl::DigitalOnImmediate;
            if (!config->_userOutputs->setDigital((int)gc_block.values.p, turnOn)) {
                FAIL(Error::PParamMaxExceeded);
            }
        } else {
            FAIL(Error::PParamMaxExceeded);
        }
    }
    if ((gc_block.modal.io_control == IoControl::SetAnalogSync) || (gc_block.modal.io_control == IoControl::SetAnalogImmediate)) {
        if (gc_block.values.e < MaxUserDigitalPin) {
            if (gc_block.values.q < 0.0f) {
                gc_block.values.q = 0.0f;
            } else if (gc_block.values.q > 100.0f) {
                gc_block.values.q = 100.0f;
            }
            if (gc_block.modal.io_control == IoControl::SetAnalogSync) {
                protocol_buffer_synchronize();
            }
            if (!config->_userOutputs->setAnalogPercent((int)gc_block.values.e, gc_block.values.q)) {
                FAIL(Error::PParamMaxExceeded);
            }
        } else {
            FAIL(Error::PParamMaxExceeded);
        }
    }

    // [9. Override control ]: NOT SUPPORTED. Always enabled, except for parking control.
    if (config->_enableParkingOverrideControl) {
        if (gc_state.modal.override != gc_block.modal.override) {
            gc_state.modal.override = gc_block.modal.override;
            mc_override_ctrl_update(gc_state.modal.override);
        }
    }

    // [10. Dwell ]:
    if (gc_block.non_modal_command == NonModal::Dwell) {
        mc_dwell(int32_t(gc_block.values.p * 1000.0f));
    }
    // [11. Set active plane ]:
    gc_state.modal.plane_select = gc_block.modal.plane_select;
    // [12. Set length units ]:
    gc_state.modal.units = gc_block.modal.units;
    // [13. Cutter radius compensation ]: G41/42 NOT SUPPORTED
    // gc_state.modal.cutter_comp = gc_block.modal.cutter_comp; // NOTE: Not needed since always disabled.
    // [14. Cutter length compensation ]: G43.1 and G49 supported. G43 NOT SUPPORTED.
    // NOTE: If G43 were supported, its operation wouldn't be any different from G43.1 in terms
    // of execution. The error-checking step would simply load the offset value into the correct
    // axis of the block XYZ value array.
    if (axis_command == AxisCommand::ToolLengthOffset) {  // Indicates a change.
        gc_state.modal.tool_length = gc_block.modal.tool_length;
        if (gc_state.modal.tool_length == ToolLengthOffset::Cancel) {  // G49
            gc_block.values.xyz[TOOL_LENGTH_OFFSET_AXIS] = 0.0;
        }
        // else G43.1
        if (gc_state.tool_length_offset != gc_block.values.xyz[TOOL_LENGTH_OFFSET_AXIS]) {
            gc_state.tool_length_offset = gc_block.values.xyz[TOOL_LENGTH_OFFSET_AXIS];
        }
    }
    // [15. Coordinate system selection ]:
    if (gc_state.modal.coord_select != gc_block.modal.coord_select) {
        gc_state.modal.coord_select = gc_block.modal.coord_select;
        copyAxes(gc_state.coord_system, block_coord_system);
        gc_wco_changed();
    }
    // [16. Set path control mode ]: G61.1/G64 NOT SUPPORTED
    // gc_state.modal.control = gc_block.modal.control; // NOTE: Always default.
    // [17. Set distance mode ]:
    gc_state.modal.distance = gc_block.modal.distance;
    // [18. Set retract mode ]: NOT SUPPORTED
    // [19. Go to predefined position, Set G10, or Set axis offsets ]:
    switch (gc_block.non_modal_command) {
        case NonModal::SetCoordinateData:
            coords[coord_select]->set(coord_data);
            // Update system coordinate system if currently active.
            if (gc_state.modal.coord_select == coord_select) {
                copyAxes(gc_state.coord_system, coord_data);
                gc_wco_changed();
            }
            break;
        case NonModal::GoHome0:
        case NonModal::GoHome1:
            // Move to intermediate position before going home. Obeys current coordinate system and offsets
            // and absolute and incremental modes.
            pl_data->motion.rapidMotion = 1;  // Set rapid motion flag.
            if (axis_command != AxisCommand::None) {
                mc_linear(gc_block.values.xyz, pl_data, gc_state.position);
            }
            mc_linear(coord_data, pl_data, gc_state.position);
            copyAxes(gc_state.position, coord_data);
            break;
        case NonModal::SetHome0:
            coords[CoordIndex::G28]->set(gc_state.position);
            gc_ngc_changed(CoordIndex::G28);
            break;
        case NonModal::SetHome1:
            coords[CoordIndex::G30]->set(gc_state.position);
            gc_ngc_changed(CoordIndex::G30);
            break;
        case NonModal::SetCoordinateOffset:
            copyAxes(gc_state.coord_offset, gc_block.values.xyz);
            gc_ngc_changed(CoordIndex::G92);
            gc_wco_changed();
            break;
        case NonModal::ResetCoordinateOffset:
            clear_vector(gc_state.coord_offset);  // Disable G92 offsets by zeroing offset vector.
            gc_ngc_changed(CoordIndex::G92);
            gc_wco_changed();
            break;
        default:
            break;
    }
    // [20. Motion modes ]:
    // NOTE: Commands G10,G28,G30,G92 lock out and prevent axis words from use in motion modes.
    // Enter motion modes only if there are axis words or a motion mode command word in the block.
    gc_state.modal.motion = gc_block.modal.motion;
    if (gc_state.modal.motion != Motion::None) {
        if (axis_command == AxisCommand::MotionMode) {
            GCUpdatePos gc_update_pos = GCUpdatePos::Target;
            if (gc_state.modal.motion == Motion::Linear) {
                mc_linear(gc_block.values.xyz, pl_data, gc_state.position);
            } else if (gc_state.modal.motion == Motion::Seek) {
                pl_data->motion.rapidMotion = 1;  // Set rapid motion flag.
                mc_linear(gc_block.values.xyz, pl_data, gc_state.position);
            } else if ((gc_state.modal.motion == Motion::CwArc) || (gc_state.modal.motion == Motion::CcwArc)) {
                mc_arc(gc_block.values.xyz,
                       pl_data,
                       gc_state.position,
                       gc_block.values.ijk,
                       gc_block.values.r,
                       axis_0,
                       axis_1,
                       axis_linear,
                       clockwiseArc,
                       int(gc_block.values.p));
            } else {
                // NOTE: gc_block.values.xyz is returned from mc_probe_cycle with the updated position value. So
                // upon a successful probing cycle, the machine position and the returned value should be the same.
                if (!ALLOW_FEED_OVERRIDE_DURING_PROBE_CYCLES) {
                    pl_data->motion.noFeedOverride = 1;
                }
                gc_update_pos = mc_probe_cycle(gc_block.values.xyz, pl_data, probeAway, probeNoError, axis_words, gc_block.values.p);
            }
            // As far as the parser is concerned, the position is now == target. In reality the
            // motion control system might still be processing the action and the real tool position
            // in any intermediate location.
            if (sys.abort) {
                return Error::Reset;
            }
            if (gc_update_pos == GCUpdatePos::Target) {
                copyAxes(gc_state.position, gc_block.values.xyz);
            } else if (gc_update_pos == GCUpdatePos::System) {
                gc_sync_position();
            }  // == GCUpdatePos::None
        }
    }
    // [21. Program flow ]:
    // M0,M1,M2,M30: Perform non-running program flow actions. During a program pause, the buffer may
    // refill and can only be resumed by the cycle start run-time command.
    gc_state.modal.program_flow = gc_block.modal.program_flow;
    switch (gc_state.modal.program_flow) {
        case ProgramFlow::Running:
            break;
        case ProgramFlow::OptionalStop:
            // TODO - to support M1 we would need some code to determine whether to stop
            // Then either break or fall through to actually stop.
            break;
        case ProgramFlow::Paused:
            protocol_buffer_synchronize();  // Sync and finish all remaining buffered motions before moving on.
            if (sys.state != State::CheckMode) {
                protocol_send_event(&feedHoldEvent);
                protocol_execute_realtime();  // Execute suspend.
            }
            break;
        case ProgramFlow::CompletedM2:
        case ProgramFlow::CompletedM30:
            protocol_buffer_synchronize();  // Sync and finish all remaining buffered motions before moving on.

            // Upon program complete, only a subset of g-codes reset to certain defaults, according to
            // LinuxCNC's program end descriptions and testing. Only modal groups [G-code 1,2,3,5,7,12]
            // and [M-code 7,8,9] reset to [G1,G17,G90,G94,G40,G54,M5,M9,M48]. The remaining modal groups
            // [G-code 4,6,8,10,13,14,15] and [M-code 4,5,6] and the modal words [F,S,T,H] do not reset.
            gc_state.modal.motion       = Motion::Linear;
            gc_state.modal.plane_select = Plane::XY;
            gc_state.modal.distance     = Distance::Absolute;
            gc_state.modal.feed_rate    = FeedRate::UnitsPerMin;
            // gc_state.modal.cutter_comp = CutterComp::Disable; // Not supported.
            gc_state.modal.coord_select = CoordIndex::G54;
            gc_state.modal.spindle      = SpindleState::Disable;
            gc_state.modal.coolant      = {};
            if (config->_enableParkingOverrideControl) {
                if (config->_start->_deactivateParking) {
                    gc_state.modal.override = Override::Disabled;
                } else {
                    gc_state.modal.override = Override::ParkingMotion;
                }
            }

            // gc_state.modal.override = OVERRIDE_DISABLE; // Not supported.
            if (RESTORE_OVERRIDES_AFTER_PROGRAM_END) {
                sys.f_override        = FeedOverride::Default;
                sys.r_override        = RapidOverride::Default;
                sys.spindle_speed_ovr = SpindleSpeedOverride::Default;
            }

            // Execute coordinate change and spindle/coolant stop.
            if (sys.state != State::CheckMode) {
                coords[gc_state.modal.coord_select]->get(gc_state.coord_system);
                gc_wco_changed();  // Set to refresh immediately just in case something altered.
                spindle->spinDown();
                config->_coolant->off();
                report_ovr_counter = 0;  // Set to report changes immediately
            }
            report_feedback_message(Message::ProgramEnd);
            user_m30();
            break;
    }
    gc_state.modal.program_flow = ProgramFlow::Running;  // Reset program flow.

    // TODO: % to denote start of program.
    return Error::Ok;
}

/*
  Not supported:

  - Canned cycles
  - Tool radius compensation
  - A,B,C-axes
  - Evaluation of expressions
  - Variables
  - Override control (TBD)
  - Tool changes
  - Switches

   (*) Indicates optional parameter, enabled through config.h and re-compile
   group 0 = {G92.2, G92.3} (Non modal: Cancel and re-enable G92 offsets)
   group 1 = {G81 - G89} (Motion modes: Canned cycles)
   group 4 = {M1} (Optional stop, ignored)
   group 6 = {M6} (Tool change)
   group 7 = {G41, G42} cutter radius compensation (G40 is supported)
   group 8 = {G43} tool length offset (G43.1/G49 are supported)
   group 8 = {M7*} enable mist coolant (* Compile-option)
   group 9 = {M48, M49} enable/disable feed and speed override switches
   group 10 = {G98, G99} return mode canned cycles
   group 13 = {G61.1, G64} path control mode (G61 is supported)
*/

void WEAK_LINK user_m30() {}

void WEAK_LINK user_tool_change(uint32_t new_tool) {
    Spindles::Spindle::switchSpindle(new_tool, config->_spindles, spindle);
    report_ovr_counter = 0;  // Set to report change immediately
}
