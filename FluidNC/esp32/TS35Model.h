#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Report parsing and command building, kept free of Arduino, ESP-IDF, FreeRTOS
// and FluidNC dependencies so that the protocol rules stay separate from the
// panel and from the module wiring.
namespace ts35 {

    constexpr std::size_t AxisCount = 6;

    enum class Axis : std::uint8_t {
        X = 0,
        Y,
        Z,
        A,
        B,
        C,
    };

    enum class MachineState : std::uint8_t {
        Unknown = 0,
        Idle,
        Cycle,
        Hold,
        Jog,
        Alarm,
        SafetyDoor,
        Check,
        Homing,
        Sleep,
        ConfigAlarm,
        Critical,
        Starting,
    };

    enum class CoordinateSystem : std::uint8_t {
        Unknown = 0,
        G54,
        G55,
        G56,
        G57,
        G58,
        G59,
        G59_1,
        G59_2,
        G59_3,
    };

    enum class Units : std::uint8_t {
        Unknown = 0,
        Millimeters,
        Inches,
    };

    enum class DistanceMode : std::uint8_t {
        Unknown = 0,
        Absolute,
        Incremental,
    };

    enum class ReplyKind : std::uint8_t {
        None = 0,
        Status,
        Modal,
        Ok,
        Error,
        Alarm,
        Message,
    };

    struct AxisValues {
        std::array<double, AxisCount> value;
        std::array<bool, AxisCount>   valid;

        AxisValues();
        bool   has(Axis axis) const;
        double get(Axis axis) const;
    };

    struct FeedSpindle {
        double feed;
        double spindle;
        bool   valid;

        FeedSpindle();
    };

    struct Overrides {
        std::uint32_t feed;
        std::uint32_t rapid;
        std::uint32_t spindle;
        bool          valid;

        Overrides();
    };

    // The Pn: field is shown verbatim, so the individual switch letters are not
    // broken out here.
    struct PinState {
        bool        valid;
        std::string raw;

        PinState();
    };

    struct SdProgress {
        bool   valid;
        double percent;

        SdProgress();
    };

    struct BufferState {
        bool          valid;
        std::uint32_t plannerAvailable;
        std::uint32_t rxAvailable;

        BufferState();
    };

    struct LineNumber {
        bool          valid;
        std::uint32_t value;

        LineNumber();
    };

    struct ModalState {
        CoordinateSystem coordinateSystem;
        Units            units;
        DistanceMode     distanceMode;

        ModalState();
    };

    struct Reply {
        ReplyKind   kind;
        bool        hasCode;
        int         code;
        std::string text;

        Reply();
    };

    struct ModelSnapshot {
        MachineState state;
        std::string  rawState;
        int          stateSubstate;

        AxisValues    mpos;
        AxisValues    wpos;
        AxisValues    wco;
        FeedSpindle   fs;
        Overrides     overrides;
        PinState      pins;
        SdProgress    sd;
        BufferState   buffers;
        LineNumber    lineNumber;
        ModalState    modal;
        Reply         lastReply;
        std::uint64_t sequence;

        ModelSnapshot();
    };

    struct AxisMove {
        Axis   axis;
        double millimeters;

        AxisMove(Axis axisValue, double millimetersValue);
    };

    struct CommandBuildResult {
        bool        ok;
        std::string command;

        CommandBuildResult();
    };

    // Produces a line without CR/LF.  P0 means the currently active coordinate
    // system, so the touchscreen never needs to guess whether G54, G55, etc. is
    // active.  The selected axes are emitted in deterministic X,Y,Z,A,B,C order.
    CommandBuildResult buildSetWorkZeroCommand(const std::vector<Axis>& axes);

    // Produces "$J=G91 G21 ... F..." without CR/LF.  Distances and feed are always
    // expressed in millimeters, independent of the currently active G20/G21 mode.
    CommandBuildResult buildIncrementalJogCommand(const std::vector<AxisMove>& moves, double feedMmPerMinute);

    enum class RealtimeAction : std::uint8_t {
        FeedHold = 0,
        Resume,
        CancelJog,
        EmergencyReset,
    };

    class TS35Model {
    public:
        TS35Model();

        const ModelSnapshot& snapshot() const;

        // Parses one complete GRBL/FluidNC line. CR/LF and surrounding whitespace
        // are accepted. On false, the snapshot and optional parsedReply are left
        // unchanged.
        bool parseLine(const std::string& line, Reply* parsedReply = nullptr);

        // Line commands are intentionally gated to Idle. This also rejects empty,
        // multiline, control-character, and excessively long commands.
        bool canSendLineCommand(const std::string& command) const;

        // Realtime actions have their own state policy and are never serialized as
        // line commands. EmergencyReset is explicitly named because Ctrl-X resets
        // controller execution; it is not the same operation as CancelJog (0x85).
        bool                canSendRealtime(RealtimeAction action) const;
        static std::uint8_t realtimeByte(RealtimeAction action);

    private:
        ModelSnapshot snapshot_;
    };

    const char* machineStateName(MachineState state);
    const char* coordinateSystemName(CoordinateSystem coordinateSystem);

}  // namespace ts35
