// Class for creating JSON-encoded strings.

#include "JSONEncoder.h"
#include "../Report.h"

namespace WebUI {
    // Constructor.  If _pretty is true, newlines are
    // inserted into the JSON string for easy reading.
    JSONencoder::JSONencoder(bool pretty, Print* s) : pretty(pretty), level(0), str(""), stream(s), category("nvs") { count[level] = 0; }

    // Constructor.  If _pretty is true, newlines are
    // inserted into the JSON string for easy reading.
    JSONencoder::JSONencoder(bool pretty) : JSONencoder(pretty, nullptr) {}

    // Constructor that supplies a default falue for "pretty"
    JSONencoder::JSONencoder() : JSONencoder(false) {}

    void JSONencoder::add(char c) {
        if (stream) {
            (*stream) << c;
        } else {
            str += c;
        }
    }

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
        if (stream) {
            (*stream) << s;
        } else {
            str.concat(s);
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

    // Private function to implement pretty-printing
    void JSONencoder::line() {
        if (pretty) {
            add('\n');
            for (int i = 0; i < 2 * level; i++) {
                add(' ');
            }
        }
    }

    // Begins the JSON encoding process, creating an unnamed object
    void JSONencoder::begin() { begin_object(); }

    // Finishes the JSON encoding process, closing the unnamed object
    // and returning the encoded string
    String JSONencoder::end() {
        end_object();
        if (pretty) {
            add('\n');
        }
        return str;
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

    // Creates a "tag":"value" member from an Arduino string
    void JSONencoder::member(const char* tag, String value) {
        begin_member(tag);
        quoted(value.c_str());
    }

    // Creates a "tag":"value" member from an integer
    void JSONencoder::member(const char* tag, int value) { member(tag, String(value)); }

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
        begin_webui(brief, full, type, String(val).c_str());
    }

    // Creates an Esp32_WebUI configuration item specification from
    // a C-style string value, with additional min and max arguments.
    void JSONencoder::begin_webui(const char* brief, const char* full, const char* type, const char* val, int min, int max) {
        begin_webui(brief, full, type, val);
        member("S", max);
        member("M", min);
    }
}
