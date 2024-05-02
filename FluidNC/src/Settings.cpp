#include "Settings.h"

#include "WebUI/JSONEncoder.h"  // JSON
#include "WebUI/WifiConfig.h"   // WebUI::WiFiConfig
#include "WebUI/Commands.h"     // WebUI::COMMANDS
#include "System.h"             // sys
#include "Protocol.h"           // protocol_buffer_synchronize
#include "Machine/MachineConfig.h"

#include <map>
#include <limits>
#include <cstring>
#include <vector>
#include <charconv>
#include <nvs.h>

std::vector<Setting*> Setting::List __attribute__((init_priority(101))) = {};
std::vector<Command*> Command::List __attribute__((init_priority(102))) = {};

bool anyState() {
    return false;
}
bool notIdleOrJog() {
    return !state_is(State::Idle) && !state_is(State::Jog);
}
bool notIdleOrAlarm() {
    return !state_is(State::Idle) && !state_is(State::Alarm) && !state_is(State::ConfigAlarm);
}
bool cycleOrHold() {
    return state_is(State::Cycle) || state_is(State::Hold);
}

bool allowConfigStates() {
    return !state_is(State::Idle) && !state_is(State::Alarm) && !state_is(State::ConfigAlarm) && !state_is(State::Critical);
}

Word::Word(type_t type, permissions_t permissions, const char* description, const char* grblName, const char* fullName) :
    _description(description), _grblName(grblName), _fullName(fullName), _type(type), _permissions(permissions) {}

Command::Command(
    const char* description, type_t type, permissions_t permissions, const char* grblName, const char* fullName, bool (*cmdChecker)()) :
    Word(type, permissions, description, grblName, fullName),
    _cmdChecker(cmdChecker) {
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
        char*    hashName = (char*)malloc(16);
        uint32_t hash     = len;
        for (const char* s = fullName; *s; s++) {
            hash = ((hash << 5) ^ (hash >> 27)) ^ (*s);
        }
        sprintf(hashName, "%.7s%08x", fullName, hash);
        _keyName = hashName;
    }
}

Error Setting::check_state() {
    if (notIdleOrAlarm()) {
        return Error::IdleError;
    }
    return Error::Ok;
}

nvs_handle Setting::_handle = 0;

void Setting::init() {
    if (!_handle) {
        if (esp_err_t err = nvs_open("FluidNC", NVS_READWRITE, &_handle)) {
            log_debug("nvs_open failed with error " << err);
        }
    }
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
    _defaultValue(defVal), _currentValue(defVal), _minValue(minVal), _maxValue(maxVal), _currentIsNvm(currentIsNvm) {
    _storedValue = std::numeric_limits<int32_t>::min();
}

void IntSetting::load() {
    esp_err_t err = nvs_get_i32(_handle, _keyName, &_storedValue);
    if (err) {
        _storedValue  = std::numeric_limits<int32_t>::min();
        _currentValue = _defaultValue;
    } else {
        _currentValue = _storedValue;
    }
}

void IntSetting::setDefault() {
    if (_currentIsNvm) {
        nvs_erase_key(_handle, _keyName);
    } else {
        _currentValue = _defaultValue;
        if (_storedValue != _currentValue) {
            nvs_erase_key(_handle, _keyName);
        }
    }
}

Error IntSetting::setStringValue(std::string_view s) {
    Error err = check_state();
    if (err != Error::Ok) {
        return err;
    }
    trim(s);
    int32_t convertedValue;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.length(), convertedValue);
    if (ec != std::errc()) {
        return Error::BadNumberFormat;
    }
    if (convertedValue < _minValue || convertedValue > _maxValue) {
        return Error::NumberRange;
    }

    // If we don't see the NVM state, we have to make this the live value:
    if (!_currentIsNvm) {
        _currentValue = convertedValue;
    }

    if (_storedValue != convertedValue) {
        if (convertedValue == _defaultValue) {
            nvs_erase_key(_handle, _keyName);
        } else {
            if (nvs_set_i32(_handle, _keyName, convertedValue)) {
                return Error::NvsSetFailed;
            }
            _storedValue = convertedValue;
        }
    }
    return Error::Ok;
}

const char* IntSetting::getDefaultString() {
    static char strval[32];
    sprintf(strval, "%d", _defaultValue);
    return strval;
}

const char* IntSetting::getStringValue() {
    static char strval[32];

    int currentSettingValue;
    if (_currentIsNvm) {
        if (std::numeric_limits<int32_t>::min() == _storedValue) {
            currentSettingValue = _defaultValue;
        } else {
            currentSettingValue = _storedValue;
        }
    } else {
        currentSettingValue = get();
    }

    sprintf(strval, "%d", currentSettingValue);
    return strval;
}

void IntSetting::addWebui(WebUI::JSONencoder* j) {
    if (getDescription()) {
        j->begin_webui(getName(), getName(), "I", getStringValue(), _minValue, _maxValue);
        j->end_object();
    }
}

StringSetting::StringSetting(const char*   description,
                             type_t        type,
                             permissions_t permissions,
                             const char*   grblName,
                             const char*   name,
                             const char*   defVal,
                             int           min,
                             int           max) :
    Setting(description, type, permissions, grblName, name) {
    _defaultValue = defVal;
    _currentValue = defVal;
    _minLength    = min;
    _maxLength    = max;
};

void StringSetting::load() {
    size_t    len = 0;
    esp_err_t err = nvs_get_str(_handle, _keyName, NULL, &len);
    if (err) {
        _storedValue  = _defaultValue;
        _currentValue = _defaultValue;
        return;
    }

    // TODO: Can't we allocate the string immediately?
    std::vector<char> buffer;
    buffer.resize(len);
    char* buf = buffer.data();
    err       = nvs_get_str(_handle, _keyName, buf, &len);
    if (err) {
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
        nvs_erase_key(_handle, _keyName);
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
            nvs_erase_key(_handle, _keyName);
            _storedValue = _defaultValue;
        } else {
            if (nvs_set_str(_handle, _keyName, _currentValue.c_str())) {
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

void StringSetting::addWebui(WebUI::JSONencoder* j) {
    if (!getDescription()) {
        return;
    }
    j->begin_webui(getName(), getName(), "S", getStringValue(), _minLength, _maxLength);
    j->end_object();
}

// typedef std::map<const char*, int8_t, cmp_str> enum_opt_t;
// typedef std::map<const char*, int8_t, std::less<>> enum_opt_t;

EnumSetting::EnumSetting(
    const char* description, type_t type, permissions_t permissions, const char* grblName, const char* name, int8_t defVal, const enum_opt_t* opts) :
    Setting(description, type, permissions, grblName, name),
    _defaultValue(defVal), _options(opts) {}

void EnumSetting::load() {
    esp_err_t err = nvs_get_i8(_handle, _keyName, &_storedValue);
    if (err) {
        _storedValue  = -1;
        _currentValue = _defaultValue;
    } else {
        _currentValue = _storedValue;
    }
}

void EnumSetting::setDefault() {
    _currentValue = _defaultValue;
    if (_storedValue != _currentValue) {
        nvs_erase_key(_handle, _keyName);
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
    std::string          str(s);
    enum_opt_t::const_iterator it = _options->find(str.c_str());
    if (it == _options->end()) {
        // If we don't find the value in keys, look for it in the numeric values

        // Disallow empty string
        if (!s.length()) {
            showList();
            return Error::BadNumberFormat;
        }
        char* endptr;
        int   num;
        // Disallow non-numeric characters in string
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.length(), num);
        if (ec != std::errc()) {
            showList();
            return Error::BadNumberFormat;
        }
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
            nvs_erase_key(_handle, _keyName);
        } else {
            if (nvs_set_i8(_handle, _keyName, _currentValue)) {
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

void EnumSetting::addWebui(WebUI::JSONencoder* j) {
    if (!getDescription()) {
        return;
    }
    j->begin_webui(getName(), getName(), "B", get());
    j->begin_array("O");
    for (enum_opt_t:: const_iterator it = _options->begin(); it != _options->end(); it++) {
        j->begin_object();
        j->member(it->first, it->second);
        j->end_object();
    }
    j->end_array();
    j->end_object();
}

Error UserCommand::action(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
    if (_cmdChecker && _cmdChecker()) {
        return Error::IdleError;
    }
    return _action((const char*)value, auth_level, out);
};
Coordinates* coords[CoordIndex::End];

bool Coordinates::load() {
    size_t len;
    switch (nvs_get_blob(Setting::_handle, _name, _currentValue, &len)) {
        case ESP_OK:
            return true;
        case ESP_ERR_NVS_INVALID_LENGTH:
            // This could happen if the stored value is longer than the buffer.
            // That is highly unlikely since we always store MAX_N_AXIS coordinates.
            // It would indicate that we have decreased MAX_N_AXIS since the
            // value was stored.  We don't flag it as an error, but rather
            // accept the initial coordinates and ignore the residue.
            // We could issue a warning message if we were so inclined.
            return true;
        case ESP_ERR_NVS_INVALID_NAME:
        case ESP_ERR_NVS_INVALID_HANDLE:
        default:
            return false;
    }
};

void Coordinates::set(float value[MAX_N_AXIS]) {
    memcpy(&_currentValue, value, sizeof(_currentValue));
    if (FORCE_BUFFER_SYNC_DURING_NVS_WRITE) {
        protocol_buffer_synchronize();
    }
    nvs_set_blob(Setting::_handle, _name, _currentValue, sizeof(_currentValue));
}

IPaddrSetting::IPaddrSetting(
    const char* description, type_t type, permissions_t permissions, const char* grblName, const char* name, uint32_t defVal) :
    Setting(description, type, permissions, grblName, name)  // There are no GRBL IP settings.
    ,
    _defaultValue(defVal), _currentValue(defVal) {}

IPaddrSetting::IPaddrSetting(
    const char* description, type_t type, permissions_t permissions, const char* grblName, const char* name, const char* defVal) :
    Setting(description, type, permissions, grblName, name) {
    IPAddress ipaddr;
    if (ipaddr.fromString(defVal)) {
        _defaultValue = ipaddr;
        _currentValue = _defaultValue;
    } else {
        throw std::runtime_error("Bad IPaddr default");
    }
}

void IPaddrSetting::load() {
    esp_err_t err = nvs_get_i32(_handle, _keyName, (int32_t*)&_storedValue);
    if (err) {
        _storedValue  = 0x000000ff;  // Unreasonable value for any IP thing
        _currentValue = _defaultValue;
    } else {
        _currentValue = _storedValue;
    }
}

void IPaddrSetting::setDefault() {
    _currentValue = _defaultValue;
    if (_storedValue != _currentValue) {
        nvs_erase_key(_handle, _keyName);
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
            nvs_erase_key(_handle, _keyName);
        } else {
            if (nvs_set_i32(_handle, _keyName, (int32_t)_currentValue)) {
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

void IPaddrSetting::addWebui(WebUI::JSONencoder* j) {
    if (getDescription()) {
        j->begin_webui(getName(), getName(), "A", getStringValue());
        j->end_object();
    }
}

template <>
const char* MachineConfigProxySetting<float>::getStringValue() {
    auto got = _getter(*MachineConfig::instance());
    _cachedValue.reserve(16);
    std::snprintf(_cachedValue.data(), _cachedValue.capacity(), "%.3f", got);
    return _cachedValue.c_str();
}

template <>
const char* MachineConfigProxySetting<int32_t>::getStringValue() {
    auto got     = _getter(*MachineConfig::instance());
    _cachedValue = std::to_string(got);
    return _cachedValue.c_str();
}
