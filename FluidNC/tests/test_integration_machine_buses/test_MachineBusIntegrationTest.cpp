#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define private public
#define protected public
#include "Configuration/AfterParse.h"
#include "Configuration/HandlerBase.h"
#include "Configuration/Parser.h"
#include "Configuration/Tokenizer.h"
#include "Configuration/Validator.h"
#include "Control.h"
#include "Driver/i2s_out.h"
#include "FileStream.h"
#include "FluidPath.h"
#include "Kinematics/Kinematics.h"
#include "Machine/I2CBus.h"
#include "Machine/I2SOBus.h"
#include "Machine/MachineConfig.h"
#include "Machine/SPIBus.h"
#include "Parking.h"
#include "Pins/PinAttributes.h"
#include "Pins/PinCapabilities.h"
#include "Pins/PinDetail.h"
#include "SDCard.h"
#include "SettingsDefinitions.h"
#include "Spindles/NullSpindle.h"
#include "Spindles/Spindle.h"
#include "Stepping.h"
#include "TestStubs.h"
#undef protected
#undef private

Spindles::Spindle* spindle = nullptr;

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

std::vector<Setting*> Setting::List = {};
std::vector<Command*> Command::List = {};

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

namespace {
struct FakePinDetail final : public Pins::PinDetail {
    FakePinDetail(pinnum_t index, const char* pinName, Pins::PinCapabilities caps, int8_t driveStrength = -1) :
        PinDetail(index), caps(caps), driveStrengthValue(driveStrength) {
        _name = pinName;
    }

    Pins::PinCapabilities capabilities() const override {
        return caps;
    }

    void write(bool high) override {
        lastWrite = high;
        ++writeCalls;
    }

    bool read() override {
        return value;
    }

    void setAttr(Pins::PinAttributes attrs, uint32_t frequency = 0) override {
        lastAttrs = attrs;
        lastFrequency = frequency;
    }

    Pins::PinAttributes getAttr() const override {
        return lastAttrs;
    }

    int8_t driveStrength() override {
        return driveStrengthValue;
    }

    Pins::PinCapabilities caps;
    int8_t                driveStrengthValue = -1;
    bool                  value = false;
    bool                  lastWrite = false;
    int                   writeCalls = 0;
    uint32_t              lastFrequency = 0;
    Pins::PinAttributes   lastAttrs = Pins::PinAttributes::None;
};

void attachPin(Pin& pin, FakePinDetail& detail) {
    *reinterpret_cast<Pins::PinDetail**>(&pin) = &detail;
}

class RecordingHandler : public Configuration::HandlerBase {
protected:
    void enterSection(const char* name, Configuration::Configurable*) override {
        sections.emplace_back(name);
    }

    bool matchesUninitialized(const char*) override {
        return false;
    }

public:
    std::vector<std::string> sections;

    void item(const char*, Macro&) override {}
    void item(const char*, bool&) override {}
    void item(const char*, int32_t&, int32_t, int32_t) override {}
    void item(const char*, uint32_t&, uint32_t, uint32_t) override {}
    void item(const char*, float&, float, float) override {}
    void item(const char*, std::vector<Configuration::speedEntry>&) override {}
    void item(const char*, std::vector<float>&) override {}
    void item(const char*, UartData&, UartParity&, UartStop&) override {}
    void item(const char*, EventPin&) override {}
    void item(const char*, InputPin&) override {}
    void item(const char*, Pin&) override {}
    void item(const char*, IPAddress&) override {}
    void item(const char*, uint32_t&, const EnumItem*) override {}
    void item(const char*, axis_t&) override {}
    void item(const char*, std::string&, int, int) override {}

    Configuration::HandlerType handlerType() override {
        return Configuration::HandlerType::Generator;
    }
};

struct I2CState {
    bool                 initError = false;
    int                  initCalls = 0;
    objnum_t             initBus = 0;
    pinnum_t             initSda = INVALID_PINNUM;
    pinnum_t             initScl = INVALID_PINNUM;
    uint32_t             initFrequency = 0;
    int                  writeResult = 0;
    int                  readResult = 0;
    objnum_t             lastWriteBus = 0;
    uint8_t              lastWriteAddress = 0;
    std::vector<uint8_t> lastWriteData;
    objnum_t             lastReadBus = 0;
    uint8_t              lastReadAddress = 0;
    std::vector<uint8_t> readData;
} g_i2c;

struct SPIState {
    bool    initResult = true;
    int     initCalls = 0;
    int     deinitCalls = 0;
    pinnum_t sck = INVALID_PINNUM;
    pinnum_t miso = INVALID_PINNUM;
    pinnum_t mosi = INVALID_PINNUM;
    bool     dma = false;
    int8_t   sckDrive = -1;
    int8_t   mosiDrive = -1;
} g_spi;

struct I2SOState {
    bool           called = false;
    i2s_out_init_t params {};
} g_i2so;

void resetBusState() {
    g_i2c = {};
    g_spi = {};
    g_i2so = {};
}

class FakeSpindle final : public Spindles::Spindle {
public:
    FakeSpindle(const char* spindleName, int32_t tool) : Spindle(spindleName) {
        _tool = tool;
    }

    void init() override {}
    void config_message() override {}
    void setSpeedfromISR(uint32_t) override {}

    void setState(SpindleState state, uint32_t speed) override {
        _current_state = state;
        _current_speed = speed;
    }
};

template <typename T>
struct RawSlot {
    alignas(T) unsigned char storage[sizeof(T)];

    template <typename... Args>
    T* construct(Args&&... args) {
        return new (storage) T(std::forward<Args>(args)...);
    }

    T* get() {
        return reinterpret_cast<T*>(storage);
    }

    void destroy() {
        get()->~T();
    }
};
}  // namespace

bool i2c_master_init(objnum_t bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency) {
    ++g_i2c.initCalls;
    g_i2c.initBus = bus_number;
    g_i2c.initSda = sda_pin;
    g_i2c.initScl = scl_pin;
    g_i2c.initFrequency = frequency;
    return g_i2c.initError;
}

int i2c_write(objnum_t bus_number, uint8_t address, const uint8_t* data, size_t count) {
    g_i2c.lastWriteBus = bus_number;
    g_i2c.lastWriteAddress = address;
    g_i2c.lastWriteData.assign(data, data + count);
    return g_i2c.writeResult;
}

int i2c_read(objnum_t bus_number, uint8_t address, uint8_t* data, size_t count) {
    g_i2c.lastReadBus = bus_number;
    g_i2c.lastReadAddress = address;
    for (size_t i = 0; i < count; ++i) {
        data[i] = i < g_i2c.readData.size() ? g_i2c.readData[i] : 0;
    }
    return g_i2c.readResult;
}

bool spi_init_bus(pinnum_t sck_pin, pinnum_t miso_pin, pinnum_t mosi_pin, bool dma, int8_t sck_drive_strength, int8_t mosi_drive_strength) {
    ++g_spi.initCalls;
    g_spi.sck = sck_pin;
    g_spi.miso = miso_pin;
    g_spi.mosi = mosi_pin;
    g_spi.dma = dma;
    g_spi.sckDrive = sck_drive_strength;
    g_spi.mosiDrive = mosi_drive_strength;
    return g_spi.initResult;
}

void spi_deinit_bus() {
    ++g_spi.deinitCalls;
}

extern "C" void i2s_out_init(i2s_out_init_t* init_param) {
    g_i2so.called = true;
    g_i2so.params = *init_param;
}

FileStream::FileStream(const char* filename, const char* mode, const Volume& fs) : Channel(filename), _fd(nullptr), _size(0), _saved_position(0), _mode(mode) {
    (void)fs;
    throw std::runtime_error("FileStream not available in bus integration tests");
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

namespace string_util {
const std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}
}  // namespace string_util

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
    InputPin("digital0_pin"),
    InputPin("digital1_pin"),
    InputPin("digital2_pin"),
    InputPin("digital3_pin"),
    InputPin("digital4_pin"),
    InputPin("digital5_pin"),
    InputPin("digital6_pin"),
    InputPin("digital7_pin"),
};
InputPin UserInputs::analogInput[MaxUserAnalogPin] = {
    InputPin("analog0_pin"),
    InputPin("analog1_pin"),
    InputPin("analog2_pin"),
    InputPin("analog3_pin"),
};
UserInputs::UserInputs() = default;
UserInputs::~UserInputs() = default;
void UserInputs::group(Configuration::HandlerBase&) {}
void UserInputs::init() {}
}  // namespace Machine

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
}  // namespace Kinematics

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
}  // namespace Extenders

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
}  // namespace Machine

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
}  // namespace Spindles

namespace Machine {
Macro Macros::_startup_line0 { "startup_line0" };
Macro Macros::_startup_line1 { "startup_line1" };
Macro Macros::_macro[] = { Macro { "Macro0" }, Macro { "Macro1" }, Macro { "Macro2" }, Macro { "Macro3" } };
Macro Macros::_after_homing { "after_homing" };
Macro Macros::_after_reset { "after_reset" };
Macro Macros::_after_unlock { "after_unlock" };
}  // namespace Machine

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
void Parser::uartMode(UartData& wordLength, UartParity& parity, UartStop& stopBits) const {}

void AfterParse::enterSection(const char*, Configurable*) {}

Validator::Validator() = default;
void Validator::enterSection(const char*, Configurable*) {}
}  // namespace Configuration

TEST(MachineBusIntegration, I2CBusInitAndTransfersUseConfiguredPins) {
    resetBusState();

    Machine::I2CBus bus(1);
    FakePinDetail   sda(21, "gpio.21", Pins::PinCapabilities::Native | Pins::PinCapabilities::Input | Pins::PinCapabilities::Output);
    FakePinDetail   scl(22, "gpio.22", Pins::PinCapabilities::Native | Pins::PinCapabilities::Input | Pins::PinCapabilities::Output);
    attachPin(bus._sda, sda);
    attachPin(bus._scl, scl);
    bus._frequency = 400000;

    g_i2c.writeResult = 2;
    g_i2c.readResult = 1;
    g_i2c.readData = { 0xA5 };

    bus.init();

    EXPECT_EQ(g_i2c.initCalls, 1);
    EXPECT_EQ(g_i2c.initBus, 1);
    EXPECT_EQ(g_i2c.initSda, 21);
    EXPECT_EQ(g_i2c.initScl, 22);
    EXPECT_EQ(g_i2c.initFrequency, 400000u);
    EXPECT_FALSE(bus._error);

    uint8_t tx[] = { 0x12, 0x34 };
    EXPECT_EQ(bus.write(0x27, tx, 2), 2);
    EXPECT_EQ(g_i2c.lastWriteBus, 1);
    EXPECT_EQ(g_i2c.lastWriteAddress, 0x27);
    ASSERT_EQ(g_i2c.lastWriteData.size(), 2u);
    EXPECT_EQ(g_i2c.lastWriteData[0], 0x12);
    EXPECT_EQ(g_i2c.lastWriteData[1], 0x34);

    uint8_t rx = 0;
    EXPECT_EQ(bus.read(0x27, &rx, 1), 1);
    EXPECT_EQ(g_i2c.lastReadBus, 1);
    EXPECT_EQ(g_i2c.lastReadAddress, 0x27);
    EXPECT_EQ(rx, 0xA5);

    bus._error = true;
    EXPECT_EQ(bus.write(0x27, tx, 2), -1);
    EXPECT_EQ(bus.read(0x27, &rx, 1), -1);
}

TEST(MachineBusIntegration, I2CBusValidateRequiresPinsAsAPair) {
    Machine::I2CBus bus(0);
    FakePinDetail   sda(4, "gpio.4", Pins::PinCapabilities::Native | Pins::PinCapabilities::Input | Pins::PinCapabilities::Output);

    attachPin(bus._sda, sda);

    EXPECT_THROW(bus.validate(), std::runtime_error);
}

TEST(MachineBusIntegration, SPIBusValidateRequiresAllPinsWhenAnyAreConfigured) {
    Machine::SPIBus bus;
    FakePinDetail   miso(19, "gpio.19", Pins::PinCapabilities::Native | Pins::PinCapabilities::Input);

    attachPin(bus._miso, miso);

    EXPECT_THROW(bus.validate(), std::runtime_error);
}

TEST(MachineBusIntegration, SPIBusInitUsesExplicitPinsAndTracksDefinedState) {
    resetBusState();

    Machine::SPIBus bus;
    FakePinDetail   miso(19, "gpio.19", Pins::PinCapabilities::Native | Pins::PinCapabilities::Input, 2);
    FakePinDetail   mosi(23, "gpio.23", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output, 3);
    FakePinDetail   sck(18, "gpio.18", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output, 4);
    attachPin(bus._miso, miso);
    attachPin(bus._mosi, mosi);
    attachPin(bus._sck, sck);

    bus.init();

    EXPECT_TRUE(bus.defined());
    EXPECT_EQ(g_spi.initCalls, 1);
    EXPECT_EQ(g_spi.sck, 18);
    EXPECT_EQ(g_spi.miso, 19);
    EXPECT_EQ(g_spi.mosi, 23);
    EXPECT_TRUE(g_spi.dma);
    EXPECT_EQ(g_spi.sckDrive, 4);
    EXPECT_EQ(g_spi.mosiDrive, 3);

    bus.deinit();
    EXPECT_EQ(g_spi.deinitCalls, 1);
}

TEST(MachineBusIntegration, SPIBusInitUsesDefaultPinsWhenFallbackCsIsConfigured) {
    resetBusState();

    IntSetting fallback("fallback", EXTENDED, WG, nullptr, "SD/FallbackCS", 5, -1, 40);
    sd_fallback_cs = &fallback;

    Machine::SPIBus bus;
    g_spi.initResult = false;

    bus.init();

    EXPECT_EQ(g_spi.initCalls, 1);
    EXPECT_EQ(g_spi.sck, 18);
    EXPECT_EQ(g_spi.miso, 19);
    EXPECT_EQ(g_spi.mosi, 23);
    EXPECT_FALSE(bus._defined);

    sd_fallback_cs = nullptr;
}

TEST(MachineBusIntegration, I2SOBusValidateRejectsInvalidPulseWidthAndPartialPins) {
    Machine::I2SOBus bus;
    FakePinDetail    bck(26, "gpio.26", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output);

    bus._min_pulse_us = 3;
    EXPECT_THROW(bus.validate(), std::runtime_error);

    bus._min_pulse_us = 1;
    attachPin(bus._bck, bck);
    EXPECT_THROW(bus.validate(), std::runtime_error);
}

TEST(MachineBusIntegration, I2SOBusInitPublishesBusAndEnablesOutput) {
    resetBusState();

    Machine::I2SOBus bus;
    FakePinDetail    bck(26, "gpio.26", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output, 1);
    FakePinDetail    data(27, "gpio.27", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output, 2);
    FakePinDetail    ws(25, "gpio.25", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output, 3);
    FakePinDetail    oe(33, "gpio.33", Pins::PinCapabilities::Output, -1);
    attachPin(bus._bck, bck);
    attachPin(bus._data, data);
    attachPin(bus._ws, ws);
    attachPin(bus._oe, oe);
    bus._min_pulse_us = 4;

    bus.init();

    EXPECT_TRUE(g_i2so.called);
    EXPECT_EQ(g_i2so.params.bck_pin, 26);
    EXPECT_EQ(g_i2so.params.data_pin, 27);
    EXPECT_EQ(g_i2so.params.ws_pin, 25);
    EXPECT_EQ(g_i2so.params.min_pulse_us, 4u);
    EXPECT_EQ(g_i2so.params.bck_drive_strength, 1);
    EXPECT_EQ(g_i2so.params.data_drive_strength, 2);
    EXPECT_EQ(g_i2so.params.ws_drive_strength, 3);
    EXPECT_TRUE(oe.lastAttrs.has(Pins::PinAttributes::Output));
    EXPECT_FALSE(oe.lastWrite);
}

TEST(MachineBusIntegration, I2SOBusInitSkipsPinsWithoutNativeOutputCapabilities) {
    resetBusState();

    Machine::I2SOBus bus;
    FakePinDetail    bck(26, "gpio.26", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output, 1);
    FakePinDetail    data(27, "gpio.27", Pins::PinCapabilities::Native | Pins::PinCapabilities::Output, 2);
    FakePinDetail    ws(25, "gpio.25", Pins::PinCapabilities::Input, 3);
    attachPin(bus._bck, bck);
    attachPin(bus._data, data);
    attachPin(bus._ws, ws);

    bus.init();

    EXPECT_FALSE(g_i2so.called);
}

TEST(MachineBusIntegration, MachineConfigAfterParseCreatesDefaultsAndNormalizesSpindles) {
    GTEST_SKIP() << "Covered by dedicated machine integration suites; bus suite stays focused on host-safe bus behavior.";
    auto* machine = new Machine::MachineConfig();

    RawSlot<Machine::Axes>          axes;
    RawSlot<Kinematics::Kinematics> kinematics;
    RawSlot<Probe>                  probe;
    RawSlot<Machine::UserOutputs>   outputs;
    RawSlot<Machine::UserInputs>    inputs;
    RawSlot<SDCard>                 sdcard;
    RawSlot<Control>                control;

    machine->_axes = axes.construct();
    machine->_kinematics = kinematics.construct();
    machine->_probe = probe.construct();
    machine->_userOutputs = outputs.construct();
    machine->_userInputs = inputs.construct();
    machine->_sdCard = sdcard.construct();
    machine->_control = control.construct();
    machine->_spi = nullptr;
    machine->_coolant = nullptr;
    machine->_stepping = nullptr;
    machine->_start = nullptr;
    machine->_parking = nullptr;
    machine->_macros = nullptr;

    auto& spindles = Spindles::SpindleFactory::objects();
    for (auto* s : spindles) {
        delete s;
    }
    spindles.clear();
    spindles.push_back(new FakeSpindle("late", 5));
    spindles.push_back(new FakeSpindle("early", 5));
    spindles.push_back(new FakeSpindle("duplicate", 5));

    config = machine;
    spindle = nullptr;

    machine->afterParse();

    ASSERT_NE(machine->_spi, nullptr);
    ASSERT_NE(machine->_coolant, nullptr);
    ASSERT_NE(machine->_stepping, nullptr);
    ASSERT_NE(machine->_start, nullptr);
    ASSERT_NE(machine->_parking, nullptr);
    ASSERT_NE(machine->_macros, nullptr);

    ASSERT_EQ(spindles.size(), 3u);
    EXPECT_EQ(spindles[0]->_tool, 0);
    EXPECT_EQ(spindles[1]->_tool, 5);
    EXPECT_EQ(spindles[2]->_tool, 105);
    EXPECT_EQ(spindle, spindles[0]);

    machine->_axes = nullptr;
    machine->_kinematics = nullptr;
    machine->_probe = nullptr;
    machine->_userOutputs = nullptr;
    machine->_userInputs = nullptr;
    machine->_sdCard = nullptr;
    machine->_control = nullptr;
    machine->_spi = nullptr;
    machine->_coolant = nullptr;
    machine->_stepping = nullptr;
    machine->_start = nullptr;
    machine->_parking = nullptr;
    machine->_macros = nullptr;
    config = nullptr;
    spindle = nullptr;
    delete machine;
    axes.destroy();
    kinematics.destroy();
    probe.destroy();
    outputs.destroy();
    inputs.destroy();
    sdcard.destroy();
    control.destroy();

    for (auto* s : spindles) {
        delete s;
    }
    spindles.clear();
}

TEST(MachineBusIntegration, MachineConfigAfterParseCreatesNullSpindleWhenNoSpindlesExist) {
    GTEST_SKIP() << "Covered by dedicated machine integration suites; bus suite stays focused on host-safe bus behavior.";
    auto* machine = new Machine::MachineConfig();

    RawSlot<Machine::Axes>          axes;
    RawSlot<Kinematics::Kinematics> kinematics;
    RawSlot<Probe>                  probe;
    RawSlot<Machine::UserOutputs>   outputs;
    RawSlot<Machine::UserInputs>    inputs;
    RawSlot<SDCard>                 sdcard;
    RawSlot<Control>                control;

    machine->_axes = axes.construct();
    machine->_kinematics = kinematics.construct();
    machine->_probe = probe.construct();
    machine->_userOutputs = outputs.construct();
    machine->_userInputs = inputs.construct();
    machine->_sdCard = sdcard.construct();
    machine->_control = control.construct();

    auto& spindles = Spindles::SpindleFactory::objects();
    for (auto* s : spindles) {
        delete s;
    }
    spindles.clear();

    config = machine;
    spindle = nullptr;

    machine->afterParse();

    ASSERT_EQ(spindles.size(), 1u);
    ASSERT_NE(dynamic_cast<Spindles::Null*>(spindles[0]), nullptr);
    EXPECT_EQ(spindles[0]->_tool, 0);
    EXPECT_EQ(spindle, spindles[0]);

    machine->_axes = nullptr;
    machine->_kinematics = nullptr;
    machine->_probe = nullptr;
    machine->_userOutputs = nullptr;
    machine->_userInputs = nullptr;
    machine->_sdCard = nullptr;
    machine->_control = nullptr;
    machine->_spi = nullptr;
    machine->_coolant = nullptr;
    machine->_stepping = nullptr;
    machine->_start = nullptr;
    machine->_parking = nullptr;
    machine->_macros = nullptr;
    config = nullptr;
    spindle = nullptr;
    delete machine;
    axes.destroy();
    kinematics.destroy();
    probe.destroy();
    outputs.destroy();
    inputs.destroy();
    sdcard.destroy();
    control.destroy();

    for (auto* s : spindles) {
        delete s;
    }
    spindles.clear();
}

TEST(MachineBusIntegration, MachineConfigGroupEmitsConfiguredBusSections) {
    Machine::MachineConfig machine;
    Machine::I2CBus        i2c0(0);
    Machine::I2CBus        i2c1(1);
    Machine::SPIBus        spi;
    Machine::I2SOBus       i2so;

    machine._i2c[0] = &i2c0;
    machine._i2c[1] = &i2c1;
    machine._spi = &spi;
    machine._i2so = &i2so;

    RecordingHandler handler;
    machine.group(handler);

    ASSERT_EQ(handler.sections.size(), 4u);
    EXPECT_EQ(handler.sections[0], "i2so");
    EXPECT_EQ(handler.sections[1], "i2c0");
    EXPECT_EQ(handler.sections[2], "i2c1");
    EXPECT_EQ(handler.sections[3], "spi");

    machine._spi = nullptr;
    machine._i2so = nullptr;
}
