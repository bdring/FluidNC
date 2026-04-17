#include "TestStubs.h"

#include <cstring>
#include <cstdint>
#include <mutex>
#include <string_view>

#include "Channel.h"
#include "GCode.h"
#include "Machine/MachineConfig.h"
#include "Serial.h"

namespace {
State g_state = State::Idle;
bool  g_logFilterEnabled = true;
}

gc_modal_t __attribute__((weak)) modal_defaults {};

// Provide weak queue fallbacks so host tests that instantiate AllChannels
// do not need to define queue symbols unless they care about queue behavior.
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

namespace {
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
}  // namespace

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

namespace Machine {
void __attribute__((weak)) MachineConfig::afterParse() {}
void __attribute__((weak)) MachineConfig::group(Configuration::HandlerBase&) {}
void __attribute__((weak)) MachineConfig::load() {}
void __attribute__((weak)) MachineConfig::load_file(std::string_view) {}
void __attribute__((weak)) MachineConfig::load_yaml(std::string_view) {}
__attribute__((weak)) MachineConfig::~MachineConfig() {}
}  // namespace Machine

namespace TestStubs {
void reset_state(State s) {
    set_state(s);
}

void set_log_filter_enabled(bool enabled) {
    g_logFilterEnabled = enabled;
}
}  // namespace TestStubs
