#pragma once

#include "WebUI/JSONEncoder.h"
#include "WebUI/Authentication.h"
#include "Report.h"  // info_channel
#include "GCode.h"   // CoordIndex

#include <string_view>
#include <map>
#include <nvs.h>
#include <string_view>

// forward declarations
namespace Machine {
    class MachineConfig;
}

// Initialize the configuration subsystem
void settings_init();

// Define settings restore bitflags.
enum SettingsRestore {
    Defaults     = bitnum_to_mask(0),
    Parameters   = bitnum_to_mask(1),
    StartupLines = bitnum_to_mask(2),
    // BuildInfo = bitnum_to_mask(3), // Obsolete
    Wifi = bitnum_to_mask(4),
    All  = 0xff,
};

// Restore subsets of settings to default values
void settings_restore(uint8_t restore_flag);

// This abstract class defines the generic interface that
// is used to set and get values for all settings independent
// of their underlying data type.  The values are always
// represented as human-readable strings.  This generic
// interface is used for managing settings via the user interface.

// Derived classes implement these generic functions for different
// kinds of data.  Code that accesses settings should use only these
// generic functions and should not use derived classes directly.

enum {
    NO_AXIS = 255,
};
typedef enum : uint8_t {
    GRBL = 1,  // Classic GRBL settings like $100
    EXTENDED,  // Settings added by early versions of Grbl_Esp32
    WEBSET,    // Settings for ESP3D_WebUI, stored in NVS
    PIN,       // Pin settings
    GRBLCMD,   // Non-persistent GRBL commands like $H
    WEBCMD,    // ESP3D_WebUI commands that are not directly settings
} type_t;

typedef enum : uint8_t {
    WG,  // Readable and writable as guest
    WU,  // Readable and writable as user and admin
    WA,  // Readable as user and admin, writable as admin
} permissions_t;

typedef uint8_t axis_t;

class Word {
protected:
    const char*   _description;
    const char*   _grblName;
    const char*   _fullName;
    type_t        _type;
    permissions_t _permissions;

public:
    Word(type_t type, permissions_t permissions, const char* description, const char* grblName, const char* fullName);
    type_t        getType() { return _type; }
    permissions_t getPermissions() { return _permissions; }
    const char*   getName() { return _fullName; }
    const char*   getGrblName() { return _grblName; }
    const char*   getDescription() { return _description; }
};

class Command : public Word {
protected:
    bool (*_cmdChecker)();

public:
    // Command::List is a vector of all commands,
    // so common code can enumerate them.
    static std::vector<Command*> List;

    ~Command() {}
    Command(const char* description, type_t type, permissions_t permissions, const char* grblName, const char* fullName, bool (*cmdChecker)());

    // The default implementation of addWebui() does nothing.
    // Derived classes may override it to do something.
    virtual void addWebui(WebUI::JSONencoder*) {};

    virtual Error action(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) = 0;
};

class Setting : public Word {
private:
protected:
    // group_t _group;
    axis_t      _axis = NO_AXIS;
    const char* _keyName;

public:
    static nvs_handle _handle;
    static void       init();

    // Setting::List is a vector of all settings,
    // so common code can enumerate them.
    static std::vector<Setting*> List;

    Error check_state();

    static Error report_nvs_stats(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
        nvs_stats_t stats;
        if (esp_err_t err = nvs_get_stats(NULL, &stats)) {
            return Error::NvsGetStatsFailed;
        }

        log_info("NVS Used:" << stats.used_entries << " Free:" << stats.free_entries << " Total:" << stats.total_entries);
#if 0  // The SDK we use does not have this yet
        nvs_iterator_t it = nvs_entry_find(NULL, NULL, NVS_TYPE_ANY);
        while (it != NULL) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            it = nvs_entry_next(it);
            log_info("namespace:"<<info.namespace_name<<" key:"<<info.key<<" type:"<< info.type);
        }
#endif
        return Error::Ok;
    }

    static Error eraseNVS(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out) {
        nvs_erase_all(_handle);
        return Error::Ok;
    }

    ~Setting() {}
    Setting(const char* description, type_t type, permissions_t permissions, const char* grblName, const char* fullName);
    axis_t getAxis() { return _axis; }
    void   setAxis(axis_t axis) { _axis = axis; }

    // load() reads the backing store to get the current
    // value of the setting.  This could be slow so it
    // should be done infrequently, typically once at startup.
    virtual void load() {};
    virtual void setDefault() {};

    // The default implementation of addWebui() does nothing.
    // Derived classes may override it to do something.
    virtual void addWebui(WebUI::JSONencoder*) {};

    virtual Error       setStringValue(std::string_view s) = 0;
    virtual const char* getStringValue()                   = 0;
    virtual const char* getCompatibleValue() { return getStringValue(); }
    virtual const char* getDefaultString() = 0;
};

class IntSetting : public Setting {
private:
    int32_t _defaultValue;
    int32_t _currentValue;
    int32_t _storedValue;
    int32_t _minValue;
    int32_t _maxValue;
    bool    _currentIsNvm;

public:
    IntSetting(const char*   description,
               type_t        type,
               permissions_t permissions,
               const char*   grblName,
               const char*   name,
               int32_t       defVal,
               int32_t       minVal,
               int32_t       maxVal,
               bool          currentIsNvm = false);

    IntSetting(type_t        type,
               permissions_t permissions,
               const char*   grblName,
               const char*   name,
               int32_t       defVal,
               int32_t       minVal,
               int32_t       maxVal,
               bool          currentIsNvm = false) :
        IntSetting(NULL, type, permissions, grblName, name, defVal, minVal, maxVal, currentIsNvm) {}

    void        load();
    void        setDefault();
    void        addWebui(WebUI::JSONencoder*);
    Error       setStringValue(std::string_view s);
    const char* getStringValue();
    const char* getDefaultString();

    int32_t get() { return _currentValue; }
};

// See Settings.cpp for the int32_t and float specialization implementations
template <typename T>
class MachineConfigProxySetting : public Setting {
    std::function<T(Machine::MachineConfig const&)> _getter;
    std::string                                     _cachedValue;

public:
    MachineConfigProxySetting(const char* grblName, const char* fullName, std::function<T(Machine::MachineConfig const&)> getter) :
        Setting(fullName, type_t::GRBL, permissions_t::WU, grblName, fullName), _getter(getter), _cachedValue("") {}

    const char* getStringValue() override;
    Error       setStringValue(std::string_view value) { return Error::ReadOnlySetting; }
    const char* getDefaultString() override { return ""; }
};

class Coordinates {
private:
    float       _currentValue[MAX_N_AXIS];
    const char* _name;

public:
    Coordinates(const char* name) : _name(name) {}

    const char* getName() { return _name; }
    bool        load();
    void        setDefault() {
        float zeros[MAX_N_AXIS] = {
            0.0,
        };
        set(zeros);
    };
    // Copy the value to an array
    void get(float* value) { memcpy(value, _currentValue, sizeof(_currentValue)); }
    // Return a pointer to the array
    const float* get() { return _currentValue; }
    void         set(float* value);
};

extern Coordinates* coords[CoordIndex::End];

class StringSetting : public Setting {
private:
    std::string _defaultValue;
    std::string _currentValue;
    std::string _storedValue;
    int         _minLength;
    int         _maxLength;
    void        _setStoredValue(const char* s);

public:
    StringSetting(const char*   description,
                  type_t        type,
                  permissions_t permissions,
                  const char*   grblName,
                  const char*   name,
                  const char*   defVal,
                  int           min,
                  int           max);

    StringSetting(type_t type, permissions_t permissions, const char* grblName, const char* name, const char* defVal) :
        StringSetting(NULL, type, permissions, grblName, name, defVal, 0, 0) {};

    void        load();
    void        setDefault();
    void        addWebui(WebUI::JSONencoder*);
    Error       setStringValue(std::string_view s);
    const char* getStringValue();
    const char* getDefaultString();

    const char* get() { return _currentValue.c_str(); }
};

struct cmp_str {
    bool operator()(char const* a, char const* b) const { return strcasecmp(a, b) < 0; }
};
typedef std::map<const char*, int8_t, cmp_str> enum_opt_t;

class EnumSetting : public Setting {
private:
    int8_t                                  _defaultValue;
    int8_t                                  _storedValue;
    int8_t                                  _currentValue;
    std::map<const char*, int8_t, cmp_str>* _options;
    const char*                             enumToString(int8_t value);

public:
    EnumSetting(const char*   description,
                type_t        type,
                permissions_t permissions,
                const char*   grblName,
                const char*   name,
                int8_t        defVal,
                enum_opt_t*   opts);

    EnumSetting(type_t type, permissions_t permissions, const char* grblName, const char* name, int8_t defVal, enum_opt_t* opts) :
        EnumSetting(NULL, type, permissions, grblName, name, defVal, opts) {}

    void        load();
    void        setDefault();
    void        addWebui(WebUI::JSONencoder*);
    Error       setStringValue(std::string_view s);
    const char* getStringValue();
    const char* getDefaultString();
    void        showList();

    int8_t get() { return _currentValue; }
};

extern bool notIdleOrJog();
extern bool notIdleOrAlarm();
extern bool anyState();
extern bool cycleOrHold();

class IPaddrSetting : public Setting {
private:
    uint32_t _defaultValue;
    uint32_t _currentValue;
    uint32_t _storedValue;

public:
    IPaddrSetting(const char* description, type_t type, permissions_t permissions, const char* grblName, const char* name, uint32_t defVal);
    IPaddrSetting(const char* description, type_t type, permissions_t permissions, const char* grblName, const char* name, const char* defVal);

    void        load();
    void        setDefault();
    void        addWebui(WebUI::JSONencoder*);
    Error       setStringValue(std::string_view s);
    const char* getStringValue();
    const char* getDefaultString();

    uint32_t get() { return _currentValue; }
};

class WebCommand : public Command {
private:
    Error (*_action)(const char*, WebUI::AuthenticationLevel, Channel& out);
    const char* password;

public:
    WebCommand(const char*   description,
               type_t        type,
               permissions_t permissions,
               const char*   grblName,
               const char*   name,
               Error (*action)(const char*, WebUI::AuthenticationLevel, Channel& out),
               bool (*cmdChecker)() = notIdleOrAlarm) :
        Command(description, type, permissions, grblName, name, cmdChecker),
        _action(action) {}

    Error action(const char* value, WebUI::AuthenticationLevel auth_level, Channel& out);
};

class UserCommand : public Command {
private:
    Error (*_action)(const char*, WebUI::AuthenticationLevel, Channel&);

public:
    UserCommand(const char* grblName,
                const char* name,
                Error (*action)(const char*, WebUI::AuthenticationLevel, Channel&),
                bool (*cmdChecker)(),
                permissions_t auth = WG) :
        Command(NULL, GRBLCMD, auth, grblName, name, cmdChecker),
        _action(action) {}

    Error action(const char* value, WebUI::AuthenticationLevel auth_level, Channel& response);
};

// Execute the startup script lines stored in non-volatile storage upon initialization
void  settings_execute_startup();
Error settings_execute_line(char* line, Channel& out, WebUI::AuthenticationLevel);
Error do_command_or_setting(const char* key, const char* value, WebUI::AuthenticationLevel auth_level, Channel&);
Error execute_line(char* line, Channel& channel, WebUI::AuthenticationLevel auth_level);

extern enum_opt_t onoffOptions;
