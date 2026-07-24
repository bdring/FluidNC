// Copyright (c) 2026 - FluidNC Contributors
// Use of this source code be governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <string>
#include <string_view>

namespace WebUI {

// Parse an HTTP command string of the form:
//   url
//   url{json_options}
// Token substitution patterns like ${...} are skipped when searching for the JSON block.
bool parse_http_command(std::string_view value, std::string& url, std::string& json_options);

}  // namespace WebUI
