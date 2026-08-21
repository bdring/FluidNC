#include "Driver/SysStats.h"
#ifdef ENABLE_WS_CHANNEL_PINS
#    include "DebugRecovery/DebugRecovery.h"
#    include "WebUI/WebResourcePolicy.h"
#endif
#include <sstream>
#include <iomanip>

#include <Esp.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#ifdef ENABLE_WS_CHANNEL_PINS
extern "C" uint32_t async_web_request_created();
extern "C" uint32_t async_web_request_destroyed();
extern "C" uint32_t async_web_request_owner_allocations();
extern "C" uint32_t async_web_request_owner_deallocations();
extern "C" uint32_t async_web_resource_reject_abort_calls();
extern "C" uint32_t async_web_resource_reject_abort_max_us();
extern "C" uint32_t channel_queue_mutex_created();
extern "C" uint32_t channel_queue_mutex_destroyed();
extern "C" uint32_t async_tcp_client_created();
extern "C" uint32_t async_tcp_client_destroyed();
extern "C" uint32_t async_tcp_event_created();
extern "C" uint32_t async_tcp_event_destroyed();
extern "C" uint32_t async_tcp_event_queue_depth();
extern "C" uint32_t async_tcp_event_queue_high_water();
extern "C" uint32_t async_tcp_rx_timeouts();
extern "C" uint32_t async_tcp_last_rx_timeout_idle_ms();
extern "C" uint32_t async_tcp_accept_event_allocation_failures();
extern "C" uint32_t async_tcp_early_rst_accept_cleanups();
extern "C" uint32_t async_tcp_accept_admission_rejections();
extern "C" uint32_t async_tcp_server_pending_accepts();
extern "C" uint32_t async_tcp_accept_last_observed_free();
extern "C" uint32_t async_tcp_accept_last_largest_block();
extern "C" uint32_t async_tcp_accept_last_effective_free();
extern "C" uint32_t async_tcp_accept_last_live_clients();
extern "C" uint32_t async_tcp_accept_callbacks();
extern "C" uint32_t async_tcp_accept_null_pcbs();
extern "C" int32_t async_tcp_accept_last_null_pcb_error();
extern "C" uint32_t async_tcp_accept_client_allocation_failures();
extern "C" uint32_t async_tcp_accept_client_setup_failures();
extern "C" uint32_t async_tcp_accept_pcb_active_time_wait_peak();
extern "C" void async_tcp_pcb_snapshot(uint32_t* active, uint32_t* time_wait, uint32_t* bound, uint32_t* listening,
                                         uint32_t* listen_backlog, uint32_t* listen_accepts_pending);
extern "C" uint32_t fluidnc_ota_active();
extern "C" uint32_t fluidnc_ota_expected_bytes();
extern "C" uint32_t fluidnc_ota_accepted_bytes();
extern "C" uint32_t fluidnc_ota_max_write_us();
extern "C" uint32_t fluidnc_ota_disconnect_aborts();
extern "C" uint32_t fluidnc_ota_failures();
extern "C" uint32_t fluidnc_ota_update_owned();
#endif

void platform_sys_stats(JSONencoder& j) {
    multi_heap_info_t heapInfo {};
    heap_caps_get_info(&heapInfo, MALLOC_CAP_8BIT);
#ifdef ENABLE_WS_CHANNEL_PINS
    const auto webResources = WebUI::ResourcePolicy::runtime_snapshot();
    uint32_t   tcpActive = 0, tcpTimeWait = 0, tcpBound = 0, tcpListening = 0, tcpListenBacklog = 0, tcpListenPending = 0;
    async_tcp_pcb_snapshot(&tcpActive, &tcpTimeWait, &tcpBound, &tcpListening, &tcpListenBacklog, &tcpListenPending);
#endif
    j.id_value_object("Chip ID", (uint16_t)(ESP.getEfuseMac() >> 32));
    j.id_value_object("CPU Cores", ESP.getChipCores());
    j.id_value_object("CPU Frequency", std::to_string(ESP.getCpuFreqMHz()) + "Mhz");
    j.id_value_object("CPU Temperature", formatFloat(temperatureRead(), 1) + "°C");
    j.id_value_object("Free memory", formatBytes(heapInfo.total_free_bytes));
    j.id_value_object("Largest free block", formatBytes(heapInfo.largest_free_block));
    j.id_value_object("Heap allocated bytes", formatBytes(heapInfo.total_allocated_bytes));
    j.id_value_object("Heap free blocks", heapInfo.free_blocks);
    j.id_value_object("Heap allocated blocks", heapInfo.allocated_blocks);
    j.id_value_object("Heap total blocks", heapInfo.total_blocks);
    j.id_value_object("Heap minimum free", formatBytes(heapInfo.minimum_free_bytes));
#ifdef ENABLE_WS_CHANNEL_PINS
    j.id_value_object("Diagnostic hardening ID", DebugRecovery::diagnostic_hardening_id);
    j.id_value_object("Diagnostic boot sequence", DebugRecovery::current_boot_sequence());
    j.id_value_object("Diagnostic uptime ms", millis());
    j.id_value_object("Diagnostic reset reason", static_cast<uint32_t>(esp_reset_reason()));
    j.id_value_object("Web WS pending", webResources.pendingWebSockets);
    j.id_value_object("Web WS active", webResources.activeWebSockets);
    j.id_value_object("Web WS connecting", webResources.connectingWebSockets);
    j.id_value_object("Web WS deferred closes", webResources.deferredWebSocketCloses);
    j.id_value_object("Web file streams", webResources.activeFileStreams);
    j.id_value_object("Web heavy HTTP active", webResources.activeHeavyHttpResponses);
    j.id_value_object("Web WS limit rejections", webResources.webSocketClientLimitRejections);
    j.id_value_object("Web WS heap rejections", webResources.webSocketHeapRejections);
    j.id_value_object("Web WS recovery admissions", webResources.webSocketRecoveryAdmissions);
    j.id_value_object("Web WS zero idle ms", webResources.webSocketZeroIdleMs);
    j.id_value_object("Web file starts", webResources.fileStreamStarts);
    j.id_value_object("Web file completions", webResources.fileStreamCompletions);
    j.id_value_object("Web file rejections", webResources.fileStreamRejections);
    j.id_value_object("Web heavy HTTP rejections", webResources.heavyHttpRejections);
    j.id_value_object("Web WS last observed free", formatBytes(webResources.lastWebSocketObservedFree));
    j.id_value_object("Web WS last largest block", formatBytes(webResources.lastWebSocketLargestBlock));
    j.id_value_object("Web WS last effective free", formatBytes(webResources.lastWebSocketEffectiveFree));
    j.id_value_object("Web WS last occupied slots", webResources.lastWebSocketOccupiedSlots);
    j.id_value_object("Web requests created", async_web_request_created());
    j.id_value_object("Web requests destroyed", async_web_request_destroyed());
    j.id_value_object("Web request owners allocated", async_web_request_owner_allocations());
    j.id_value_object("Web request owners deallocated", async_web_request_owner_deallocations());
    j.id_value_object("Async WebSocket reject abort calls", async_web_resource_reject_abort_calls());
    j.id_value_object("Async WebSocket reject abort max us", async_web_resource_reject_abort_max_us());
    j.id_value_object("Channel queue mutexes created", channel_queue_mutex_created());
    j.id_value_object("Channel queue mutexes destroyed", channel_queue_mutex_destroyed());
    j.id_value_object("Async TCP clients created", async_tcp_client_created());
    j.id_value_object("Async TCP clients destroyed", async_tcp_client_destroyed());
    j.id_value_object("Async TCP events created", async_tcp_event_created());
    j.id_value_object("Async TCP events destroyed", async_tcp_event_destroyed());
    j.id_value_object("Async TCP event queue depth", async_tcp_event_queue_depth());
    j.id_value_object("Async TCP event queue high water", async_tcp_event_queue_high_water());
    j.id_value_object("Async TCP RX timeouts", async_tcp_rx_timeouts());
    j.id_value_object("Async TCP last RX timeout idle ms", async_tcp_last_rx_timeout_idle_ms());
    j.id_value_object("Async TCP accept event allocation failures", async_tcp_accept_event_allocation_failures());
    j.id_value_object("Async TCP early RST accept cleanups", async_tcp_early_rst_accept_cleanups());
    j.id_value_object("Async TCP accept admission rejections", async_tcp_accept_admission_rejections());
    j.id_value_object("Async TCP server pending accepts", async_tcp_server_pending_accepts());
    j.id_value_object("Async TCP accept last observed free", formatBytes(async_tcp_accept_last_observed_free()));
    j.id_value_object("Async TCP accept last largest block", formatBytes(async_tcp_accept_last_largest_block()));
    j.id_value_object("Async TCP accept last effective free", formatBytes(async_tcp_accept_last_effective_free()));
    j.id_value_object("Async TCP accept last live clients", async_tcp_accept_last_live_clients());
    j.id_value_object("Async TCP accept callbacks", async_tcp_accept_callbacks());
    j.id_value_object("Async TCP accept null PCBs", async_tcp_accept_null_pcbs());
    j.id_value_object("Async TCP accept last null PCB error", async_tcp_accept_last_null_pcb_error());
    j.id_value_object("Async TCP accept client allocation failures", async_tcp_accept_client_allocation_failures());
    j.id_value_object("Async TCP accept client setup failures", async_tcp_accept_client_setup_failures());
    j.id_value_object("TCP accept PCB active+TIME_WAIT peak", async_tcp_accept_pcb_active_time_wait_peak());
    j.id_value_object("Firmware OTA active", fluidnc_ota_active());
    j.id_value_object("Firmware OTA expected bytes", fluidnc_ota_expected_bytes());
    j.id_value_object("Firmware OTA accepted bytes", fluidnc_ota_accepted_bytes());
    j.id_value_object("Firmware OTA max write us", fluidnc_ota_max_write_us());
    j.id_value_object("Firmware OTA disconnect aborts", fluidnc_ota_disconnect_aborts());
    j.id_value_object("Firmware OTA failures", fluidnc_ota_failures());
    j.id_value_object("Firmware OTA updater owned", fluidnc_ota_update_owned());
    j.id_value_object("TCP PCBs active", tcpActive);
    j.id_value_object("TCP PCBs time wait", tcpTimeWait);
    j.id_value_object("TCP PCBs bound", tcpBound);
    j.id_value_object("TCP PCBs listening", tcpListening);
    j.id_value_object("TCP listener backlog", tcpListenBacklog);
    j.id_value_object("TCP listener accepts pending", tcpListenPending);
#endif
    j.id_value_object("SDK", ESP.getSdkVersion());
    j.id_value_object("Flash Size", formatBytes(ESP.getFlashChipSize()));
}

void platform_sys_stats(Channel& out) {
    multi_heap_info_t heapInfo {};
    heap_caps_get_info(&heapInfo, MALLOC_CAP_8BIT);
#ifdef ENABLE_WS_CHANNEL_PINS
    const auto webResources = WebUI::ResourcePolicy::runtime_snapshot();
    uint32_t   tcpActive = 0, tcpTimeWait = 0, tcpBound = 0, tcpListening = 0, tcpListenBacklog = 0, tcpListenPending = 0;
    async_tcp_pcb_snapshot(&tcpActive, &tcpTimeWait, &tcpBound, &tcpListening, &tcpListenBacklog, &tcpListenPending);
#endif
    log_stream(out, "Chip ID: " << (uint16_t)(ESP.getEfuseMac() >> 32));
    log_stream(out, "CPU Cores: " << ESP.getChipCores());
    log_stream(out, "CPU Frequency: " << ESP.getCpuFreqMHz() << "Mhz");
    log_stream(out, "CPU Temperature: " << formatFloat(temperatureRead(), 1) << "°C");
    log_stream(out, "Free memory: " << formatBytes(heapInfo.total_free_bytes));
    log_stream(out, "Largest free block: " << formatBytes(heapInfo.largest_free_block));
    log_stream(out, "Heap allocated bytes: " << formatBytes(heapInfo.total_allocated_bytes));
    log_stream(out, "Heap free blocks: " << heapInfo.free_blocks);
    log_stream(out, "Heap allocated blocks: " << heapInfo.allocated_blocks);
    log_stream(out, "Heap total blocks: " << heapInfo.total_blocks);
    log_stream(out, "Heap minimum free: " << formatBytes(heapInfo.minimum_free_bytes));
#ifdef ENABLE_WS_CHANNEL_PINS
    log_stream(out, "Diagnostic hardening ID: " << DebugRecovery::diagnostic_hardening_id);
    log_stream(out, "Diagnostic boot sequence: " << DebugRecovery::current_boot_sequence());
    log_stream(out, "Diagnostic uptime ms: " << millis());
    log_stream(out, "Diagnostic reset reason: " << static_cast<uint32_t>(esp_reset_reason()));
    log_stream(out, "Web WS pending: " << webResources.pendingWebSockets);
    log_stream(out, "Web WS active: " << webResources.activeWebSockets);
    log_stream(out, "Web WS connecting: " << webResources.connectingWebSockets);
    log_stream(out, "Web WS deferred closes: " << webResources.deferredWebSocketCloses);
    log_stream(out, "Web file streams: " << webResources.activeFileStreams);
    log_stream(out, "Web heavy HTTP active: " << webResources.activeHeavyHttpResponses);
    log_stream(out, "Web WS limit rejections: " << webResources.webSocketClientLimitRejections);
    log_stream(out, "Web WS heap rejections: " << webResources.webSocketHeapRejections);
    log_stream(out, "Web WS recovery admissions: " << webResources.webSocketRecoveryAdmissions);
    log_stream(out, "Web WS zero idle ms: " << webResources.webSocketZeroIdleMs);
    log_stream(out, "Web file starts: " << webResources.fileStreamStarts);
    log_stream(out, "Web file completions: " << webResources.fileStreamCompletions);
    log_stream(out, "Web file rejections: " << webResources.fileStreamRejections);
    log_stream(out, "Web heavy HTTP rejections: " << webResources.heavyHttpRejections);
    log_stream(out, "Web WS last observed free: " << formatBytes(webResources.lastWebSocketObservedFree));
    log_stream(out, "Web WS last largest block: " << formatBytes(webResources.lastWebSocketLargestBlock));
    log_stream(out, "Web WS last effective free: " << formatBytes(webResources.lastWebSocketEffectiveFree));
    log_stream(out, "Web WS last occupied slots: " << webResources.lastWebSocketOccupiedSlots);
    log_stream(out, "Web requests created: " << async_web_request_created());
    log_stream(out, "Web requests destroyed: " << async_web_request_destroyed());
    log_stream(out, "Web request owners allocated: " << async_web_request_owner_allocations());
    log_stream(out, "Web request owners deallocated: " << async_web_request_owner_deallocations());
    log_stream(out, "Async WebSocket reject abort calls: " << async_web_resource_reject_abort_calls());
    log_stream(out, "Async WebSocket reject abort max us: " << async_web_resource_reject_abort_max_us());
    log_stream(out, "Channel queue mutexes created: " << channel_queue_mutex_created());
    log_stream(out, "Channel queue mutexes destroyed: " << channel_queue_mutex_destroyed());
    log_stream(out, "Async TCP clients created: " << async_tcp_client_created());
    log_stream(out, "Async TCP clients destroyed: " << async_tcp_client_destroyed());
    log_stream(out, "Async TCP events created: " << async_tcp_event_created());
    log_stream(out, "Async TCP events destroyed: " << async_tcp_event_destroyed());
    log_stream(out, "Async TCP event queue depth: " << async_tcp_event_queue_depth());
    log_stream(out, "Async TCP event queue high water: " << async_tcp_event_queue_high_water());
    log_stream(out, "Async TCP RX timeouts: " << async_tcp_rx_timeouts());
    log_stream(out, "Async TCP last RX timeout idle ms: " << async_tcp_last_rx_timeout_idle_ms());
    log_stream(out, "Async TCP accept event allocation failures: " << async_tcp_accept_event_allocation_failures());
    log_stream(out, "Async TCP early RST accept cleanups: " << async_tcp_early_rst_accept_cleanups());
    log_stream(out, "Async TCP accept admission rejections: " << async_tcp_accept_admission_rejections());
    log_stream(out, "Async TCP server pending accepts: " << async_tcp_server_pending_accepts());
    log_stream(out, "Async TCP accept last observed free: " << formatBytes(async_tcp_accept_last_observed_free()));
    log_stream(out, "Async TCP accept last largest block: " << formatBytes(async_tcp_accept_last_largest_block()));
    log_stream(out, "Async TCP accept last effective free: " << formatBytes(async_tcp_accept_last_effective_free()));
    log_stream(out, "Async TCP accept last live clients: " << async_tcp_accept_last_live_clients());
    log_stream(out, "Async TCP accept callbacks: " << async_tcp_accept_callbacks());
    log_stream(out, "Async TCP accept null PCBs: " << async_tcp_accept_null_pcbs());
    log_stream(out, "Async TCP accept last null PCB error: " << async_tcp_accept_last_null_pcb_error());
    log_stream(out, "Async TCP accept client allocation failures: " << async_tcp_accept_client_allocation_failures());
    log_stream(out, "Async TCP accept client setup failures: " << async_tcp_accept_client_setup_failures());
    log_stream(out, "TCP accept PCB active+TIME_WAIT peak: " << async_tcp_accept_pcb_active_time_wait_peak());
    log_stream(out, "Firmware OTA active: " << fluidnc_ota_active());
    log_stream(out, "Firmware OTA expected bytes: " << fluidnc_ota_expected_bytes());
    log_stream(out, "Firmware OTA accepted bytes: " << fluidnc_ota_accepted_bytes());
    log_stream(out, "Firmware OTA max write us: " << fluidnc_ota_max_write_us());
    log_stream(out, "Firmware OTA disconnect aborts: " << fluidnc_ota_disconnect_aborts());
    log_stream(out, "Firmware OTA failures: " << fluidnc_ota_failures());
    log_stream(out, "Firmware OTA updater owned: " << fluidnc_ota_update_owned());
    log_stream(out, "TCP PCBs active: " << tcpActive);
    log_stream(out, "TCP PCBs time wait: " << tcpTimeWait);
    log_stream(out, "TCP PCBs bound: " << tcpBound);
    log_stream(out, "TCP PCBs listening: " << tcpListening);
    log_stream(out, "TCP listener backlog: " << tcpListenBacklog);
    log_stream(out, "TCP listener accepts pending: " << tcpListenPending);
#endif
    log_stream(out, "SDK: " << ESP.getSdkVersion());
    log_stream(out, "Flash Size: " << formatBytes(ESP.getFlashChipSize()));
}
