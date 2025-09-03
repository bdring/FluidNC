#pragma once

#include "Channel.h"
#include <string>

// Class for creating JSON-encoded strings.

class JSONencoder {
private:
    static const int32_t MAX_JSON_LEVEL = 16;

    bool    _encapsulate = false;
    int32_t level;
    int32_t count[MAX_JSON_LEVEL];
    void    add(char c);
    void    comma_line();
    void    comma();
    void    inc_level();
    void    dec_level();
    void    indent();
    void    line();

    void quoted(const char* s);

    std::string linebuf;

    std::string* _str     = nullptr;
    Channel*     _channel = nullptr;

    std::string category;

    void flush();

public:
    // Constructor; set _encapsulate true for [MSG:JSON: ,,,] encapsulation
    JSONencoder(bool encapsulate, Channel* channel);
    explicit JSONencoder(std::string* str);

    // begin() starts the encoding process.
    void begin();

    void setCategory(const char* cat) { category = cat; }

    void end();

    void string(const char* s);

    // begin_member() starts the creation of a member.
    void begin_member(const char* tag);

    // member() creates a "tag":"value" element
    void member(const char* tag, const char* value);
    void member(const char* tag, const std::string& value);
    void member(const char* tag, int32_t value);

    // begin_array() starts a "tag":[  array element
    void begin_array(const char* tag);

    // end_array() closes the array with ]
    void end_array();

    // begin_object() starts an object with {
    void begin_object();

    // end_object() closes the object with }
    void end_object();

    // Begins the creation of a member whose value is an object.
    // Call end_object() to close the member
    void begin_member_object(const char* tag);

    void verbatim(const std::string& s);

    // The begin_webui() methods are specific to Esp3D_WebUI
    // WebUI sends JSON objects to the UI to generate configuration
    // page entries. Each object describes a named setting with a
    // type, current value, and a description of the possible values.
    // The possible values can either be a minumum and maximum
    // integer value, min/max string length, or an enumeration list.
    // When the user chooses a value, this command is sent back:
    //   [ESP401]P=p T=type V=value
    // P: parameter name
    // T: type
    // M: min_val
    // S: max_val
    // O: options:[ { "name", "value" } ... ]
    // V: currentvalue
    // H: label
    // F: F       ("network", used for filtering)
    // If M and S are not supplied, they are inferred from type:
    //  B => -127 .. 255
    //  S => 0 .. 255
    //  A => 7 .. 15  (0.0.0.0 .. 255.255.255.255)
    //  I => 0 .. 2^31-1
    void begin_webui(const std::string name, const char* type, const std::string& val) { begin_webui(name, type, val.c_str()); }
    void begin_webui(const std::string name, const char* type, const char* val);
    void begin_webui(const std::string name, const char* type, const int32_t val);
    void begin_webui(const std::string name, const char* type, const char* val, int32_t min, int32_t max);

    void id_value_object(const char* id, const char* value);
    void id_value_object(const char* id, const std::string& value);
    void id_value_object(const char* id, int32_t value);
};
