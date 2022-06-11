// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

namespace Configuration {
    enum struct HandlerType { Parser, AfterParse, Runtime, Generator, Validator, Completer };
}
