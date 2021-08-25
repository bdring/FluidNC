// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

namespace Configuration {
    enum class TokenState {
        Bof,
        Matching,
        Matched,
        Held,
        Eof,
    };
}
