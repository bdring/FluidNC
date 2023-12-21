// Class for creating JSON-encoded strings.

#include "JSONEncoder.h"
#include "../Report.h"
#include "../Protocol.h"  // send_line()

namespace WebUI {
    // Constructor.  If _pretty is true, newlines are
    // inserted into the JSON string for easy reading.
    JSONencoder::JSONencoder(bool pretty, Channel* channel) : pretty(pretty), level(0), _str(&linebuf), _channel(channel), category("nvs") {
        count[level] = 0;
    }

    JSONencoder::JSONencoder(bool pretty, std::string* str) : pretty(pretty), level(0), _str(str), category("nvs") { count[level] = 0; }

    void JSONencoder::add(char c) { (*_str) += c; }

    // Private function to add commas between
    // elements as needed, omitting the comma
    // before the first element in a list.
    // If pretty-printing is enabled, a newline
    // is added after the comma.
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
    void JSONencoder::dec_level() { --level; }

    void JSONencoder::indent() {
        for (int i = 0; i < 2 * level; i++) {
            add(' ');
        }
    }

    // Private function to implement pretty-printing
    void JSONencoder::line() {
        if (_channel) {
            // Always pretty print to a channel, because channels
            // cannot necessary handle really long lines.
            add('\n');
            log_to(*_channel, *_str);
            *_str = "";
            indent();
        } else {
            if (pretty) {
                add('\n');
                indent();
            }
        }
    }

    // Begins the JSON encoding process, creating an unnamed object
    void JSONencoder::begin() { begin_object(); }

    // Finishes the JSON encoding process, closing the unnamed object
    // and returning the encoded string
    void JSONencoder::end() {
        end_object();
        line();
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
    void JSONencoder::member(const char* tag, int value) { member(tag, std::to_string(value)); }

    // Creates an Esp32_WebUI configuration item specification from
    // a value passed in as a C-style string.
    void JSONencoder::begin_webui(const char* name, const char* help, const char* type, const char* val) {
        begin_object();
        member("F", category);
        // P is the name that WebUI uses to set a new value.
        // H is the legend that WebUI displays in the UI.
        // The distinction used to be important because, prior to the introuction
        // of named settings, P was a numerical offset into a fixed EEPROM layout.
        // Now P is a hierarchical name that is as easy to understand as the old H values.
        member("P", name);
        member("H", help);
        member("T", type);
        member("V", val);
    }

    // Creates an Esp32_WebUI configuration item specification from
    // an integer value.
    void JSONencoder::begin_webui(const char* brief, const char* full, const char* type, int val) {
        begin_webui(brief, full, type, std::to_string(val).c_str());
    }

    // Creates an Esp32_WebUI configuration item specification from
    // a C-style string value, with additional min and max arguments.
    void JSONencoder::begin_webui(const char* brief, const char* full, const char* type, const char* val, int min, int max) {
        begin_webui(brief, full, type, val);
        member("S", max);
        member("M", min);
    }
}
