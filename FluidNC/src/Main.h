// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <WString.h>

void main_init();
void run_once();

// Callouts to custom code
void machine_init();
void display_init();
void display(const char* tag, String s);
