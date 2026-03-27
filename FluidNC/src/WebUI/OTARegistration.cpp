// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Module.h"
#include "OTA.h"

namespace {
    ModuleFactory::InstanceBuilder<WebUI::OTA> __attribute__((init_priority(106))) ota_module("ota", true);
}
