#pragma once

#include <cstdint>
#include <string>
#include <vector>

inline int                                     g_mdnsInitResult = 0;
inline int                                     g_mdnsHostnameSetResult = 0;
inline int                                     g_mdnsFreeCalls = 0;
inline std::vector<std::string>                g_mdnsAddedServices;
inline std::vector<std::pair<std::string,std::string>> g_mdnsRemovedServices;

inline int mdns_init() {
    return g_mdnsInitResult;
}

inline int mdns_hostname_set(const char*) {
    return g_mdnsHostnameSetResult;
}

inline void mdns_free() {
    ++g_mdnsFreeCalls;
}

inline void mdns_service_add(const char*, const char* service, const char* proto, uint16_t port, void*, int) {
    g_mdnsAddedServices.emplace_back(std::string(service) + "/" + proto + ":" + std::to_string(port));
}

inline void mdns_service_remove(const char* service, const char* proto) {
    g_mdnsRemovedServices.emplace_back(service ? service : "", proto ? proto : "");
}
