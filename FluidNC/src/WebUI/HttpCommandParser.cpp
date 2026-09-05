// Copyright (c) 2026 - FluidNC Contributors
// Use of this source code be governed by a GPLv3 license that can be found in the LICENSE file.

#include "HttpCommandParser.h"

namespace WebUI {

bool parse_http_command(std::string_view value, std::string& url, std::string& json_options) {
    if (value.empty()) {
        return false;
    }

    size_t pos = 0;
    size_t json_pos = std::string_view::npos;
    bool in_string = false;
    bool escape = false;

    while (pos < value.size()) {
        char c = value[pos];

        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            pos++;
            continue;
        }

        if (c == '$' && pos + 1 < value.size() && value[pos + 1] == '{') {
            pos += 2;
            while (pos < value.size() && value[pos] != '}') {
                pos++;
            }
            if (pos < value.size() && value[pos] == '}') {
                pos++;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
            pos++;
            continue;
        }

        if (c == '{') {
            json_pos = pos;
            break;
        }

        pos++;
    }

    if (json_pos == std::string_view::npos) {
        url.assign(value);
        json_options.clear();
        return !url.empty();
    }

    if (json_pos == 0) {
        return false;
    }

    url.assign(value.substr(0, json_pos));

    int brace_count = 1;
    pos = json_pos + 1;
    in_string = false;
    escape = false;

    while (pos < value.size() && brace_count > 0) {
        char c = value[pos];

        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            pos++;
            continue;
        }

        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            brace_count++;
        } else if (c == '}') {
            brace_count--;
        }
        pos++;
    }

    if (brace_count != 0) {
        return false;
    }

    json_options.assign(value.substr(json_pos, pos - json_pos));
    return !url.empty();
}

}  // namespace WebUI
