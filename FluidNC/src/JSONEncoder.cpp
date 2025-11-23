// Class for creating JSON-encoded strings.

#include "JSONEncoder.h"
#include "Report.h"
#include "Protocol.h"  // send_line()
#include "UartChannel.h"

// Constructor.  If _encapsulate is true, the output is
// encapsulated in [MSG:JSON: ...] lines
JSONencoder::JSONencoder(Channel* channel, const char* json_tag) : level(0), _channel(channel), category("nvs") {
    count[level] = 0;
}
JSONencoder::JSONencoder(JsonCallback callback) : level(0), _callback(callback) {
    count[level] = 0;
}

void JSONencoder::flush() {
    if (_linebuf.length()) {
        if (_channel) {
            if (_json_tag) {
                // Output to channels is encapsulated in [MSG:JSON:...]
                _channel->out_acked(_linebuf, "JSON:");
            } else {
                log_stream(*_channel, _linebuf);
            }

        } else {
            _callback(_linebuf.c_str());
        }
        _linebuf.clear();
    }
}
void JSONencoder::add(char c) {
    _linebuf += c;
    if (_channel && _linebuf.length() >= 100) {
        flush();
    }
}

void JSONencoder::verbatim(const std::string& s) {
    for (auto const c : s) {
        if (c != '\n') {
            add(c);
        }
    }
}

// Private function to add commas between
// elements as needed, omitting the comma
// before the first element in a list.
void JSONencoder::comma_line() {
    if (count[level]) {
        add(',');
        line();
    }
    count[level]++;
}

// Private function to add commas between
// elements as needed, omitting the comma
// before the first element in a list.
void JSONencoder::comma() {
    if (count[level]) {
        add(',');
    }
    count[level]++;
}

// Private function to add a name enclosed with quotes.
void JSONencoder::quoted(const char* s) {
    add('"');
    char c;
    while ((c = *s++) != '\0') {
        // Escape JSON special characters
        switch (c) {
            case '\b':
                add('\\');
                add('b');
                break;
            case '\n':
                add('\\');
                add('n');
                break;
            case '\f':
                add('\\');
                add('f');
                break;
            case '\r':
                add('\\');
                add('r');
                break;
            case '\t':
                add('\\');
                add('t');
                break;
            case '"':
                add('\\');
                add('\"');
                break;
            case '\\':
                add('\\');
                // Fall through
            default:
                add(c);
                break;
        }
    }
    add('"');
}

// Private function to increment the nesting level.
// It's necessary to account for the level in order
// to handle commas properly, as each level must
// know when to omit the comma.
void JSONencoder::inc_level() {
    if (++level == MAX_JSON_LEVEL) {
        --level;
    }
    count[level] = 0;
}

// Private function to increment the nesting level.
void JSONencoder::dec_level() {
    --level;
}

void JSONencoder::indent() {
    for (int i = 0; i < 2 * level; i++) {
        add(' ');
    }
}

void JSONencoder::string(const char* s) {
    comma_line();
    quoted(s);
}

// line() is called at places in the JSON stream where it would be
// reasonable to insert a newline without causing syntax problems.
// We want to limit the line length when the output is going to an
// unencapsulated serial channel, since some receivers might have line
// length limits.  For encapsulated serial channels, we want to
// pack as many characters as possible into the line to reduce the
// encapsulation overhead.  The decapsulation will splice together
// pieces so there is no problem if a token is split across two
// encapsulation packets.
void JSONencoder::line() {
    if (_channel) {
        if (_json_tag) {
            // In encapsulated mode, we just collect data until
            // the line is almost full, then wrap it in [MSG:JSON:...]
        } else {
            // log_stream() always adds a newline
            // We want that for channels because they might not
            // be able to handle really long lines.
            log_stream(*_channel, _linebuf);
            _linebuf.clear();
            indent();
        }
    } else {
        add('\n');
        indent();
    }
}

// Begins the JSON encoding process, creating an unnamed object
void JSONencoder::begin() {
    if (_channel && _json_tag) {
        _channel->beginJSON(_json_tag);
    }
    begin_object();
}

// Finishes the JSON encoding process, closing the unnamed object
// and returning the encoded string
void JSONencoder::end() {
    end_object();
    line();
    flush();
    if (_channel && _json_tag) {
        _channel->endJSON(_json_tag);
    }
}

// Starts a member element.
void JSONencoder::begin_member(const char* tag) {
    comma_line();
    quoted(tag);
    add(':');
}

// Starts an array with "tag":[
void JSONencoder::begin_array(const char* tag) {
    begin_member(tag);
    add('[');
    inc_level();
    line();
}

// Ends an array with ]
void JSONencoder::end_array() {
    dec_level();
    line();
    add(']');
}

// Begins the creation of a member whose value is an object.
// Call end_object() to close the member
void JSONencoder::begin_member_object(const char* tag) {
    comma_line();
    quoted(tag);
    add(':');
    add('{');
    inc_level();
}

// Starts an object with {.
// If you need a named object you must call begin_member() first.
void JSONencoder::begin_object() {
    comma_line();
    add('{');
    inc_level();
}

// Ends an object with }.
void JSONencoder::end_object() {
    dec_level();
    line();
    add('}');
}

// Creates a "tag":"value" member from a C-style string
void JSONencoder::member(const char* tag, const char* value) {
    begin_member(tag);
    quoted(value);
}

// Creates a "tag":"value" member from a C++ string
void JSONencoder::member(const char* tag, const std::string& value) {
    begin_member(tag);
    quoted(value.c_str());
}

// Creates a "tag":"value" member from an integer
void JSONencoder::member(const char* tag, int32_t value) {
    member(tag, std::to_string(value));
}

// Creates a WebUI configuration item specification from
// a value passed in as a C-style string.
void JSONencoder::begin_webui(const std::string name, const char* type, const char* val) {
    begin_object();
    member("F", category);
    // P is the name that WebUI uses to set a new value.
    // H is the legend that WebUI displays in the UI.
    // The distinction used to be important because, prior to the introuction
    // of named settings, P was a numerical offset into a fixed EEPROM layout.
    // Now P is a hierarchical name that is as easy to understand as the old H values.
    member("P", name.c_str());
    member("H", name.c_str());
    member("T", type);
    member("V", val);
}

// Creates an WebUI configuration item specification from
// an integer value.
void JSONencoder::begin_webui(const std::string name, const char* type, int32_t val) {
    begin_webui(name, type, std::to_string(val).c_str());
}

// Creates an WebUI configuration item specification from
// a C-style string value, with additional min and max arguments.
void JSONencoder::begin_webui(const std::string name, const char* type, const char* val, int32_t min, int32_t max) {
    begin_webui(name, type, val);
    member("S", max);
    member("M", min);
}

// id, value objects are used extensively by WebUI ESP420 so it is worthwhile
// to have helper routines for them.
void JSONencoder::id_value_object(const char* id, const char* value) {
    begin_object();
    member("id", id);
    member("value", value);
    end_object();
}

void JSONencoder::id_value_object(const char* id, const std::string& value) {
    id_value_object(id, value.c_str());
}

void JSONencoder::id_value_object(const char* id, int32_t value) {
    id_value_object(id, std::to_string(value));
}
