#include "TS35Model.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace ts35 {

    namespace {

        constexpr std::size_t MaxLineCommandLength = 240;

        std::size_t axisIndex(Axis axis) {
            return static_cast<std::size_t>(axis);
        }

        bool isValidAxis(Axis axis) {
            return axisIndex(axis) < AxisCount;
        }

        char axisLetter(std::size_t index) {
            static const char letters[AxisCount] = { 'X', 'Y', 'Z', 'A', 'B', 'C' };
            return index < AxisCount ? letters[index] : '?';
        }

        std::string trim(const std::string& text) {
            std::size_t first = 0;
            while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
                ++first;
            }

            std::size_t last = text.size();
            while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
                --last;
            }
            return text.substr(first, last - first);
        }

        std::string upper(const std::string& text) {
            std::string result(text);
            for (std::size_t i = 0; i < result.size(); ++i) {
                result[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[i])));
            }
            return result;
        }

        bool startsWithCaseInsensitive(const std::string& text, const char* prefix) {
            std::size_t index = 0;
            while (prefix[index] != '\0') {
                if (index >= text.size()) {
                    return false;
                }
                const unsigned char lhs = static_cast<unsigned char>(text[index]);
                const unsigned char rhs = static_cast<unsigned char>(prefix[index]);
                if (std::toupper(lhs) != std::toupper(rhs)) {
                    return false;
                }
                ++index;
            }
            return true;
        }

        std::vector<std::string> split(const std::string& text, char delimiter) {
            std::vector<std::string> result;
            std::size_t              start = 0;
            while (true) {
                const std::size_t end = text.find(delimiter, start);
                if (end == std::string::npos) {
                    result.push_back(text.substr(start));
                    break;
                }
                result.push_back(text.substr(start, end - start));
                start = end + 1;
            }
            return result;
        }

        bool parseDouble(const std::string& text, double& value) {
            const std::string cleaned = trim(text);
            if (cleaned.empty()) {
                return false;
            }
            bool sawDigit = false;
            for (const unsigned char character : cleaned) {
                if (std::isdigit(character)) {
                    sawDigit = true;
                } else if (character != '+' && character != '-' && character != '.' && character != 'e' && character != 'E') {
                    return false;
                }
            }
            if (!sawDigit) {
                return false;
            }

            errno     = 0;
            char* end = nullptr;
            value     = std::strtod(cleaned.c_str(), &end);
            return errno != ERANGE && end != cleaned.c_str() && *end == '\0' && std::isfinite(value);
        }

        bool parseUnsigned(const std::string& text, std::uint32_t& value) {
            const std::string cleaned = trim(text);
            if (cleaned.empty()) {
                return false;
            }
            for (std::size_t i = 0; i < cleaned.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(cleaned[i]))) {
                    return false;
                }
            }

            errno                      = 0;
            char*               end    = nullptr;
            const unsigned long parsed = std::strtoul(cleaned.c_str(), &end, 10);
            if (errno == ERANGE || end == cleaned.c_str() || *end != '\0' || parsed > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            value = static_cast<std::uint32_t>(parsed);
            return true;
        }

        bool parseInt(const std::string& text, int& value) {
            const std::string cleaned = trim(text);
            if (cleaned.empty()) {
                return false;
            }

            std::size_t index = (cleaned[0] == '+' || cleaned[0] == '-') ? 1 : 0;
            if (index == cleaned.size()) {
                return false;
            }
            for (; index < cleaned.size(); ++index) {
                if (!std::isdigit(static_cast<unsigned char>(cleaned[index]))) {
                    return false;
                }
            }

            errno             = 0;
            char*      end    = nullptr;
            const long parsed = std::strtol(cleaned.c_str(), &end, 10);
            if (errno == ERANGE || end == cleaned.c_str() || *end != '\0' || parsed < std::numeric_limits<int>::min() ||
                parsed > std::numeric_limits<int>::max()) {
                return false;
            }
            value = static_cast<int>(parsed);
            return true;
        }

        bool parseAxisValues(const std::string& text, AxisValues& result) {
            const std::vector<std::string> words = split(text, ',');
            if (words.empty() || words.size() > AxisCount) {
                return false;
            }

            AxisValues parsed;
            for (std::size_t i = 0; i < words.size(); ++i) {
                double value = 0.0;
                if (!parseDouble(words[i], value)) {
                    return false;
                }
                parsed.value[i] = value;
                parsed.valid[i] = true;
            }
            result = parsed;
            return true;
        }

        bool parseTwoDoubles(const std::string& text, double& first, double& second) {
            const std::vector<std::string> words = split(text, ',');
            return words.size() == 2 && parseDouble(words[0], first) && parseDouble(words[1], second);
        }

        bool parseTwoUnsigned(const std::string& text, std::uint32_t& first, std::uint32_t& second) {
            const std::vector<std::string> words = split(text, ',');
            return words.size() == 2 && parseUnsigned(words[0], first) && parseUnsigned(words[1], second);
        }

        bool parseThreeUnsigned(const std::string& text, std::uint32_t& first, std::uint32_t& second, std::uint32_t& third) {
            const std::vector<std::string> words = split(text, ',');
            return words.size() == 3 && parseUnsigned(words[0], first) && parseUnsigned(words[1], second) && parseUnsigned(words[2], third);
        }

        MachineState parseMachineState(const std::string& rawState, int& substate) {
            const std::size_t colon = rawState.find(':');
            const std::string name  = upper(trim(rawState.substr(0, colon)));
            substate                = -1;
            if (colon != std::string::npos) {
                int parsed = -1;
                if (parseInt(rawState.substr(colon + 1), parsed)) {
                    substate = parsed;
                }
            }

            if (name == "IDLE") {
                return MachineState::Idle;
            }
            if (name == "RUN" || name == "CYCLE") {
                return MachineState::Cycle;
            }
            if (name == "HOLD" || name == "HELD") {
                return MachineState::Hold;
            }
            if (name == "JOG") {
                return MachineState::Jog;
            }
            if (name == "ALARM") {
                return MachineState::Alarm;
            }
            if (name == "DOOR" || name == "SAFETYDOOR") {
                return MachineState::SafetyDoor;
            }
            if (name == "CHECK" || name == "CHECKMODE") {
                return MachineState::Check;
            }
            if (name == "HOME" || name == "HOMING") {
                return MachineState::Homing;
            }
            if (name == "SLEEP") {
                return MachineState::Sleep;
            }
            if (name == "CONFIGALARM") {
                return MachineState::ConfigAlarm;
            }
            if (name == "CRITICAL") {
                return MachineState::Critical;
            }
            if (name == "START" || name == "STARTING") {
                return MachineState::Starting;
            }
            return MachineState::Unknown;
        }

        void synchronizePositions(ModelSnapshot& snapshot, bool sawMpos, bool sawWpos, bool sawWco) {
            for (std::size_t axis = 0; axis < AxisCount; ++axis) {
                // A WCO present in this report is authoritative. FluidNC normally emits
                // only MPos or WPos, with WCO at a slower cadence.
                if (sawWco && snapshot.wco.valid[axis]) {
                    if (sawMpos && snapshot.mpos.valid[axis]) {
                        snapshot.wpos.value[axis] = snapshot.mpos.value[axis] - snapshot.wco.value[axis];
                        snapshot.wpos.valid[axis] = true;
                    } else if (sawWpos && snapshot.wpos.valid[axis]) {
                        snapshot.mpos.value[axis] = snapshot.wpos.value[axis] + snapshot.wco.value[axis];
                        snapshot.mpos.valid[axis] = true;
                    } else if (snapshot.mpos.valid[axis]) {
                        snapshot.wpos.value[axis] = snapshot.mpos.value[axis] - snapshot.wco.value[axis];
                        snapshot.wpos.valid[axis] = true;
                    } else if (snapshot.wpos.valid[axis]) {
                        snapshot.mpos.value[axis] = snapshot.wpos.value[axis] + snapshot.wco.value[axis];
                        snapshot.mpos.valid[axis] = true;
                    }
                    continue;
                }

                if (sawMpos && sawWpos && snapshot.mpos.valid[axis] && snapshot.wpos.valid[axis]) {
                    snapshot.wco.value[axis] = snapshot.mpos.value[axis] - snapshot.wpos.value[axis];
                    snapshot.wco.valid[axis] = true;
                } else if (sawMpos && snapshot.mpos.valid[axis] && snapshot.wco.valid[axis]) {
                    snapshot.wpos.value[axis] = snapshot.mpos.value[axis] - snapshot.wco.value[axis];
                    snapshot.wpos.valid[axis] = true;
                } else if (sawWpos && snapshot.wpos.valid[axis] && snapshot.wco.valid[axis]) {
                    snapshot.mpos.value[axis] = snapshot.wpos.value[axis] + snapshot.wco.value[axis];
                    snapshot.mpos.valid[axis] = true;
                }
            }
        }

        void applyModalWord(const std::string& modalWord, ModalState& modal) {
            const std::string word = upper(modalWord);
            if (word == "G54") {
                modal.coordinateSystem = CoordinateSystem::G54;
            } else if (word == "G55") {
                modal.coordinateSystem = CoordinateSystem::G55;
            } else if (word == "G56") {
                modal.coordinateSystem = CoordinateSystem::G56;
            } else if (word == "G57") {
                modal.coordinateSystem = CoordinateSystem::G57;
            } else if (word == "G58") {
                modal.coordinateSystem = CoordinateSystem::G58;
            } else if (word == "G59") {
                modal.coordinateSystem = CoordinateSystem::G59;
            } else if (word == "G59.1") {
                modal.coordinateSystem = CoordinateSystem::G59_1;
            } else if (word == "G59.2") {
                modal.coordinateSystem = CoordinateSystem::G59_2;
            } else if (word == "G59.3") {
                modal.coordinateSystem = CoordinateSystem::G59_3;
            } else if (word == "G20") {
                modal.units = Units::Inches;
            } else if (word == "G21") {
                modal.units = Units::Millimeters;
            } else if (word == "G90") {
                modal.distanceMode = DistanceMode::Absolute;
            } else if (word == "G91") {
                modal.distanceMode = DistanceMode::Incremental;
            }
        }

        std::string formatNumber(double value) {
            if (value == 0.0) {
                value = 0.0;  // Normalize negative zero.
            }

            char      buffer[48];
            const int written = std::snprintf(buffer, sizeof(buffer), "%.6f", value);
            if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
                return std::string();
            }
            std::string result(buffer);
            while (!result.empty() && result[result.size() - 1] == '0') {
                result.erase(result.size() - 1);
            }
            if (!result.empty() && result[result.size() - 1] == '.') {
                result.erase(result.size() - 1);
            }
            if (result == "-0") {
                result = "0";
            }
            return result;
        }

        CommandBuildResult commandFailure() {
            return CommandBuildResult();
        }

        CommandBuildResult commandSuccess(const std::string& command) {
            CommandBuildResult result;
            result.ok      = true;
            result.command = command;
            return result;
        }

    }  // namespace

    AxisValues::AxisValues() : value(), valid() {}

    bool AxisValues::has(Axis axis) const {
        return isValidAxis(axis) && valid[axisIndex(axis)];
    }

    double AxisValues::get(Axis axis) const {
        return has(axis) ? value[axisIndex(axis)] : 0.0;
    }

    FeedSpindle::FeedSpindle() : feed(0.0), spindle(0.0), valid(false) {}

    Overrides::Overrides() : feed(100), rapid(100), spindle(100), valid(false) {}

    PinState::PinState() : valid(false), raw() {}

    SdProgress::SdProgress() : valid(false), percent(0.0) {}

    BufferState::BufferState() : valid(false), plannerAvailable(0), rxAvailable(0) {}

    LineNumber::LineNumber() : valid(false), value(0) {}

    ModalState::ModalState() : coordinateSystem(CoordinateSystem::Unknown), units(Units::Unknown), distanceMode(DistanceMode::Unknown) {}

    Reply::Reply() : kind(ReplyKind::None), hasCode(false), code(0), text() {}

    ModelSnapshot::ModelSnapshot() :
        state(MachineState::Unknown), rawState(), stateSubstate(-1), mpos(), wpos(), wco(), fs(), overrides(), pins(), sd(), buffers(),
        lineNumber(), modal(), lastReply(), sequence(0) {}

    AxisMove::AxisMove(Axis axisValue, double millimetersValue) : axis(axisValue), millimeters(millimetersValue) {}

    CommandBuildResult::CommandBuildResult() : ok(false), command() {}

    CommandBuildResult buildSetWorkZeroCommand(const std::vector<Axis>& axes) {
        if (axes.empty()) {
            return commandFailure();
        }

        std::array<bool, AxisCount> selected = {};
        for (std::size_t i = 0; i < axes.size(); ++i) {
            if (!isValidAxis(axes[i])) {
                return commandFailure();
            }
            const std::size_t index = axisIndex(axes[i]);
            if (selected[index]) {
                return commandFailure();
            }
            selected[index] = true;
        }

        std::string command("G10 L20 P0");
        for (std::size_t axis = 0; axis < AxisCount; ++axis) {
            if (selected[axis]) {
                command += " ";
                command += axisLetter(axis);
                command += "0";
            }
        }
        return commandSuccess(command);
    }

    CommandBuildResult buildIncrementalJogCommand(const std::vector<AxisMove>& moves, double feedMmPerMinute) {
        if (moves.empty()) {
            return commandFailure();
        }
        if (!std::isfinite(feedMmPerMinute) || feedMmPerMinute <= 0.0) {
            return commandFailure();
        }

        std::array<bool, AxisCount>   selected  = {};
        std::array<double, AxisCount> distances = {};
        for (std::size_t i = 0; i < moves.size(); ++i) {
            if (!isValidAxis(moves[i].axis)) {
                return commandFailure();
            }
            if (!std::isfinite(moves[i].millimeters) || moves[i].millimeters == 0.0) {
                return commandFailure();
            }
            const std::size_t index = axisIndex(moves[i].axis);
            if (selected[index]) {
                return commandFailure();
            }
            const std::string formatted = formatNumber(moves[i].millimeters);
            if (formatted == "0") {
                return commandFailure();
            }
            selected[index]  = true;
            distances[index] = moves[i].millimeters;
        }

        std::string command("$J=G91 G21");
        for (std::size_t axis = 0; axis < AxisCount; ++axis) {
            if (selected[axis]) {
                command += " ";
                command += axisLetter(axis);
                command += formatNumber(distances[axis]);
            }
        }
        command += " F";
        command += formatNumber(feedMmPerMinute);
        if (command.size() > MaxLineCommandLength) {
            return commandFailure();
        }
        return commandSuccess(command);
    }

    TS35Model::TS35Model() : snapshot_() {}

    const ModelSnapshot& TS35Model::snapshot() const {
        return snapshot_;
    }

    bool TS35Model::parseLine(const std::string& input, Reply* parsedReply) {
        const std::string line = trim(input);
        if (line.empty()) {
            return false;
        }

        if (line[0] == '<') {
            if (line.size() < 3 || line[line.size() - 1] != '>') {
                return false;
            }
            const std::vector<std::string> fields = split(line.substr(1, line.size() - 2), '|');
            if (fields.empty() || trim(fields[0]).empty()) {
                return false;
            }

            ModelSnapshot next = snapshot_;
            next.rawState      = trim(fields[0]);
            next.state         = parseMachineState(next.rawState, next.stateSubstate);

            // Pn/SD/Ln are omitted when inactive, unlike slower-cadence WCO and
            // Ov fields. Reset them for every complete status report.
            next.pins       = PinState();
            next.sd         = SdProgress();
            next.lineNumber = LineNumber();

            bool sawMpos = false;
            bool sawWpos = false;
            bool sawWco  = false;

            for (std::size_t i = 1; i < fields.size(); ++i) {
                const std::string field = trim(fields[i]);
                const std::size_t colon = field.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                const std::string tag   = trim(field.substr(0, colon));
                const std::string value = trim(field.substr(colon + 1));

                if (tag == "MPos") {
                    AxisValues parsed;
                    if (parseAxisValues(value, parsed)) {
                        next.mpos = parsed;
                        sawMpos   = true;
                    }
                } else if (tag == "WPos") {
                    AxisValues parsed;
                    if (parseAxisValues(value, parsed)) {
                        next.wpos = parsed;
                        sawWpos   = true;
                    }
                } else if (tag == "WCO") {
                    AxisValues parsed;
                    if (parseAxisValues(value, parsed)) {
                        next.wco = parsed;
                        sawWco   = true;
                    }
                } else if (tag == "FS") {
                    double feed    = 0.0;
                    double spindle = 0.0;
                    if (parseTwoDoubles(value, feed, spindle)) {
                        next.fs.feed    = feed;
                        next.fs.spindle = spindle;
                        next.fs.valid   = true;
                    }
                } else if (tag == "Ov") {
                    std::uint32_t feed    = 0;
                    std::uint32_t rapid   = 0;
                    std::uint32_t spindle = 0;
                    if (parseThreeUnsigned(value, feed, rapid, spindle)) {
                        next.overrides.feed    = feed;
                        next.overrides.rapid   = rapid;
                        next.overrides.spindle = spindle;
                        next.overrides.valid   = true;
                    }
                } else if (tag == "Pn") {
                    next.pins.valid = true;
                    next.pins.raw   = value;
                } else if (tag == "SD") {
                    const std::size_t comma   = value.find(',');
                    double            percent = 0.0;
                    if (comma != std::string::npos && parseDouble(value.substr(0, comma), percent)) {
                        next.sd.valid   = true;
                        next.sd.percent = percent;
                    }
                } else if (tag == "Bf") {
                    std::uint32_t planner = 0;
                    std::uint32_t rx      = 0;
                    if (parseTwoUnsigned(value, planner, rx)) {
                        next.buffers.valid            = true;
                        next.buffers.plannerAvailable = planner;
                        next.buffers.rxAvailable      = rx;
                    }
                } else if (tag == "Ln") {
                    std::uint32_t number = 0;
                    if (parseUnsigned(value, number)) {
                        next.lineNumber.valid = true;
                        next.lineNumber.value = number;
                    }
                }
            }

            synchronizePositions(next, sawMpos, sawWpos, sawWco);
            next.lastReply.kind    = ReplyKind::Status;
            next.lastReply.hasCode = false;
            next.lastReply.code    = 0;
            next.lastReply.text    = line;
            next.sequence          = snapshot_.sequence + 1;
            snapshot_              = next;
        } else if (startsWithCaseInsensitive(line, "[GC:")) {
            if (line[line.size() - 1] != ']') {
                return false;
            }
            const std::string body     = line.substr(4, line.size() - 5);
            std::size_t       position = 0;
            while (position < body.size()) {
                while (position < body.size() && std::isspace(static_cast<unsigned char>(body[position]))) {
                    ++position;
                }
                const std::size_t start = position;
                while (position < body.size() && !std::isspace(static_cast<unsigned char>(body[position]))) {
                    ++position;
                }
                if (position > start) {
                    applyModalWord(body.substr(start, position - start), snapshot_.modal);
                }
            }
            snapshot_.lastReply.kind    = ReplyKind::Modal;
            snapshot_.lastReply.hasCode = false;
            snapshot_.lastReply.code    = 0;
            snapshot_.lastReply.text    = line;
            ++snapshot_.sequence;
        } else if (startsWithCaseInsensitive(line, "[MSG:")) {
            if (line[line.size() - 1] != ']') {
                return false;
            }
            snapshot_.lastReply.kind    = ReplyKind::Message;
            snapshot_.lastReply.hasCode = false;
            snapshot_.lastReply.code    = 0;
            snapshot_.lastReply.text    = line.substr(5, line.size() - 6);
            ++snapshot_.sequence;
        } else if (upper(line) == "OK") {
            snapshot_.lastReply.kind    = ReplyKind::Ok;
            snapshot_.lastReply.hasCode = false;
            snapshot_.lastReply.code    = 0;
            snapshot_.lastReply.text    = line;
            ++snapshot_.sequence;
        } else if (startsWithCaseInsensitive(line, "ERROR:")) {
            int code                    = 0;
            snapshot_.lastReply.kind    = ReplyKind::Error;
            snapshot_.lastReply.hasCode = parseInt(line.substr(6), code);
            snapshot_.lastReply.code    = snapshot_.lastReply.hasCode ? code : 0;
            snapshot_.lastReply.text    = line;
            ++snapshot_.sequence;
        } else if (startsWithCaseInsensitive(line, "ALARM:")) {
            int code                    = 0;
            snapshot_.state             = MachineState::Alarm;
            snapshot_.rawState          = "Alarm";
            snapshot_.stateSubstate     = -1;
            snapshot_.lastReply.kind    = ReplyKind::Alarm;
            snapshot_.lastReply.hasCode = parseInt(line.substr(6), code);
            snapshot_.lastReply.code    = snapshot_.lastReply.hasCode ? code : 0;
            snapshot_.lastReply.text    = line;
            ++snapshot_.sequence;
        } else {
            return false;
        }

        if (parsedReply != nullptr) {
            *parsedReply = snapshot_.lastReply;
        }
        return true;
    }

    bool TS35Model::canSendLineCommand(const std::string& command) const {
        const std::string cleaned = trim(command);
        if (cleaned.empty() || cleaned.size() > MaxLineCommandLength) {
            return false;
        }
        for (std::size_t i = 0; i < cleaned.size(); ++i) {
            const unsigned char character = static_cast<unsigned char>(cleaned[i]);
            if (character < 0x20 || character == 0x7f) {
                return false;
            }
        }
        return snapshot_.state == MachineState::Idle;
    }

    bool TS35Model::canSendRealtime(RealtimeAction action) const {
        switch (action) {
            case RealtimeAction::FeedHold:
                return snapshot_.state == MachineState::Cycle || snapshot_.state == MachineState::Jog;

            case RealtimeAction::Resume:
                return snapshot_.state == MachineState::Hold || snapshot_.state == MachineState::SafetyDoor;

            case RealtimeAction::CancelJog:
                return snapshot_.state == MachineState::Jog;

            case RealtimeAction::EmergencyReset:
                switch (snapshot_.state) {
                    case MachineState::Cycle:
                    case MachineState::Hold:
                    case MachineState::Jog:
                    case MachineState::Alarm:
                    case MachineState::SafetyDoor:
                    case MachineState::Check:
                    case MachineState::Homing:
                    case MachineState::Sleep:
                    case MachineState::ConfigAlarm:
                    case MachineState::Critical:
                        return true;
                    default:
                        return false;
                }
        }
        return false;
    }

    std::uint8_t TS35Model::realtimeByte(RealtimeAction action) {
        switch (action) {
            case RealtimeAction::FeedHold:
                return static_cast<std::uint8_t>('!');
            case RealtimeAction::Resume:
                return static_cast<std::uint8_t>('~');
            case RealtimeAction::CancelJog:
                return 0x85;
            case RealtimeAction::EmergencyReset:
                return 0x18;
        }
        return 0;
    }

    const char* machineStateName(MachineState state) {
        switch (state) {
            case MachineState::Idle:
                return "Idle";
            case MachineState::Cycle:
                return "Cycle";
            case MachineState::Hold:
                return "Hold";
            case MachineState::Jog:
                return "Jog";
            case MachineState::Alarm:
                return "Alarm";
            case MachineState::SafetyDoor:
                return "SafetyDoor";
            case MachineState::Check:
                return "Check";
            case MachineState::Homing:
                return "Homing";
            case MachineState::Sleep:
                return "Sleep";
            case MachineState::ConfigAlarm:
                return "ConfigAlarm";
            case MachineState::Critical:
                return "Critical";
            case MachineState::Starting:
                return "Starting";
            case MachineState::Unknown:
            default:
                return "Unknown";
        }
    }

    const char* coordinateSystemName(CoordinateSystem coordinateSystem) {
        switch (coordinateSystem) {
            case CoordinateSystem::G54:
                return "G54";
            case CoordinateSystem::G55:
                return "G55";
            case CoordinateSystem::G56:
                return "G56";
            case CoordinateSystem::G57:
                return "G57";
            case CoordinateSystem::G58:
                return "G58";
            case CoordinateSystem::G59:
                return "G59";
            case CoordinateSystem::G59_1:
                return "G59.1";
            case CoordinateSystem::G59_2:
                return "G59.2";
            case CoordinateSystem::G59_3:
                return "G59.3";
            case CoordinateSystem::Unknown:
            default:
                return "Unknown";
        }
    }

}  // namespace ts35
