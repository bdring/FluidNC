// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "AssertionFailed.h"

class AssertionFailed;

#undef Assert

#define Assert(condition, ...)                                                                                                             \
    {                                                                                                                                      \
        if (!(condition)) {                                                                                                                \
            throw AssertionFailed::create(__VA_ARGS__);                                                                                    \
        }                                                                                                                                  \
    }
