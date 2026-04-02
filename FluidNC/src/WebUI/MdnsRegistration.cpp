// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Module.h"
#include "Mdns.h"

namespace {
    EnumSetting* mdnsEnableSetting = new EnumSetting("mDNS enable", WEBSET, WA, NULL, "MDNS/Enable", true, &onoffOptions);

    struct MdnsSettingsBinding {
        MdnsSettingsBinding() {
            WebUI::Mdns::setEnableSetting(mdnsEnableSetting);
        }
    } mdnsSettingsBinding;

    ModuleFactory::InstanceBuilder<WebUI::Mdns> __attribute__((init_priority(107))) mdns_module("mdns", true);
}
