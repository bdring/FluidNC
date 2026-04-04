#include "Stage1HostSupport.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ArduinoOTA.h"
#include "Channel.h"
#include "Configuration/AfterParse.h"
#include "Configuration/Validator.h"
#include "Control.h"
#include "FileStream.h"
#include "FluidPath.h"
#include "GCode.h"
#include "JSONEncoder.h"
#include "Kinematics/Kinematics.h"
#include "Logging.h"
#include "Machine/MachineConfig.h"
#include "Parking.h"
#include "Probe.h"
#include "SDCard.h"
#include "Serial.h"
#include "Settings.h"
#include "SettingsDefinitions.h"
#include "Spindles/NullSpindle.h"
#include "Spindles/Spindle.h"
#include "Stepping.h"
#include "TestStubs.h"
#include "Uart.h"
#include "UartChannel.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "mdns.h"

namespace {
    State g_state = State::Idle;
    bool  g_logFilterEnabled = true;

    class NullChannel final : public Channel {
    public:
        NullChannel() : Channel("null") {}

        size_t write(uint8_t) override {
            return 1;
        }
        size_t write(const uint8_t*, size_t length) override {
            return length;
        }
    };

    NullChannel g_nullChannel;
}

gc_modal_t __attribute__((weak)) modal_defaults {};

StringSetting* config_filename = nullptr;
StringSetting* build_info = nullptr;
StringSetting* start_message = nullptr;
IntSetting*    status_mask = nullptr;
IntSetting*    sd_fallback_cs = nullptr;
EnumSetting*   message_level = nullptr;
EnumSetting*   gcode_echo = nullptr;

const EnumItem messageLevels2[] = { { MsgLevelNone, "None" }, { MsgLevelInfo, "Info" }, EnumItem(MsgLevelInfo) };

Volume SD { "sd", "/sd" };
Volume LocalFS { "localfs", "/localfs" };
Spindles::Spindle* spindle = nullptr;

std::vector<Setting*> Setting::List = {};
std::vector<Command*> Command::List = {};

QueueHandle_t __attribute__((weak)) xQueueGenericCreate(const UBaseType_t, const UBaseType_t, const uint8_t) {
    return nullptr;
}

BaseType_t __attribute__((weak)) xQueueGenericSend(QueueHandle_t, const void*, TickType_t, BaseType_t) {
    return pdTRUE;
}

BaseType_t __attribute__((weak)) xQueueGenericReceive(QueueHandle_t, void*, TickType_t, BaseType_t) {
    return pdFALSE;
}

UBaseType_t __attribute__((weak)) uxQueueMessagesWaiting(const QueueHandle_t) {
    return 0;
}

unsigned long millis() {
    static unsigned long now = 0;
    return ++now;
}

void protocol_buffer_synchronize() {}

void delay_ms(uint32_t) {}

bool read_number(const std::string_view sv, float& value, bool) {
    std::string text(sv);
    char*       end = nullptr;
    value           = std::strtof(text.c_str(), &end);
    return end != text.c_str() && *end == '\0';
}

std::string IP_string(uint32_t ipaddr) {
    return std::to_string((ipaddr >> 24) & 0xff) + "." + std::to_string((ipaddr >> 16) & 0xff) + "."
           + std::to_string((ipaddr >> 8) & 0xff) + "." + std::to_string(ipaddr & 0xff);
}

bool atMsgLevel(MsgLevel) {
    return g_logFilterEnabled;
}

Channel::Channel(const std::string& name, bool addCR) : _name(name), _addCR(addCR) {}
Channel::Channel(const char* name, bool addCR) : _name(name), _addCR(addCR) {}
Channel::Channel(const char* name, objnum_t, bool addCR) : _name(name), _addCR(addCR) {}
Error Channel::pollLine(char*) {
    return Error::NoData;
}
void Channel::ack(Error) {}
void Channel::sendLine(MsgLevel, const char* line) {
    if (!line) {
        return;
    }
    write(reinterpret_cast<const uint8_t*>(line), std::strlen(line));
}
void Channel::sendLine(MsgLevel level, const std::string* line) {
    if (line == nullptr) {
        return;
    }
    sendLine(level, *line);
}
void Channel::sendLine(MsgLevel, const std::string& line) {
    write(reinterpret_cast<const uint8_t*>(line.c_str()), line.size());
}
void Channel::flushRx() {}
void Channel::handleRealtimeCharacter(uint8_t) {}
bool Channel::lineComplete(char*, char) {
    return false;
}
bool Channel::is_visible(const std::string&, std::string, bool) {
    return true;
}
void Channel::writeUTF8(uint32_t) {}
void Channel::print_msg(MsgLevel, const char*) {}
uint32_t Channel::setReportInterval(uint32_t) {
    return 0;
}
void Channel::autoReport() {}
void Channel::autoReportGCodeState() {}
void Channel::push(uint8_t) {}
void Channel::out(const char*, const char*) {}
void Channel::out(const std::string&, const char*) {}
void Channel::out_acked(const std::string&, const char*) {}
void Channel::ready() {}
void Channel::registerEvent(pinnum_t, InputPin*) {}
void Channel::pause() {}
void Channel::resume() {}

std::mutex AllChannels::_mutex_general;
std::mutex AllChannels::_mutex_pollLine;
void AllChannels::kill(Channel*) {}
void AllChannels::registration(Channel*) {}
void AllChannels::deregistration(Channel*) {}
void AllChannels::init() {}
void AllChannels::ready() {}
size_t AllChannels::write(uint8_t) {
    return 1;
}
size_t AllChannels::write(const uint8_t*, size_t length) {
    return length;
}
void AllChannels::print_msg(MsgLevel, const char*) {}
void AllChannels::flushRx() {}
void AllChannels::notifyOvr() {}
void AllChannels::notifyWco() {}
void AllChannels::notifyNgc(CoordIndex) {}
void AllChannels::listChannels(Channel&) {}
Channel* AllChannels::find(const std::string_view) {
    return nullptr;
}
Channel* AllChannels::poll(char*) {
    return nullptr;
}
AllChannels allChannels;

LogStream::LogStream(Channel& channel, MsgLevel level) : _channel(channel), _line(nullptr), _level(level) {}
LogStream::LogStream(Channel& channel, const char* name) : LogStream(channel, MsgLevelNone, name) {}
LogStream::LogStream(Channel& channel, MsgLevel level, const char* name) : LogStream(channel, level) {
    if (name) {
        print(name);
    }
}
LogStream::LogStream(MsgLevel level, const char* name) : _channel(g_nullChannel), _line(nullptr), _level(level) {
    if (name) {
        print(name);
    }
}
size_t LogStream::write(uint8_t c) {
    if (_line == nullptr) {
        _line = new std::string();
    }
    _line->push_back(static_cast<char>(c));
    return 1;
}
LogStream::~LogStream() {
    if (_line == nullptr) {
        return;
    }
    if (!_line->empty() && (*_line)[0] == '[') {
        _line->push_back(']');
    }
    if (!_line->empty()) {
        _channel.sendLine(_level, _line);
    }
    delete _line;
}

void set_state(State s) {
    g_state = s;
}

bool state_is(State s) {
    return g_state == s;
}

Pin::~Pin() {}
void Pin::report(const char*) {}

Word::Word(type_t type, permissions_t permissions, const char* description, const char* grblName, const char* fullName) :
    _description(description),
    _grblName(grblName),
    _fullName(fullName),
    _type(type),
    _permissions(permissions) {}

Command::Command(const char*   description,
                 type_t        type,
                 permissions_t permissions,
                 const char*   grblName,
                 const char*   fullName,
                 bool (*)(),
                 bool synchronous) :
    Word(type, permissions, description, grblName, fullName),
    _synchronous(synchronous) {
    List.insert(List.begin(), this);
}

Setting::Setting(const char* description, type_t type, permissions_t permissions, const char* grblName, const char* fullName) :
    Word(type, permissions, description, grblName, fullName),
    _keyName(fullName) {
    List.insert(List.begin(), this);
}

IntSetting::IntSetting(const char*   description,
                       type_t        type,
                       permissions_t permissions,
                       const char*   grblName,
                       const char*   name,
                       int32_t       defVal,
                       int32_t       minVal,
                       int32_t       maxVal,
                       bool          currentIsNvm) :
    Setting(description, type, permissions, grblName, name),
    _defaultValue(defVal),
    _currentValue(defVal),
    _storedValue(defVal),
    _minValue(minVal),
    _maxValue(maxVal),
    _currentIsNvm(currentIsNvm) {}

void IntSetting::load() {}
void IntSetting::setDefault() {
    _currentValue = _defaultValue;
}
void IntSetting::addWebui(JSONencoder*) {}
Error IntSetting::setStringValue(std::string_view) {
    return Error::ReadOnlySetting;
}
const char* IntSetting::getStringValue() {
    static std::string value;
    value = std::to_string(_currentValue);
    return value.c_str();
}
const char* IntSetting::getDefaultString() {
    static std::string value;
    value = std::to_string(_defaultValue);
    return value.c_str();
}

FileStream::FileStream(const char* filename, const char* mode, const Volume& fs) :
    Channel(filename), _fd(nullptr), _size(0), _saved_position(0), _mode(mode) {
    (void)fs;
    throw std::runtime_error("FileStream not available in host integration tests");
}
FileStream::FileStream(FluidPath fpath, const char* mode) : Channel("file"), _fd(nullptr), _size(0), _saved_position(0), _mode(mode) {}
std::string FileStream::path() { return {}; }
std::string FileStream::name() { return {}; }
int FileStream::available() { return 0; }
int FileStream::read() { return -1; }
int FileStream::peek() { return -1; }
void FileStream::flush() {}
int FileStream::read(char*, size_t) { return 0; }
size_t FileStream::write(uint8_t) { return 0; }
size_t FileStream::write(const uint8_t*, size_t length) { return length; }
size_t FileStream::size() { return _size; }
size_t FileStream::position() { return 0; }
void FileStream::set_position(size_t) {}
void FileStream::save() {}
void FileStream::restore() {}
FileStream::~FileStream() = default;

FluidPath::~FluidPath() = default;

void protocol_send_event(const Event*, void*) {}
void send_alarm(ExecAlarm) {}

void InputPin::init() {}
void InputPin::trigger(bool active) {
    update(active);
}

void EventPin::trigger(bool active) {
    InputPin::trigger(active);
}

namespace Machine {
    Axes::Axes() = default;
    Axes::~Axes() = default;
    axis_t Axes::axisNum(std::string_view) {
        return X_AXIS;
    }
    void Axes::group(Configuration::HandlerBase&) {}
    void Axes::afterParse() {}

    UserOutputs::UserOutputs() = default;
    UserOutputs::~UserOutputs() = default;
    void UserOutputs::group(Configuration::HandlerBase&) {}
    void UserOutputs::init() {}
    void UserOutputs::all_off() {}
    bool UserOutputs::setDigital(size_t, bool) { return true; }
    bool UserOutputs::setAnalogPercent(size_t, float) { return true; }

    InputPin UserInputs::digitalInput[MaxUserDigitalPin] = {
        InputPin("digital0_pin"), InputPin("digital1_pin"), InputPin("digital2_pin"), InputPin("digital3_pin"),
        InputPin("digital4_pin"), InputPin("digital5_pin"), InputPin("digital6_pin"), InputPin("digital7_pin"),
    };
    InputPin UserInputs::analogInput[MaxUserAnalogPin] = {
        InputPin("analog0_pin"), InputPin("analog1_pin"), InputPin("analog2_pin"), InputPin("analog3_pin"),
    };
    UserInputs::UserInputs() = default;
    UserInputs::~UserInputs() = default;
    void UserInputs::group(Configuration::HandlerBase&) {}
    void UserInputs::init() {}
}

SDCard::SDCard() = default;
SDCard::~SDCard() = default;
void SDCard::afterParse() {}
const char* SDCard::filename() { return ""; }
void SDCard::init() {}

Control::Control() = default;
void Control::init() {}
void Control::group(Configuration::HandlerBase&) {}
bool Control::stuck() { return false; }
bool Control::safety_door_ajar() { return false; }
bool Control::pins_block_unlock() { return false; }
std::string Control::report_status() { return {}; }

void Parking::group(Configuration::HandlerBase&) {}

void CoolantControl::init() {}
CoolantState CoolantControl::get_state() { return {}; }
void CoolantControl::stop() {}
void CoolantControl::off() {}
void CoolantControl::set_state(CoolantState) {}
void CoolantControl::group(Configuration::HandlerBase&) {}

namespace Kinematics {
    Kinematics::~Kinematics() = default;
    void Kinematics::group(Configuration::HandlerBase&) {}
    void Kinematics::afterParse() {}
    void Kinematics::init() {}
    void Kinematics::init_position() {}
    float Kinematics::min_motor_pos(axis_t) { return 0.0f; }
    float Kinematics::max_motor_pos(axis_t) { return 0.0f; }
}

Probe::ProbeEventPin::ProbeEventPin(const char* legend) : EventPin(nullptr, ExecAlarm::None, legend) {}
void Probe::init() {}
void Probe::set_direction(bool away) { _away = away; }
bool Probe::get_state() { return false; }
bool Probe::tripped() { return false; }
void Probe::validate() {}
void Probe::group(Configuration::HandlerBase&) {}

namespace Extenders {
    Extenders::Extenders() = default;
    void Extenders::group(Configuration::HandlerBase&) {}
    void Extenders::init() {}
    Extenders::~Extenders() = default;
}

Uart::Uart(uint32_t uart_num) : _uart_num(uart_num) {}
void Uart::begin() {}
void Uart::begin(uint32_t, UartData, UartStop, UartParity) {}
int Uart::peek() { return -1; }
int Uart::available() { return 0; }
int Uart::read() { return -1; }
size_t Uart::write(uint8_t) { return 1; }
size_t Uart::write(const uint8_t*, size_t length) { return length; }
void Uart::flushRx() {}
int Uart::rx_buffer_available() { return 0; }
size_t Uart::timedReadBytes(char*, size_t, TickType_t) { return 0; }
bool Uart::flushTxTimed(TickType_t) { return true; }
bool Uart::setHalfDuplex() { return true; }
void Uart::forceXon() {}
void Uart::forceXoff() {}
void Uart::setSwFlowControl(bool, uint32_t, uint32_t) {}
void Uart::getSwFlowControl(bool& enabled, uint32_t& rx_threshold, uint32_t& tx_threshold) {
    enabled = false;
    rx_threshold = 0;
    tx_threshold = 0;
}
void Uart::changeMode(uint32_t, UartData, UartParity, UartStop) {}
void Uart::restoreMode() {}
void Uart::enterPassthrough() {}
void Uart::exitPassthrough() {}
void Uart::registerInputPin(pinnum_t, InputPin*) {}
void Uart::config_message(const char*, const char*) {}

UartChannel::UartChannel(objnum_t num, bool addCR) : Channel("uart_channel", addCR), _lineedit(nullptr), _uart(nullptr), _uart_num(num) {}
void UartChannel::init() {}
void UartChannel::init(Uart* uart) { _uart = uart; }
size_t UartChannel::write(uint8_t) { return 1; }
size_t UartChannel::write(const uint8_t*, size_t len) { return len; }
int UartChannel::peek() { return -1; }
int UartChannel::available() { return 0; }
int UartChannel::read() { return -1; }
int UartChannel::rx_buffer_available() { return 0; }
void UartChannel::flushRx() {}
size_t UartChannel::timedReadBytes(char*, size_t, TickType_t) { return 0; }
bool UartChannel::realtimeOkay(char) { return true; }
bool UartChannel::lineComplete(char*, char) { return false; }
bool UartChannel::setAttr(pinnum_t, bool*, const std::string&) { return false; }
void UartChannel::out(const std::string&, const char*) {}
void UartChannel::out_acked(const std::string&, const char*) {}
void UartChannel::beginJSON(const char*) {}
void UartChannel::endJSON(const char*) {}
void UartChannel::getExpanderId() {}
void UartChannel::registerEvent(pinnum_t, InputPin*) {}

namespace Machine {
    void Stepping::group(Configuration::HandlerBase&) {}
    void Stepping::afterParse() {}
    uint32_t Stepping::_idleMsecs = 0;
    uint32_t Stepping::_disableDelayUsecs = 0;
    uint32_t Stepping::_engine = Stepping::TIMED;
}

namespace Spindles {
    uint32_t Spindle::maxSpeed() { return 0; }
    uint32_t Spindle::mapSpeed(SpindleState, SpindleSpeed) { return 0; }
    void Spindle::setupSpeeds(uint32_t) {}
    void Spindle::shelfSpeeds(SpindleSpeed, SpindleSpeed) {}
    void Spindle::linearSpeeds(SpindleSpeed, float) {}
    void Spindle::switchSpindle(uint32_t, SpindleList, Spindle*&, bool&, bool&) {}
    void Spindle::spindleDelay(SpindleState, SpindleSpeed) {}
    void Spindle::init_atc() {}
    bool Spindle::isRateAdjusted() { return false; }
    bool Spindle::tool_change(uint32_t, bool, bool) { return false; }
    void Spindle::afterParse() {}

    void Null::init() {}
    void Null::setSpeedfromISR(uint32_t) {}
    void Null::setState(SpindleState state, SpindleSpeed speed) {
        _current_state = state;
        _current_speed = speed;
    }
    void Null::config_message() {}
}

namespace Machine {
    Macro Macros::_startup_line0 { "startup_line0" };
    Macro Macros::_startup_line1 { "startup_line1" };
    Macro Macros::_macro[] = { Macro { "Macro0" }, Macro { "Macro1" }, Macro { "Macro2" }, Macro { "Macro3" } };
    Macro Macros::_after_homing { "after_homing" };
    Macro Macros::_after_reset { "after_reset" };
    Macro Macros::_after_unlock { "after_unlock" };
}

namespace Configuration {
    Tokenizer::Tokenizer(std::string_view yaml_string) : _remainder(yaml_string), _linenum(0), _token() {}

    void Tokenizer::Tokenize() {
        _token._state = TokenState::Eof;
        _token._indent = -1;
        _token._key = {};
        _token._value = {};
    }

    void Tokenizer::parseError(std::string_view) const {
        throw std::runtime_error("Tokenizer parse error");
    }

    Parser::Parser(std::string_view yaml_string) : Tokenizer(yaml_string) {}
    bool Parser::is(const char*) { return false; }
    std::string_view Parser::stringValue() const { return {}; }
    bool Parser::boolValue() const { return false; }
    int32_t Parser::intValue() const { return 0; }
    uint32_t Parser::uintValue() const { return 0; }
    std::vector<speedEntry> Parser::speedEntryValue() const { return {}; }
    std::vector<float> Parser::floatArray() const { return {}; }
    float Parser::floatValue() const { return 0.0f; }
    Pin Parser::pinValue() const { return Pin(); }
    uint32_t Parser::enumValue(const EnumItem*) const { return 0; }
    IPAddress Parser::ipValue() const { return IPAddress(); }
    void Parser::uartMode(UartData&, UartParity&, UartStop&) const {}

    void AfterParse::enterSection(const char*, Configurable*) {}

    Validator::Validator() = default;
    void Validator::enterSection(const char*, Configurable*) {}
}

namespace TestStubs {
    void reset_state(State s) {
        set_state(s);
    }

    void set_log_filter_enabled(bool enabled) {
        g_logFilterEnabled = enabled;
    }
}
