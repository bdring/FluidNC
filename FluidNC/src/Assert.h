// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "StackTrace/AssertionFailed.h"

class AssertionFailed;

#undef Assert

#define Stringify(x) #x
#define Stringify2(x) Stringify(x)
#define Assert(condition, ...)                                                                                                             \
    {                                                                                                                                      \
        if (!(condition)) {                                                                                                                \
            const char* ch = #condition " (@line " Stringify2(__LINE__) ")";                                                               \
            throw AssertionFailed::create(ch, ##__VA_ARGS__);                                                                              \
        }                                                                                                                                  \
    }
