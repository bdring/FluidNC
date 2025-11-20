#include "Settings.h"

#include "System.h"    // sys
#include "Protocol.h"  // protocol_buffer_synchronize
#include "Machine/MachineConfig.h"
#include "Parameters.h"

#include <map>
#include <limits>
#include <cstring>
#include <vector>
#include <charconv>
#include <Driver/NVS.h>

NVS nvs("FluidNC");

std::vector<Setting*> Setting::List __attribute__((init_priority(101))) = {};
std::vector<Command*> Command::List __attribute__((init_priority(102))) = {};

bool get_param(const char* parameter, const char* key, std::string& s) {
    const char* start = strstr(parameter, key);
    if (!start) {
        return false;
    }
    s = "";
    for (const char* p = start + strlen(key); *p; ++p) {
        if (*p == ' ') {
            break;  // Unescaped space
        }
        if (*p == '\\') {
            if (*++p == '\0') {
                break;
            }
        }
        s += *p;
    }
    return true;
}

bool paramIsJSON(const char* cmd_params) {
    return strstr(cmd_params, "json=yes") != NULL;
}

bool anyState() {
    return false;
}
bool notIdleOrJog() {
    return !state_is(State::Idle) && !state_is(State::Jog);
}
bool notIdleOrAlarm() {
    return !state_is(State::Idle) && !state_is(State::Alarm) && !state_is(State::ConfigAlarm) && !state_is(State::SafetyDoor) &&
           !state_is(State::Critical);
}
bool cycleOrHold() {
    return state_is(State::Cycle) || state_is(State::Hold);
}

bool allowConfigStates() {
    return !state_is(State::Idle) && !state_is(State::Alarm) && !state_is(State::ConfigAlarm) && !state_is(State::Critical);
}

Word::Word(type_t type, permissions_t permissions, const char* description, const char* grblName, const char* fullName) :
    _description(description), _grblName(grblName), _fullName(fullName), _type(type), _permissions(permissions) {}

Command::Command(const char*   description,
                 type_t        type,
                 permissions_t permissions,
                 const char*   grblName,
                 const char*   fullName,
                 bool (*cmdChecker)(),
                 bool synchronous) :
    Word(type, permissions, description, grblName, fullName),
    _cmdChecker(cmdChecker), _synchronous(synchronous) {
    List.insert(List.begin(), this);
}

Setting::Setting(const char* description, type_t type, permissions_t permissions, const char* grblName, const char* fullName) :
    Word(type, permissions, description, grblName, fullName) {
    List.insert(List.begin(), this);

    // NVS keys are limited to 15 characters, so if the setting name is longer
    // than that, we derive a 15-character name from a hash function
    size_t len = strlen(fullName);
    if (len <= 15) {
        _keyName = _fullName;
    } else {
        // This is Donald Knuth's hash function from Vol 3, chapter 6.4
        uint32_t hash = len;
        for (const char* s = fullName; *s; s++) {
            hash = ((hash << 5) ^ (hash >> 27)) ^ (*s);
        }

        char* hashName = (char*)malloc(16);  // Intentionally not freed
        sprintf(hashName, "%.7s%08x", fullName, static_cast<unsigned int>(hash));
        _keyName = hashName;
    }
}

Error Setting::check_state() {
    if (notIdleOrAlarm()) {
        return Error::IdleError;
    }
    return Error::Ok;
}

void Setting::init() {}

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
    _defaultValue(defVal), _currentValue(defVal), _minValue(minVal), _maxValue(maxVal), _currentIsNvm(currentIsNvm) {
    _storedValue = std::numeric_limits<int32_t>::min();
    load();
}

void IntSetting::load() {
    if (nvs.get_i32(_keyName, &_storedValue)) {
        _storedValue  = std::numeric_limits<int32_t>::min();
        _currentValue = _defaultValue;
    } else {
        _currentValue = _storedValue;
    }
}

void IntSetting::setDefault() {
    if (_currentIsNvm) {
        nvs.erase_key(_keyName);
    } else {
        _currentValue = _defaultValue;
        if (_storedValue != _currentValue) {
            nvs.erase_key(_keyName);
        }
    }
}

Error IntSetting::setStringValue(std::string_view s) {
    Error err = check_state();
    if (err != Error::Ok) {
        return err;
    }
    trim(s);
    float fnum;
    if (!read_number(s, fnum)) {
        return Error::BadNumberFormat;
    }

    int32_t convertedValue = fnum;
    if (convertedValue < _minValue || convertedValue > _maxValue) {
        return Error::NumberRange;
    }

    // If we don't see the NVM state, we have to make this the live value:
    if (!_currentIsNvm) {
        _currentValue = convertedValue;
    }

    if (_storedValue != convertedValue) {
        if (convertedValue == _defaultValue) {
            nvs.erase_key(_keyName);
        } else {
            if (nvs.set_i32(_keyName, convertedValue)) {
                return Error::NvsSetFailed;
            }
            _storedValue = convertedValue;
        }
    }
    return Error::Ok;
}

const char* IntSetting::getDefaultString() {
    static char strval[32];
    sprintf(strval, "%d", int(_defaultValue));
    return strval;
}

const char* IntSetting::getStringValue() {
    static char strval[32];

    int32_t currentSettingValue;
    if (_currentIsNvm) {
        if (std::numeric_limits<int32_t>::min() == _storedValue) {
            currentSettingValue = _defaultValue;
        } else {
            currentSettingValue = _storedValue;
        }
    } else {
        currentSettingValue = get();
    }

    sprintf(strval, "%d", int(currentSettingValue));
    return strval;
}

void IntSetting::addWebui(JSONencoder* j) {
    if (getDescription()) {
        j->begin_webui(getName(), "I", getStringValue(), _minValue, _maxValue);
        j->end_object();
    }
}

StringSetting::StringSetting(const char*   description,
                             type_t        type,
                             permissions_t permissions,
                             const char*   grblName,
                             const char*   name,
                             const char*   defVal,
                             int32_t       min,
                             int32_t       max) :
    Setting(description, type, permissions, grblName, name),
    _defaultValue(defVal), _currentValue(defVal), _minLength(min), _maxLength(max) {
    load();
};

void StringSetting::load() {
    size_t len = 0;
    if (nvs.get_str(_keyName, NULL, &len)) {
        _storedValue  = _defaultValue;
        _currentValue = _defaultValue;
        return;
    }

    std::vector<char> buffer(len);

    char* buf = buffer.data();
    if (nvs.get_str(_keyName, buf, &len)) {
        _storedValue  = _defaultValue;
        _currentValue = _defaultValue;
        return;
    }
    _storedValue  = buf;
    _currentValue = _storedValue;
}

void StringSetting::setDefault() {
    _currentValue = _defaultValue;
    if (_storedValue != _currentValue) {
        nvs.erase_key(_keyName);
    }
}

Error StringSetting::setStringValue(std::string_view s) {
    Error err = check_state();
    if (err != Error::Ok) {
        return err;
    }
    if (_minLength && _maxLength && (s.length() < size_t(_minLength) || s.length() > size_t(_maxLength))) {
        log_error("Setting length error");
        return Error::BadNumberFormat;
    }
    _currentValue = s;
    if (_storedValue != _currentValue) {
        if (_currentValue == _defaultValue) {
            nvs.erase_key(_keyName);
            _storedValue = _defaultValue;
        } else {
            if (nvs.set_str(_keyName, _currentValue.c_str())) {
                return Error::NvsSetFailed;
            }
            _storedValue = _currentValue;
        }
    }
    return Error::Ok;
}

const char* StringSetting::getDefaultString() {
    return _defaultValue.c_str();
}
const char* StringSetting::getStringValue() {
    return get();
}

void StringSetting::addWebui(JSONencoder* j) {
    if (!getDescription()) {
        return;
    }
    j->begin_webui(getName(), "S", getStringValue(), _minLength, _maxLength);
    j->end_object();
}

// typedef std::map<const char*, int8_t, cmp_str> enum_opt_t;
// typedef std::map<const char*, int8_t, std::less<>> enum_opt_t;

EnumSetting::EnumSetting(const char*       description,
                         type_t            type,
                         permissions_t     permissions,
                         const char*       grblName,
                         const char*       name,
                         int8_t            defVal,
                         const enum_opt_t* opts) :
    Setting(description, type, permissions, grblName, name),
    _defaultValue(defVal), _options(opts) {
    load();
}

void EnumSetting::load() {
    if (nvs.get_i8(_keyName, &_storedValue)) {
        _storedValue  = -1;
        _currentValue = _defaultValue;
    } else {
        _currentValue = _storedValue;
    }
}

void EnumSetting::setDefault() {
    _currentValue = _defaultValue;
    if (_storedValue != _currentValue) {
        nvs.erase_key(_keyName);
    }
}

// For enumerations, we allow the value to be set
// either with the string name or the numeric value.
// This is necessary for WebUI, which uses the number
// for setting.
Error EnumSetting::setStringValue(std::string_view s) {
    Error err = check_state();
    if (err != Error::Ok) {
        return err;
    }
    trim(s);
    std::string                str(s);
    enum_opt_t::const_iterator it = _options->find(str.c_str());
    if (it == _options->end()) {
        // If we don't find the value in keys, look for it in the numeric values

        // Disallow empty string
        if (!s.length()) {
            showList();
            return Error::BadNumberFormat;
        }
        float fnum;
        if (!read_number(s, fnum)) {
            showList();
            return Error::BadNumberFormat;
        }

        int32_t num = fnum;
        for (it = _options->begin(); it != _options->end(); it++) {
            if (it->second == num) {
                break;
            }
        }
        if (it == _options->end()) {
            return Error::BadNumberFormat;
        }
    }
    _currentValue = it->second;
    if (_storedValue != _currentValue) {
        if (_currentValue == _defaultValue) {
            nvs.erase_key(_keyName);
        } else {
            if (nvs.set_i8(_keyName, _currentValue)) {
                return Error::NvsSetFailed;
            }
            _storedValue = _currentValue;
        }
    }
    return Error::Ok;
}

const char* EnumSetting::enumToString(int8_t value) {
    for (enum_opt_t::const_iterator it = _options->begin(); it != _options->end(); it++) {
        if (it->second == value) {
            return it->first;
        }
    }
    showList();
    return "???";
}
const char* EnumSetting::getDefaultString() {
    return enumToString(_defaultValue);
}
const char* EnumSetting::getStringValue() {
    return enumToString(get());
}

void EnumSetting::showList() {
    std::string optList = "";
    for (enum_opt_t::const_iterator it = _options->begin(); it != _options->end(); it++) {
        optList = optList + " " + it->first;
    }
    log_info("Valid options:" << optList);
}

void EnumSetting::addWebui(JSONencoder* j) {
    if (!getDescription()) {
        return;
    }
    j->begin_webui(getName(), "B", get());
    j->begin_array("O");
    for (enum_opt_t::const_iterator it = _options->begin(); it != _options->end(); it++) {
        j->begin_object();
        j->member(it->first, it->second);
        j->end_object();
    }
    j->end_array();
    j->end_object();
}

Error UserCommand::action(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (_cmdChecker && _cmdChecker()) {
        return Error::IdleError;
    }
    return _action((const char*)value, auth_level, out);
};
Coordinates* coords[CoordIndex::End];

bool Coordinates::load() {
    size_t len = U_AXIS * sizeof(float);  // 6 is old MAX_N_AXIS
    if (nvs.get_blob(_name, _currentValue, &len)) {
        return false;
    }
    // If this is a UVW build, try to get additional coordinate data
    // The UVW data is stored separately to work around a bug in old
    // builds that could overrun the memory buffer if the stored blob
    // is too large.
    if (MAX_N_AXIS > U_AXIS) {
        len = (MAX_N_AXIS - U_AXIS) * sizeof(float);
        if (nvs.get_blob((std::string("UVW") + _name).c_str(), &_currentValue[U_AXIS], &len)) {
            for (axis_t axis = U_AXIS; axis < MAX_N_AXIS; axis++) {
                _currentValue[axis] = 0;
            }
        }
    }
    return true;
};

void Coordinates::set(float value[MAX_N_AXIS]) {
    memcpy(&_currentValue, value, sizeof(_currentValue));
    if (!is_saved) {
        return;
    }
    if (FORCE_BUFFER_SYNC_DURING_NVS_WRITE) {
        protocol_buffer_synchronize();
    }
    size_t len = U_AXIS * sizeof(float);  // 6 is old MAX_N_AXIS
    nvs.set_blob(_name, _currentValue, len);

    if (MAX_N_AXIS == 9) {
        len = (MAX_N_AXIS - U_AXIS) * sizeof(float);
        nvs.set_blob((std::string("UVW") + _name).c_str(), &_currentValue[U_AXIS], len);
    }
}

IPaddrSetting::IPaddrSetting(
    const char* description, type_t type, permissions_t permissions, const char* grblName, const char* name, uint32_t defVal) :
    Setting(description, type, permissions, grblName, name)  // There are no GRBL IP settings.
    ,
    _defaultValue(defVal), _currentValue(defVal) {
    load();
}

IPaddrSetting::IPaddrSetting(
    const char* description, type_t type, permissions_t permissions, const char* grblName, const char* name, const char* defVal) :
    Setting(description, type, permissions, grblName, name) {
    IPAddress ipaddr;
    Assert(ipaddr.fromString(defVal), "Bad IPaddr default");
    _defaultValue = ipaddr;
    _currentValue = _defaultValue;
    load();
}

void IPaddrSetting::load() {
    if (nvs.get_i32(_keyName, (int32_t*)&_storedValue)) {
        _storedValue  = 0x000000ff;  // Unreasonable value for any IP thing
        _currentValue = _defaultValue;
    } else {
        _currentValue = _storedValue;
    }
}

void IPaddrSetting::setDefault() {
    _currentValue = _defaultValue;
    if (_storedValue != _currentValue) {
        nvs.erase_key(_keyName);
    }
}

Error IPaddrSetting::setStringValue(std::string_view s) {
    Error err = check_state();
    if (err != Error::Ok) {
        return err;
    }
    IPAddress   ipaddr;
    std::string str(s);
    if (!ipaddr.fromString(str.c_str())) {
        return Error::InvalidValue;
    }
    _currentValue = ipaddr;
    if (_storedValue != _currentValue) {
        if (_currentValue == _defaultValue) {
            nvs.erase_key(_keyName);
        } else {
            if (nvs.set_i32(_keyName, (int32_t)_currentValue)) {
                return Error::NvsSetFailed;
            }
            _storedValue = _currentValue;
        }
    }
    return Error::Ok;
}

const char* IPaddrSetting::getDefaultString() {
    static char ipstr[50];
    strncpy(ipstr, IP_string(IPAddress(_defaultValue)).c_str(), 50);
    return ipstr;
}
const char* IPaddrSetting::getStringValue() {
    static char ipstr[50];
    strncpy(ipstr, IP_string(IPAddress(get())).c_str(), 50);
    return ipstr;
}

void IPaddrSetting::addWebui(JSONencoder* j) {
    if (getDescription()) {
        j->begin_webui(getName(), "A", getStringValue());
        j->end_object();
    }
}

Error WebCommand::action(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (_cmdChecker && _cmdChecker()) {
        return Error::AnotherInterfaceBusy;
    }
    char empty = '\0';
    if (!value) {
        value = &empty;
    }
    return _action(value, auth_level, out);
};

const char* IntProxySetting::getStringValue() {
    auto got     = _getter(*MachineConfig::instance());
    _cachedValue = std::to_string(got);
    return _cachedValue.c_str();
}
