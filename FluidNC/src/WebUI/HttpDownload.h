// Copyright (c) 2026 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Settings.h"
#include "../Channel.h"

#include <string>

namespace WebUI {

    // Download a file over HTTP(S) into a filesystem file (SD by default).
    //
    //   $File/Download=<url> [dest]
    //   $GitHub/Get=<owner>/<repo>/<ref>/<path> [dest]
    //
    // See HttpDownload.cpp for the full description.
    class HttpDownload {
    public:
        // grbl name FDL: $File/Download=<url> [dest]
        static Error file_download(const char* value, AuthenticationLevel auth_level, Channel& out);

        // grbl name GHG: $GitHub/Get=<owner>/<repo>/<ref>/<path> [dest]
        static Error github_get(const char* value, AuthenticationLevel auth_level, Channel& out);

    private:
        static Error download(const std::string& url, std::string dest, Channel& out);
    };

}  // namespace WebUI
