// Copyright (c) 2026 - Figamore

#include "ESPNowChannel.h"
#include "ESPNowCrypto.h"
#include "../Module.h"
#include "../Serial.h"
#include "../Logging.h"
#include "../Settings.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include <Arduino.h>
#include <stddef.h>
#include <stdlib.h>

static constexpr uint32_t FRAG_REASSEMBLY_TIMEOUT_MS = 3000;
static constexpr uint32_t PENDANT_IDLE_TIMEOUT_MS = 10000;
static constexpr uint32_t PAIRING_WINDOW_MS = 60000;
static constexpr uint32_t PAIRING_PACKET_INTERVAL_MS = 100;
static constexpr uint32_t PAIRING_RESULT_RETRY_MS = 300;
static constexpr uint32_t SEND_CALLBACK_TIMEOUT_MS = 1000;
static constexpr size_t AUTH_KEEPALIVE_SIZE = 1 + 4 + ART_TAG_SIZE + 1;
static constexpr uint8_t KEEPALIVE_SESSION_CONFIRMED = 0x01;
static constexpr UBaseType_t RX_PACKET_QUEUE_DEPTH = 16;
static constexpr UBaseType_t PAIRING_PACKET_QUEUE_DEPTH = 4;
static constexpr UBaseType_t PAIR_CONFIRM_QUEUE_DEPTH = 2;
static constexpr size_t MAX_RX_PACKETS_PER_POLL = 16;
static constexpr size_t TX_FLUSH_THRESHOLD = 1024;

static const char* mac_str(const uint8_t* mac);

namespace {
class PeerStateLock {
public:
    explicit PeerStateLock(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY) : _mutex(mutex) {
        _locked = !_mutex || xSemaphoreTakeRecursive(_mutex, timeout) == pdTRUE;
    }

    ~PeerStateLock() {
        if (_locked && _mutex) {
            xSemaphoreGiveRecursive(_mutex);
        }
    }

    bool locked() const { return _locked; }

private:
    SemaphoreHandle_t _mutex;
    bool              _locked = false;
};

void copyHostname(char out[ESPNOW_HOSTNAME_SIZE]) {
    const char* hostname = WiFi.getHostname();
    if (!hostname || !*hostname) {
        hostname = "fluidnc";
    }
    strlcpy(out, hostname, ESPNOW_HOSTNAME_SIZE);
}

static Error espnowPairCommand(const char* value, AuthenticationLevel, Channel& out) {
    if (value && *value) {
        return Error::InvalidValue;
    }
    if (!espnowChannel.startPairingWindow(PAIRING_WINDOW_MS)) {
        log_error_to(out, "ESP-NOW pairing is not available");
        return Error::InvalidValue;
    }
    log_info_to(out, "ESP-NOW pairing enabled for " << (PAIRING_WINDOW_MS / 1000) << " seconds");
    return Error::Ok;
}

static Error espnowCancelCommand(const char* value, AuthenticationLevel, Channel& out) {
    if (value && *value) {
        return Error::InvalidValue;
    }
    espnowChannel.cancelPairingWindow();
    log_info_to(out, "ESP-NOW pairing cancelled");
    return Error::Ok;
}

static Error espnowListCommand(const char* value, AuthenticationLevel, Channel& out) {
    if (value && *value) {
        return Error::InvalidValue;
    }
    espnowChannel.listPairings(out);
    return Error::Ok;
}

static Error espnowUnpairCommand(const char* value, AuthenticationLevel, Channel& out) {
    if (!value || !*value) {
        espnowChannel.listPairings(out);
        return Error::Ok;
    }

    int parsed = atoi(value);
    if (parsed < 0 || parsed > (int)ESPNowConfig::MAX_PAIRINGS) {
        return Error::NumberRange;
    }
    if (parsed == 0) {
        espnowChannel.clearPairings();
        log_info_to(out, "ESP-NOW whitelist cleared");
        return Error::Ok;
    }

    uint8_t removed_mac[6] = {};
    if (!espnowChannel.removePairingIndex((size_t)parsed, removed_mac)) {
        return Error::InvalidValue;
    }
    log_info_to(out, "ESP-NOW removed pendant " << mac_str(removed_mac));
    return Error::Ok;
}
}

class ESPNowModule : public Module {
public:
    ESPNowModule(const char* name) : Module(name) {}
    void init() override;
    void poll() override;
};

static ModuleFactory::InstanceBuilder<ESPNowModule> espnow_module __attribute__((init_priority(110)))("espnow", true);


ESPNowChannel* ESPNowChannel::_instance  = nullptr;
uint8_t        ESPNowChannel::_rx_buf[ESPNowChannel::RX_BUF_SIZE];
std::atomic<int> ESPNowChannel::_rx_head {0};
std::atomic<int> ESPNowChannel::_rx_tail {0};

ESPNowChannel espnowChannel;


static const char* mac_str(const uint8_t* mac) {
    static char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

static bool mac_eq(const uint8_t* a, const uint8_t* b) {
    return a && b && memcmp(a, b, 6) == 0;
}

struct __attribute__((packed)) DiscoveryV4Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
};

struct __attribute__((packed)) PairChallengeV4Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t dial_channel;
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
    char hostname[ESPNOW_HOSTNAME_SIZE];
};

struct __attribute__((packed)) PairConfirmV4Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) PairResultV4Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    char hostname[ESPNOW_HOSTNAME_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) PairCompleteV4Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t session_id[PAIRING_SESSION_ID_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

static_assert(sizeof(DiscoveryV4Pkt) == 91, "ESP-NOW v4 discovery layout changed");
static_assert(sizeof(PairChallengeV4Pkt) == 124, "ESP-NOW v4 challenge layout changed");
static_assert(sizeof(PairConfirmV4Pkt) == 41, "ESP-NOW v4 confirmation layout changed");
static_assert(sizeof(PairResultV4Pkt) == 74, "ESP-NOW v4 result layout changed");
static_assert(sizeof(PairCompleteV4Pkt) == 41, "ESP-NOW v4 completion layout changed");


void ESPNowModule::init() {
    static bool commands_registered = false;
    if (!commands_registered) {
        commands_registered = true;
        new UserCommand("EP", "ESPNow/Pair", espnowPairCommand, anyState, WA);
        new UserCommand("EC", "ESPNow/Cancel", espnowCancelCommand, anyState, WA);
        new UserCommand("EL", "ESPNow/List", espnowListCommand, anyState, WA);
        new UserCommand("EU", "ESPNow/Unpair", espnowUnpairCommand, anyState, WA);
    }

    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
    }

    espnowChannel.init();
}

void ESPNowModule::poll() {
    espnowChannel.poll();
}


ESPNowChannel::ESPNowChannel() : Channel("espnow") {
    _peer_mutex = xSemaphoreCreateRecursiveMutex();
    _packet_queue = xQueueCreate(RX_PACKET_QUEUE_DEPTH, sizeof(RxPacket));
    _pairing_queue = xQueueCreate(PAIRING_PACKET_QUEUE_DEPTH, sizeof(RxPacket));
    _pair_confirm_queue = xQueueCreate(PAIR_CONFIRM_QUEUE_DEPTH, sizeof(RxPacket));
    _instance = this;
    if (!_peer_mutex) {
        log_error("ESP-NOW: failed to create peer mutex");
    }
    if (!_packet_queue || !_pairing_queue || !_pair_confirm_queue) {
        log_error("ESP-NOW: failed to create receive queues");
    }
}

void ESPNowChannel::init() {
    if (_initialized) {
        return;
    }
    if (!_peer_mutex || !_packet_queue || !_pairing_queue || !_pair_confirm_queue) {
        log_error("ESP-NOW: initialization blocked by missing synchronization resources");
        return;
    }

    if (esp_now_init() != ESP_OK) {
        log_error("ESP-NOW: esp_now_init() failed");
        return;
    }

    uint8_t pmk[16];
    ESPNowCrypto::derivePmk(pmk);
    esp_now_set_pmk(pmk);
    ESPNowCrypto::secureZero(pmk, sizeof(pmk));

    esp_now_register_recv_cb(ESPNowChannel::onRecv);
    esp_now_register_send_cb(ESPNowChannel::onSent);

    ESPNowPairingRecord records[ESPNowConfig::MAX_PAIRINGS];
    size_t saved_count = ESPNowConfig::loadPairings(records, ESPNowConfig::MAX_PAIRINGS);
    _paired_count.store(0, std::memory_order_release);
    for (size_t i = 0; i < saved_count; ++i) {
        size_t paired_count = _paired_count.load(std::memory_order_acquire);
        if (paired_count >= MAX_SERVER_PAIRINGS) {
            log_error("ESP-NOW: saved pairing roster exceeds runtime capacity");
            break;
        }
        size_t new_index = paired_count;
        memcpy(_paired_peers[new_index].mac, records[i].peer_mac, 6);
        memcpy(_paired_peers[new_index].lmk, records[i].lmk, 16);
        resetPeerRuntime((int)new_index);
        if (registerPeer(_paired_peers[new_index].mac, _paired_peers[new_index].lmk)) {
            _paired_count.store(new_index + 1, std::memory_order_release);
            log_info("ESP-NOW: loaded saved pairing, peer " << mac_str(_paired_peers[new_index].mac));
        } else {
            memset(_paired_peers[new_index].mac, 0, sizeof(_paired_peers[new_index].mac));
            memset(_paired_peers[new_index].lmk, 0, sizeof(_paired_peers[new_index].lmk));
            log_error("ESP-NOW: failed to restore saved pairing for " << mac_str(records[i].peer_mac));
        }
    }
    ESPNowCrypto::secureZero(records, sizeof(records));
    _paired.store(_paired_count.load(std::memory_order_acquire) > 0, std::memory_order_release);

    if (!_registered) {
        allChannels.registration(this);
        _registered = true;
    }

    if (_paired.load(std::memory_order_acquire)) {
        setActivePeer(0);
    } else {
        setReportInterval(ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS);
    }

    _initialized = true;
}

bool ESPNowChannel::startPairingWindow(uint32_t window_ms) {
    clearPairingTransaction(true);
    if (_pairing_queue) {
        xQueueReset(_pairing_queue);
    }
    if (_pair_confirm_queue) {
        xQueueReset(_pair_confirm_queue);
    }
    _pairing_window_until_ms.store((uint32_t)millis() + window_ms, std::memory_order_release);
    _pairing_window_active.store(true, std::memory_order_release);
    log_info("ESP-NOW: pairing window opened");
    return true;
}

bool ESPNowChannel::pairingWindowActive() {
    if (!_pairing_window_active.load(std::memory_order_acquire)) {
        return false;
    }
    uint32_t until = _pairing_window_until_ms.load(std::memory_order_acquire);
    if ((int32_t)(until - (uint32_t)millis()) <= 0) {
        _pairing_window_active.store(false, std::memory_order_release);
        clearPairingTransaction(true);
        return false;
    }
    return true;
}

void ESPNowChannel::cancelPairingWindow() {
    _pairing_window_active.store(false, std::memory_order_release);
    _pairing_window_until_ms.store(0, std::memory_order_release);
    clearPairingTransaction(true);
    if (_pairing_queue) {
        xQueueReset(_pairing_queue);
    }
    if (_pair_confirm_queue) {
        xQueueReset(_pair_confirm_queue);
    }
}

bool ESPNowChannel::listPairings(Channel& out) const {
    ESPNowPairingRecord records[ESPNowConfig::MAX_PAIRINGS];
    size_t count = ESPNowConfig::loadPairings(records, ESPNowConfig::MAX_PAIRINGS);
    if (count == 0) {
        log_info_to(out, "ESP-NOW whitelist is empty");
        return true;
    }

    for (size_t i = 0; i < count; ++i) {
        log_info_to(out, (i + 1) << ": " << mac_str(records[i].peer_mac));
    }
    ESPNowCrypto::secureZero(records, sizeof(records));
    return true;
}

bool ESPNowChannel::removeRuntimePeer(const uint8_t* mac) {
    if (!mac) {
        return false;
    }

    PeerStateLock peer_lock(_peer_mutex);
    int index = findPairedPeerIndexLocked(mac);
    if (index < 0) {
        return false;
    }

    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    esp_now_del_peer(_paired_peers[index].mac);
    ESPNowCrypto::secureZero(_paired_peers[index].lmk, sizeof(_paired_peers[index].lmk));

    for (size_t i = (size_t)index; i + 1 < paired_count; ++i) {
        memcpy(_paired_peers[i].mac, _paired_peers[i + 1].mac, sizeof(_paired_peers[i].mac));
        memcpy(_paired_peers[i].lmk, _paired_peers[i + 1].lmk, sizeof(_paired_peers[i].lmk));
        resetPeerRuntimeLocked((int)i);
    }

    size_t last = paired_count - 1;
    memset(_paired_peers[last].mac, 0, sizeof(_paired_peers[last].mac));
    ESPNowCrypto::secureZero(_paired_peers[last].lmk, sizeof(_paired_peers[last].lmk));
    resetPeerRuntimeLocked((int)last);

    _paired_count.store(last, std::memory_order_release);
    _paired.store(last > 0, std::memory_order_release);
    int active = _active_peer_index.load(std::memory_order_acquire);
    if (active == index || active >= (int)last) {
        _active_peer_index.store(last > 0 ? 0 : -1, std::memory_order_release);
    }
    refreshReportInterval();
    return true;
}

bool ESPNowChannel::removePairingIndex(size_t one_based_index, uint8_t removed_mac[6]) {
    if (!ESPNowConfig::removePairingIndex(one_based_index, removed_mac)) {
        return false;
    }
    removeRuntimePeer(removed_mac);
    return true;
}

void ESPNowChannel::clearPairings() {
    clearPairingTransaction(true);
    if (_pairing_queue) {
        xQueueReset(_pairing_queue);
    }
    if (_pair_confirm_queue) {
        xQueueReset(_pair_confirm_queue);
    }

    PeerStateLock peer_lock(_peer_mutex);
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        esp_now_del_peer(_paired_peers[i].mac);
        memset(_paired_peers[i].mac, 0, sizeof(_paired_peers[i].mac));
        ESPNowCrypto::secureZero(_paired_peers[i].lmk, sizeof(_paired_peers[i].lmk));
        resetPeerRuntimeLocked((int)i);
    }
    _paired_count.store(0, std::memory_order_release);
    _paired.store(false, std::memory_order_release);
    _active_peer_index.store(-1, std::memory_order_release);
    _pairing_window_active.store(false, std::memory_order_release);
    ESPNowConfig::clearPairing();
    refreshReportInterval();
}

int ESPNowChannel::findPairedPeerIndex(const uint8_t* mac, TickType_t timeout) const {
    PeerStateLock peer_lock(_peer_mutex, timeout);
    if (!peer_lock.locked() || !mac) {
        return -1;
    }
    return findPairedPeerIndexLocked(mac);
}

int ESPNowChannel::findPairedPeerIndexLocked(const uint8_t* mac) const {
    if (!mac) {
        return -1;
    }
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        if (memcmp(_paired_peers[i].mac, mac, 6) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool ESPNowChannel::peerConnected(int index) const {
    PeerStateLock peer_lock(_peer_mutex);
    return peerConnectedLocked(index, (uint32_t)millis());
}

bool ESPNowChannel::peerConnectedLocked(int index, uint32_t now) const {
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (index < 0 || index >= (int)paired_count) {
        return false;
    }
    const auto& peer = _paired_peers[index];
    if (peer.session_state.load(std::memory_order_acquire) !=
        PeerSessionState::Connected) {
        return false;
    }
    uint32_t last_rx_ms = peer.last_rx_ms.load(std::memory_order_acquire);
    if (last_rx_ms == 0) {
        return false;
    }
    return (uint32_t)(now - last_rx_ms) <= PENDANT_IDLE_TIMEOUT_MS;
}

void ESPNowChannel::refreshReportInterval() {
    setReportInterval(ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS);
}

void ESPNowChannel::resetPeerRuntime(int index) {
    PeerStateLock peer_lock(_peer_mutex);
    resetPeerRuntimeLocked(index);
}

void ESPNowChannel::resetPeerRuntimeLocked(int index) {
    if (index < 0 || index >= (int)MAX_SERVER_PAIRINGS) {
        return;
    }

    auto& peer = _paired_peers[index];
    peer.tx_peer_known.store(false, std::memory_order_release);
    peer.session_state.store(PeerSessionState::Synchronizing, std::memory_order_release);
    peer.tx_peer_nonce.store(0, std::memory_order_release);
    peer.tx_counter.store(0, std::memory_order_release);
    peer.last_rx_ms.store(0, std::memory_order_release);
    peer.last_control_ms.store(0, std::memory_order_release);
    peer.echo_pending.store(false, std::memory_order_release);
    ESPNowCrypto::issueRxChallenge(peer.rx_nonce);
    peer.rx_replay = {};
    memset(&peer.frag, 0, sizeof(peer.frag));
    peer.tx_seq = 0;
}

bool ESPNowChannel::notePeerAuthenticatedLocked(int index, bool control_activity, uint32_t now) {
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (index < 0 || index >= (int)paired_count) {
        return false;
    }

    auto& peer = _paired_peers[index];
    bool became_connected =
        peer.session_state.load(std::memory_order_acquire) !=
        PeerSessionState::Connected;

    peer.session_state.store(PeerSessionState::Connected, std::memory_order_release);
    peer.last_rx_ms.store(now, std::memory_order_release);
    if (control_activity) {
        peer.last_control_ms.store(now, std::memory_order_release);
    }
    return became_connected;
}

bool ESPNowChannel::setActivePeer(int index) {
    PeerStateLock peer_lock(_peer_mutex);
    return setActivePeerLocked(index);
}

bool ESPNowChannel::setActivePeerLocked(int index) {
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (index < 0 || index >= (int)paired_count) {
        return false;
    }
    if (_active_peer_index.load(std::memory_order_acquire) == index) {
        return true;
    }

    _active_peer_index.store(index, std::memory_order_release);
    refreshReportInterval();
    log_info("ESP-NOW: active pendant " << mac_str(_paired_peers[index].mac));
    return true;
}

bool ESPNowChannel::canSwitchActivePeerLocked(uint32_t now) const {
    int active_peer_index = _active_peer_index.load(std::memory_order_acquire);
    if (active_peer_index < 0) {
        return true;
    }
    if (!peerConnectedLocked(active_peer_index, now)) {
        return true;
    }
    uint32_t last = _paired_peers[active_peer_index].last_control_ms.load(std::memory_order_acquire);
    return last == 0 || (uint32_t)(now - last) > PENDANT_IDLE_TIMEOUT_MS;
}

bool ESPNowChannel::claimControlLeaseLocked(int index, uint32_t now) {
    if (index == _active_peer_index.load(std::memory_order_acquire)) {
        return true;
    }
    if (!canSwitchActivePeerLocked(now)) {
        return false;
    }
    return setActivePeerLocked(index);
}

void ESPNowChannel::restorePairedPeerOrDelete(const uint8_t* mac) {
    uint8_t lmk[16] = {};
    bool found = false;
    {
        PeerStateLock peer_lock(_peer_mutex);
        int index = findPairedPeerIndexLocked(mac);
        if (index >= 0) {
            memcpy(lmk, _paired_peers[index].lmk, sizeof(lmk));
            found = true;
        }
    }

    if (found) {
        if (!registerPeer(mac, lmk)) {
            log_error("ESP-NOW: failed to restore encrypted peer " << mac_str(mac));
        }
    } else {
        esp_now_del_peer(mac);
    }
    ESPNowCrypto::secureZero(lmk, sizeof(lmk));
}

void ESPNowChannel::clearPairingTransaction(bool restore_peer) {
    uint8_t mac[6] = {};
    bool had_peer = _pairing.state != PairingState::Idle;
    if (had_peer) {
        memcpy(mac, _pairing.mac, sizeof(mac));
    }
    _pairing_challenge_waiting.store(false, std::memory_order_release);
    _pairing_result_waiting.store(false, std::memory_order_release);
    ESPNowCrypto::secureZero(&_pairing, sizeof(_pairing));
    _pairing.state = PairingState::Idle;
    _pairing_send_done.store(false, std::memory_order_relaxed);
    _pairing_send_ok.store(false, std::memory_order_relaxed);
    if (restore_peer && had_peer) {
        restorePairedPeerOrDelete(mac);
    }
}

bool ESPNowChannel::registerPeer(const uint8_t* mac, const uint8_t* lmk) {
    esp_now_del_peer(mac);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    memcpy(peer.lmk, lmk, 16);
    peer.encrypt  = true;
    peer.channel  = 0;
    peer.ifidx    = WIFI_IF_STA;

    return esp_now_add_peer(&peer) == ESP_OK;
}

bool ESPNowChannel::activatePairing() {
    int paired_index = -1;
    bool new_peer = false;
    bool activation_failed = false;
    {
        PeerStateLock peer_lock(_peer_mutex);
        size_t paired_count = _paired_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < paired_count; ++i) {
            if (memcmp(_paired_peers[i].mac, _pairing.mac, 6) == 0) {
                paired_index = (int)i;
                break;
            }
        }

        new_peer = paired_index < 0;
        if (new_peer) {
            if (paired_count >= MAX_SERVER_PAIRINGS) {
                log_error("ESP-NOW: pairing roster filled before activation");
                activation_failed = true;
            } else {
                paired_index = (int)paired_count;
            }
        }

        if (!activation_failed &&
            !registerPeer(_pairing.mac, _pairing.lmk)) {
            log_error("ESP-NOW: failed to enable encrypted peer for " << mac_str(_pairing.mac));
            activation_failed = true;
        }

        if (!activation_failed) {
            memcpy(_paired_peers[paired_index].mac, _pairing.mac, 6);
            memcpy(_paired_peers[paired_index].lmk, _pairing.lmk, 16);
            resetPeerRuntimeLocked(paired_index);
            if (new_peer) {
                _paired_count.store((size_t)paired_index + 1, std::memory_order_release);
            }
            _paired.store(true, std::memory_order_release);
        }
    }

    if (activation_failed) {
        return false;
    }

    if (!ESPNowConfig::savePairing(_pairing.mac, _pairing.lmk)) {
        log_error("ESP-NOW: failed to persist pairing for " << mac_str(_pairing.mac));
    }
    if (_active_peer_index.load(std::memory_order_acquire) < 0) {
        setActivePeer(paired_index);
    }
    refreshReportInterval();
    log_info("ESP-NOW: pairing activated for " << mac_str(_pairing.mac));
    return true;
}

void ESPNowChannel::processPairingTransaction() {
    if (_pairing.state == PairingState::Idle) {
        return;
    }

    uint32_t now = (uint32_t)millis();
    if (_pairing.state == PairingState::AwaitConfirm) {
        if ((uint32_t)(now - _pairing.last_ms) > PAIRING_HANDSHAKE_TIMEOUT_MS) {
            clearPairingTransaction(true);
        }
        return;
    }

    if (_pairing.state == PairingState::ReadyResult) {
        if (_pairing_challenge_waiting.load(std::memory_order_acquire) &&
            (uint32_t)(now - _pairing.last_ms) <= SEND_CALLBACK_TIMEOUT_MS) {
            return;
        }
        _pairing_challenge_waiting.store(false, std::memory_order_release);
        _pairing.state = PairingState::SendingResult;
        _pairing.send_attempts = 1;
        _pairing.last_ms = now;
        _pairing_send_done.store(false, std::memory_order_relaxed);
        _pairing_send_ok.store(false, std::memory_order_relaxed);
        _pairing_result_waiting.store(true, std::memory_order_release);
        if (esp_now_send(_pairing.mac, _pairing.result, _pairing.result_len) != ESP_OK) {
            _pairing_send_done.store(true, std::memory_order_release);
        }
        return;
    }

    if (_pairing.state == PairingState::AwaitComplete) {
        if ((uint32_t)(now - _pairing.started_ms) > PAIRING_HANDSHAKE_TIMEOUT_MS) {
            clearPairingTransaction(true);
            return;
        }
        if ((uint32_t)(now - _pairing.last_ms) >= PAIRING_RESULT_RETRY_MS) {
            _pairing.last_ms = now;
            esp_now_send(_pairing.mac, _pairing.result, _pairing.result_len);
        }
        return;
    }

    if (!_pairing_send_done.load(std::memory_order_acquire)) {
        if ((uint32_t)(now - _pairing.last_ms) <= SEND_CALLBACK_TIMEOUT_MS) {
            return;
        }
        _pairing_send_ok.store(false, std::memory_order_relaxed);
        _pairing_send_done.store(true, std::memory_order_release);
    }

    if (!_pairing_send_ok.load(std::memory_order_acquire)) {
        if (_pairing.send_attempts < 3) {
            ++_pairing.send_attempts;
            _pairing.last_ms = now;
            _pairing_send_done.store(false, std::memory_order_relaxed);
            _pairing_send_ok.store(false, std::memory_order_relaxed);
            if (esp_now_send(_pairing.mac, _pairing.result, _pairing.result_len) != ESP_OK) {
                _pairing_send_done.store(true, std::memory_order_release);
            }
            return;
        }
        log_error("ESP-NOW: pairing result delivery failed for " << mac_str(_pairing.mac));
        clearPairingTransaction(true);
        return;
    }

    _pairing_result_waiting.store(false, std::memory_order_release);
    _pairing.state = PairingState::AwaitComplete;
    _pairing.last_ms = now;
    log_info("ESP-NOW: pairing result delivered; awaiting pendant completion");
}


size_t ESPNowChannel::write(uint8_t c) {
    return write(&c, 1);
}

size_t ESPNowChannel::write(const uint8_t* buf, size_t size) {
    if (!_paired.load(std::memory_order_acquire) || size == 0) {
        return size;
    }

    bool has_connected_peer = false;
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        if (peerConnected((int)i)) {
            has_connected_peer = true;
            break;
        }
    }
    if (!has_connected_peer) {
        return size;
    }

    size_t offset = 0;
    while (offset < size) {
        size_t room = TX_FLUSH_THRESHOLD - _tx_buf.size();
        size_t remaining = size - offset;
        size_t chunk = room < remaining ? room : remaining;
        _tx_buf.append(reinterpret_cast<const char*>(buf + offset), chunk);
        offset += chunk;

        if ((!_tx_buf.empty() && _tx_buf.back() == '\n') ||
            _tx_buf.size() == TX_FLUSH_THRESHOLD) {
            sendFragmented(reinterpret_cast<const uint8_t*>(_tx_buf.data()), _tx_buf.size());
            _tx_buf.clear();
        }
    }

    return size;
}

void ESPNowChannel::sendFragmented(const uint8_t* data, size_t len) {
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    uint32_t now = (uint32_t)millis();
    for (size_t i = 0; i < paired_count; ++i) {
        PeerStateLock peer_lock(_peer_mutex);
        if (i >= _paired_count.load(std::memory_order_acquire) ||
            !peerConnectedLocked((int)i, now)) {
            continue;
        }
        sendFragmentedToPeer(_paired_peers[i], data, len);
    }
}

void ESPNowChannel::sendFragmentedToPeer(PairedPeer& peer, const uint8_t* data, size_t len) {
    uint8_t total_frags = (uint8_t)((len + MAX_FRAG_DATA - 1) / MAX_FRAG_DATA);
    if (total_frags == 0) {
        total_frags = 1;
    }
    if (total_frags > MAX_FRAGS) {
        log_error("ESP-NOW: outgoing message (" << len << " B) exceeds max fragmented size — truncating");
        total_frags = MAX_FRAGS;
        len         = MAX_FRAGS * MAX_FRAG_DATA;
    }

    uint8_t  seq = peer.tx_seq++;
    uint8_t  pkt[MAX_ESP_PAYLOAD];
    size_t   offset = 0;

    for (uint8_t i = 0; i < total_frags; i++) {
        size_t chunk = len - offset;
        if (chunk > MAX_FRAG_DATA) {
            chunk = MAX_FRAG_DATA;
        }

        pkt[0] = PKT_DATA;
        if (!ESPNowCrypto::stampAntiReplayTag(peer.tx_peer_known, peer.tx_peer_nonce, peer.tx_counter, pkt + 1)) {
            return;
        }
        pkt[9]  = seq;          // 1 + ART_TAG_SIZE
        pkt[10] = i;
        pkt[11] = total_frags;
        memcpy(pkt + FRAG_HDR_SIZE, data + offset, chunk);

        esp_now_send(peer.mac, pkt, FRAG_HDR_SIZE + chunk);
        offset += chunk;
    }
}


void ESPNowChannel::rxPush(uint8_t byte) {
    int cur  = _rx_head.load(std::memory_order_relaxed);
    int next = (cur + 1) % RX_BUF_SIZE;
    if (next == _rx_tail.load(std::memory_order_acquire)) {
        return;  // buffer full — drop
    }
    _rx_buf[cur] = byte;
    _rx_head.store(next, std::memory_order_release);
}

void ESPNowChannel::drainRxBuffer() {
    int head = _rx_head.load(std::memory_order_acquire);
    int tail = _rx_tail.load(std::memory_order_relaxed);
    while (tail != head) {
        uint8_t byte = _rx_buf[tail];
        tail         = (tail + 1) % RX_BUF_SIZE;
        _rx_tail.store(tail, std::memory_order_release);
        Channel::push(byte);
    }
}

void ESPNowChannel::poll() {
    processPairingTransaction();
    uint32_t now = (uint32_t)millis();

    RxPacket packet;
    size_t processed = 0;
    while (_packet_queue &&
           processed < MAX_RX_PACKETS_PER_POLL &&
           xQueueReceive(_packet_queue, &packet, 0) == pdTRUE) {
        processPacket(packet);
        ++processed;
    }

    // A confirmation consumes the ECDH state created by discovery, so it must
    // run before another queued discovery can replace that state.
    while (_pair_confirm_queue &&
           xQueueReceive(_pair_confirm_queue, &packet, 0) == pdTRUE) {
        processPacket(packet);
    }

    // Pairing may perform ECDH. Process @ most 1 pairing packet per pass
    if (_pairing_queue &&
        (uint32_t)(now - _last_pairing_packet_ms) >= PAIRING_PACKET_INTERVAL_MS &&
        xQueueReceive(_pairing_queue, &packet, 0) == pdTRUE) {
        _last_pairing_packet_ms = now;
        processPacket(packet);
    }

    now = (uint32_t)millis();

    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        uint8_t reply[AUTH_KEEPALIVE_SIZE];
        size_t reply_len = 0;
        uint8_t peer_mac[6] = {};
        bool refresh_interval = false;

        {
            PeerStateLock peer_lock(_peer_mutex);
            if (i >= _paired_count.load(std::memory_order_acquire)) {
                break;
            }
            auto& peer = _paired_peers[i];
            bool timed_out =
                peer.session_state.load(std::memory_order_acquire) ==
                    PeerSessionState::Connected &&
                !peerConnectedLocked((int)i, now);

            if (timed_out) {
                resetPeerRuntimeLocked((int)i);
                if (_active_peer_index.load(std::memory_order_acquire) == (int)i) {
                    _active_peer_index.store(-1, std::memory_order_release);
                }
                refresh_interval = true;
            }

            if (peer.echo_pending.exchange(false, std::memory_order_acq_rel)) {
                uint32_t advertised = peer.rx_nonce.load(std::memory_order_acquire);
                memcpy(peer_mac, peer.mac, sizeof(peer_mac));
                reply[0] = PKT_KEEPALIVE;
                memcpy(reply + 1, &advertised, 4);

                if (!peer.tx_peer_known.load(std::memory_order_acquire)) {
                    reply_len = 5;
                } else if (ESPNowCrypto::stampAntiReplayTag(
                               peer.tx_peer_known,
                               peer.tx_peer_nonce,
                               peer.tx_counter,
                               reply + 5)) {
                    reply[1 + 4 + ART_TAG_SIZE] =
                        peer.session_state.load(std::memory_order_acquire) ==
                                PeerSessionState::Connected
                            ? KEEPALIVE_SESSION_CONFIRMED
                            : 0;
                    reply_len = sizeof(reply);
                }
            }
        }

        if (refresh_interval) {
            refreshReportInterval();
        }
        if (reply_len != 0) {
            esp_now_send(peer_mac, reply, reply_len);
        }
    }

    drainRxBuffer();
}

int ESPNowChannel::available() {
    drainRxBuffer();
    return _queue.size();
}

Error ESPNowChannel::pollLine(char* line) {
    drainRxBuffer();
    return Channel::pollLine(line);
}

void ESPNowChannel::processPacket(const RxPacket& packet) {
    if (packet.len == 0 || packet.len > MAX_ESP_PAYLOAD) {
        return;
    }

    uint8_t pkt_type = packet.data[0];
    if (pkt_type == PKT_DISCOVERY) {
        handleDiscovery(packet.src, packet.data, packet.len);
        return;
    }
    if (pkt_type == PKT_PAIR_CONFIRM) {
        handlePairConfirm(packet.src, packet.data, packet.len);
        return;
    }
    if (pkt_type == PKT_PAIR_COMPLETE) {
        handlePairComplete(packet.src, packet.data, packet.len);
        return;
    }

    if (!_paired.load(std::memory_order_acquire)) {
        return;
    }
    int paired_index = findPairedPeerIndex(packet.src);
    if (paired_index < 0) {
        return;
    }

    switch (pkt_type) {
        case PKT_DATA:
            handleData(paired_index, packet.data, packet.len);
            break;
        case PKT_REALTIME:
            handleRealtime(paired_index, packet.data, packet.len);
            break;
        case PKT_KEEPALIVE:
            handleKeepalive(paired_index, packet.data, packet.len);
            break;
        default:
            break;
    }
}

#if ESP_IDF_VERSION_MAJOR >= 5
void ESPNowChannel::onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    const uint8_t* src = info ? info->src_addr : nullptr;
#else
void ESPNowChannel::onRecv(const uint8_t* src, const uint8_t* data, int len) {
#endif
    if (!_instance || !src || !data || len < 1 || len > MAX_ESP_PAYLOAD) {
        return;
    }
    uint8_t pkt_type = data[0];
    QueueHandle_t queue = nullptr;
    switch (pkt_type) {
        case PKT_DISCOVERY:
            if (len == (int)sizeof(DiscoveryV4Pkt) &&
                _instance->_pairing_window_active.load(std::memory_order_acquire)) {
                queue = _instance->_pairing_queue;
            }
            break;
        case PKT_PAIR_CONFIRM:
            if (len == (int)sizeof(PairConfirmV4Pkt) &&
                _instance->_pairing_window_active.load(std::memory_order_acquire)) {
                queue = _instance->_pair_confirm_queue;
            }
            break;
        case PKT_PAIR_COMPLETE:
            if (len == (int)sizeof(PairCompleteV4Pkt) &&
                _instance->_pairing_window_active.load(std::memory_order_acquire)) {
                queue = _instance->_pair_confirm_queue;
            }
            break;
        case PKT_DATA:
            if (len >= FRAG_HDR_SIZE && _instance->_paired.load(std::memory_order_acquire)) {
                queue = _instance->_packet_queue;
            }
            break;
        case PKT_REALTIME:
            if (len == 1 + ART_TAG_SIZE + 1 && _instance->_paired.load(std::memory_order_acquire)) {
                queue = _instance->_packet_queue;
            }
            break;
        case PKT_KEEPALIVE:
            if ((len == 5 || len == (int)AUTH_KEEPALIVE_SIZE) &&
                _instance->_paired.load(std::memory_order_acquire)) {
                queue = _instance->_packet_queue;
            }
            break;
        default:
            break;
    }

    if (!queue) {
        return;
    }

    RxPacket packet;
    memcpy(packet.src, src, sizeof(packet.src));
    packet.len = (uint16_t)len;
    memcpy(packet.data, data, (size_t)len);
    xQueueSend(queue, &packet, 0);
}


void ESPNowChannel::handleDiscovery(const uint8_t* src_mac, const uint8_t* data, int len) {
    if (!pairingWindowActive()) {
        return;
    }
    if (len != (int)sizeof(DiscoveryV4Pkt) || !src_mac) {
        return;
    }

    DiscoveryV4Pkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.type != PKT_DISCOVERY ||
        pkt.version != PAIRING_PROTO_V4 ||
        pkt.mode != PAIRING_MODE_PAIR ||
        !mac_eq(src_mac, pkt.mac)) {
        return;
    }

    static const uint8_t zero_session[PAIRING_SESSION_ID_SIZE] = {};
    if (ESPNowCrypto::constantTimeEquals(
            pkt.session_id, zero_session, sizeof(pkt.session_id)) ||
        pkt.channel < 1 ||
        pkt.channel > 14 ||
        pkt.pubkey[0] != 0x04) {
        return;
    }

    if (_pairing.state != PairingState::Idle) {
        bool same_transaction =
            mac_eq(_pairing.mac, pkt.mac) &&
            ESPNowCrypto::constantTimeEquals(
                _pairing.session_id, pkt.session_id, sizeof(pkt.session_id));
        if (same_transaction && _pairing.state == PairingState::AwaitConfirm) {
            _pairing.last_ms = (uint32_t)millis();
            if (!_pairing_challenge_waiting.load(std::memory_order_acquire)) {
                _pairing_challenge_waiting.store(true, std::memory_order_release);
                if (esp_now_send(_pairing.mac, _pairing.challenge, _pairing.challenge_len) != ESP_OK) {
                    _pairing_challenge_waiting.store(false, std::memory_order_release);
                }
            }
        }
        return;
    }

    {
        PeerStateLock peer_lock(_peer_mutex);
        int existing_index = findPairedPeerIndexLocked(pkt.mac);
        size_t paired_count = _paired_count.load(std::memory_order_acquire);
        if (existing_index < 0 && paired_count >= MAX_SERVER_PAIRINGS) {
            log_error("ESP-NOW: pairing roster full, rejecting new pendant " << mac_str(pkt.mac));
            return;
        }
        if (existing_index >= 0 && peerConnectedLocked(existing_index, (uint32_t)millis())) {
            log_error("ESP-NOW: rejecting re-pair attempt from active pendant " << mac_str(pkt.mac));
            return;
        }
    }

    uint8_t our_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, our_mac);
    PairChallengeV4Pkt challenge = {};
    challenge.type = PKT_PAIR_CHALLENGE;
    challenge.version = PAIRING_PROTO_V4;
    challenge.mode = PAIRING_MODE_PAIR;
    memcpy(challenge.mac, our_mac, sizeof(challenge.mac));
    challenge.channel = (uint8_t)WiFi.channel();
    challenge.dial_channel = pkt.channel;
    memcpy(challenge.session_id, pkt.session_id, sizeof(challenge.session_id));
    copyHostname(challenge.hostname);

    uint8_t private_key[ESPNowCrypto::ECDH_PRIVATE_KEY_SIZE] = {};
    if (challenge.channel < 1 || challenge.channel > 14 ||
        !ESPNowCrypto::generateEcdhKeypair(private_key, challenge.pubkey)) {
        ESPNowCrypto::secureZero(private_key, sizeof(private_key));
        clearPairingTransaction(false);
        return;
    }

    uint8_t shared_secret[ESPNowCrypto::ECDH_SHARED_SECRET_SIZE] = {};
    uint8_t window_lmk[16] = {};
    bool derived =
        ESPNowCrypto::deriveEcdhSharedSecret(
            private_key, pkt.pubkey, shared_secret);
    if (derived) {
        ESPNowCrypto::derivePairingWindowLmk(window_lmk);
        ESPNowCrypto::derivePairingSessionLmk(
            window_lmk,
            shared_secret,
            reinterpret_cast<const uint8_t*>(&pkt),
            sizeof(pkt),
            reinterpret_cast<const uint8_t*>(&challenge),
            sizeof(challenge),
            _pairing.lmk);
    }
    ESPNowCrypto::secureZero(private_key, sizeof(private_key));
    ESPNowCrypto::secureZero(shared_secret, sizeof(shared_secret));
    ESPNowCrypto::secureZero(window_lmk, sizeof(window_lmk));
    if (!derived) {
        clearPairingTransaction(false);
        return;
    }

    memcpy(_pairing.mac, pkt.mac, sizeof(_pairing.mac));
    memcpy(_pairing.session_id, pkt.session_id, sizeof(_pairing.session_id));
    memcpy(_pairing.challenge, &challenge, sizeof(challenge));
    _pairing.challenge_len = sizeof(challenge);
    _pairing.started_ms = (uint32_t)millis();
    _pairing.last_ms = _pairing.started_ms;
    _pairing.state = PairingState::AwaitConfirm;
    memcpy(_pairing_callback_mac, pkt.mac, sizeof(_pairing_callback_mac));

    esp_now_del_peer(pkt.mac);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, pkt.mac, 6);
    peer.encrypt = false;
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    if (esp_now_add_peer(&peer) == ESP_OK) {
        _pairing_challenge_waiting.store(true, std::memory_order_release);
        if (esp_now_send(pkt.mac, reinterpret_cast<const uint8_t*>(&challenge), sizeof(challenge)) == ESP_OK) {
            log_info("ESP-NOW: discovery v4 challenge sent to " << mac_str(pkt.mac));
        } else {
            _pairing_challenge_waiting.store(false, std::memory_order_release);
            clearPairingTransaction(true);
            log_error("ESP-NOW: failed to queue pairing challenge");
        }
    } else {
        clearPairingTransaction(true);
        log_error("ESP-NOW: failed to add pendant peer for pairing challenge");
    }

    ESPNowCrypto::secureZero(&challenge, sizeof(challenge));
}

void ESPNowChannel::handlePairConfirm(const uint8_t* src_mac, const uint8_t* data, int len) {
    if (!pairingWindowActive()) {
        return;
    }
    if (len != (int)sizeof(PairConfirmV4Pkt) || !src_mac) {
        return;
    }

    PairConfirmV4Pkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.type != PKT_PAIR_CONFIRM ||
        pkt.version != PAIRING_PROTO_V4 ||
        pkt.mode != PAIRING_MODE_PAIR ||
        !mac_eq(src_mac, pkt.mac) ||
        !mac_eq(_pairing.mac, pkt.mac) ||
        !ESPNowCrypto::constantTimeEquals(
            _pairing.session_id, pkt.session_id, sizeof(pkt.session_id))) {
        return;
    }

    if (_pairing.state == PairingState::SendingResult) {
        return;
    }
    if (_pairing.state != PairingState::AwaitConfirm) {
        return;
    }

    if (!ESPNowCrypto::verifyPairingAuthTag(
            _pairing.lmk,
            reinterpret_cast<const uint8_t*>(&pkt),
            offsetof(PairConfirmV4Pkt, auth_tag),
            pkt.auth_tag)) {
        log_info("ESP-NOW: pairing v4 confirmation failed from " << mac_str(pkt.mac));
        return;
    }

    uint8_t our_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, our_mac);
    PairResultV4Pkt result = {};
    result.type = PKT_PAIR_RESULT;
    result.version = PAIRING_PROTO_V4;
    result.mode = PAIRING_MODE_PAIR;
    memcpy(result.mac, our_mac, sizeof(result.mac));
    result.channel = (uint8_t)WiFi.channel();
    memcpy(result.session_id, pkt.session_id, sizeof(result.session_id));
    copyHostname(result.hostname);
    ESPNowCrypto::pairingAuthTag(
        _pairing.lmk,
        reinterpret_cast<const uint8_t*>(&result),
        offsetof(PairResultV4Pkt, auth_tag),
        result.auth_tag);

    memcpy(_pairing.result, &result, sizeof(result));
    _pairing.result_len = sizeof(result);
    _pairing.last_ms = (uint32_t)millis();
    _pairing.state = PairingState::ReadyResult;
    ESPNowCrypto::secureZero(&result, sizeof(result));
}

void ESPNowChannel::handlePairComplete(const uint8_t* src_mac, const uint8_t* data, int len) {
    if (!pairingWindowActive() ||
        _pairing.state != PairingState::AwaitComplete ||
        len != (int)sizeof(PairCompleteV4Pkt) ||
        !src_mac) {
        return;
    }

    PairCompleteV4Pkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    bool valid =
        pkt.type == PKT_PAIR_COMPLETE &&
        pkt.version == PAIRING_PROTO_V4 &&
        pkt.mode == PAIRING_MODE_PAIR &&
        mac_eq(src_mac, pkt.mac) &&
        mac_eq(_pairing.mac, pkt.mac) &&
        ESPNowCrypto::constantTimeEquals(
            _pairing.session_id, pkt.session_id, sizeof(pkt.session_id)) &&
        ESPNowCrypto::verifyPairingAuthTag(
            _pairing.lmk,
            reinterpret_cast<const uint8_t*>(&pkt),
            offsetof(PairCompleteV4Pkt, auth_tag),
            pkt.auth_tag);
    ESPNowCrypto::secureZero(&pkt, sizeof(pkt));
    if (!valid) {
        return;
    }

    if (!activatePairing()) {
        clearPairingTransaction(true);
        return;
    }

    _pairing_window_active.store(false, std::memory_order_release);
    log_info("ESP-NOW: pairing v4 confirmed from " << mac_str(_pairing.mac));
    clearPairingTransaction(false);
}

void ESPNowChannel::handleKeepalive(int peer_index, const uint8_t* data, int len) {
    bool became_authenticated = false;
    bool session_reset = false;

    {
        PeerStateLock peer_lock(_peer_mutex);
        size_t paired_count = _paired_count.load(std::memory_order_acquire);
        if (peer_index < 0 || peer_index >= (int)paired_count) {
            return;
        }

        auto& peer = _paired_peers[peer_index];
        bool accepted = false;
        bool authenticated = false;

        if (len == (int)AUTH_KEEPALIVE_SIZE) {
            uint32_t advertised, nonce, counter;
            memcpy(&advertised, data + 1, 4);
            memcpy(&nonce, data + 5, 4);
            memcpy(&counter, data + 9, 4);
            if (advertised != 0 &&
                ESPNowCrypto::acceptReplay(peer.rx_nonce,
                                           peer.rx_replay,
                                           nonce,
                                           counter,
                                           (uint32_t)millis())) {
                peer.tx_peer_nonce.store(advertised, std::memory_order_release);
                peer.tx_peer_known.store(true, std::memory_order_release);
                accepted = true;
                authenticated = true;
            }
        } else if (len == 5) {
            uint32_t advertised;
            memcpy(&advertised, data + 1, 4);
            if (advertised != 0) {
                bool new_session =
                    !peer.tx_peer_known.load(std::memory_order_acquire) ||
                    peer.tx_peer_nonce.load(std::memory_order_acquire) != advertised;
                session_reset =
                    new_session &&
                    peer.session_state.load(std::memory_order_acquire) ==
                    PeerSessionState::Connected;
                peer.tx_peer_nonce.store(advertised, std::memory_order_release);
                peer.tx_peer_known.store(true, std::memory_order_release);
                if (new_session) {
                    peer.session_state.store(
                        PeerSessionState::Synchronizing, std::memory_order_release);
                    peer.tx_counter.store(0, std::memory_order_release);
                    peer.last_rx_ms.store(0, std::memory_order_release);
                    peer.last_control_ms.store(0, std::memory_order_release);
                    ESPNowCrypto::issueRxChallenge(peer.rx_nonce);
                    peer.rx_replay = {};
                }
                accepted = true;
            }
        }

        if (!accepted) {
            return;
        }

        if (authenticated) {
            became_authenticated =
                notePeerAuthenticatedLocked(
                    peer_index, false, (uint32_t)millis());
        }
        peer.echo_pending.store(true, std::memory_order_release);
    }

    if (became_authenticated || session_reset) {
        refreshReportInterval();
    }
}

void ESPNowChannel::handleData(int peer_index, const uint8_t* data, int len) {
    if (len < FRAG_HDR_SIZE) {
        return;
    }

    uint8_t seq         = data[9];
    uint8_t frag_idx    = data[10];
    uint8_t total_frags = data[11];
    size_t  payload_len = (size_t)(len - FRAG_HDR_SIZE);
    if (total_frags == 0 || total_frags > MAX_FRAGS ||
        frag_idx >= total_frags || payload_len > MAX_FRAG_DATA) {
        return;
    }

    PeerStateLock peer_lock(_peer_mutex, 0);
    if (!peer_lock.locked()) {
        return;
    }

    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (peer_index < 0 || peer_index >= (int)paired_count) {
        return;
    }

    auto& peer = _paired_peers[peer_index];

    uint32_t nonce, counter;
    memcpy(&nonce,   data + 1, 4);
    memcpy(&counter, data + 5, 4);
    if (!ESPNowCrypto::acceptReplay(peer.rx_nonce, peer.rx_replay, nonce, counter, (uint32_t)millis())) {
        return;  // replay / stale ->> drop
    }
    uint32_t now = (uint32_t)millis();
    if (!claimControlLeaseLocked(peer_index, now)) {
        return;
    }
    if (notePeerAuthenticatedLocked(peer_index, true, now)) {
        refreshReportInterval();
    }

    // Expire stale reassembly window before accepting a new sequence
    if (peer.frag.active) {
        bool stale_seq     = (peer.frag.seq != seq);
        bool stale_timeout = ((uint32_t)(millis() - peer.frag.start_ms)
                              > FRAG_REASSEMBLY_TIMEOUT_MS);
        if (stale_seq || stale_timeout) {
            memset(&peer.frag, 0, sizeof(peer.frag));
        }
    }

    if (!peer.frag.active) {
        peer.frag.seq      = seq;
        peer.frag.total    = total_frags;
        peer.frag.active   = true;
        peer.frag.start_ms = (uint32_t)millis();
        peer.frag.received = 0;
        memset(peer.frag.sizes, 0, sizeof(peer.frag.sizes));
    }

    if (peer.frag.total != total_frags) {
        return;
    }

    memcpy(peer.frag.data[frag_idx], data + FRAG_HDR_SIZE, payload_len);
    peer.frag.sizes[frag_idx]  = (uint16_t)payload_len;
    peer.frag.received        |= (1u << frag_idx);

    // Check whether all fragments received
    uint8_t full_mask = (uint8_t)((1u << total_frags) - 1u);
    if ((peer.frag.received & full_mask) != full_mask) {
        return;
    }

    // Reassembly complete — push bytes -> ring buffer
    for (uint8_t i = 0; i < total_frags; i++) {
        for (uint16_t j = 0; j < peer.frag.sizes[i]; j++) {
            rxPush(peer.frag.data[i][j]);
        }
    }

    if (peer.frag.sizes[total_frags - 1] == 0 ||
        peer.frag.data[total_frags - 1][peer.frag.sizes[total_frags - 1] - 1] != '\n') {
        rxPush('\n');
    }

    peer.frag.active = false;
}


void ESPNowChannel::handleRealtime(int peer_index, const uint8_t* data, int len) {
    if (len < 1 + ART_TAG_SIZE + 1) {
        return;
    }

    PeerStateLock peer_lock(_peer_mutex, 0);
    if (!peer_lock.locked()) {
        return;
    }

    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (peer_index < 0 || peer_index >= (int)paired_count) {
        return;
    }

    auto& peer = _paired_peers[peer_index];
    uint32_t nonce, counter;
    memcpy(&nonce,   data + 1, 4);
    memcpy(&counter, data + 5, 4);
    if (!ESPNowCrypto::acceptReplay(peer.rx_nonce, peer.rx_replay, nonce, counter, (uint32_t)millis())) {
        return;  // replay / stale ->>> drop
    }
    uint32_t now = (uint32_t)millis();
    if (!claimControlLeaseLocked(peer_index, now)) {
        return;
    }
    if (notePeerAuthenticatedLocked(peer_index, true, now)) {
        refreshReportInterval();
    }
    rxPush(data[1 + ART_TAG_SIZE]);
}

void ESPNowChannel::onSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (!_instance || !mac) {
        return;
    }

    if (_instance->_pairing_challenge_waiting.load(std::memory_order_acquire) &&
        memcmp(mac, _instance->_pairing_callback_mac, 6) == 0) {
        _instance->_pairing_challenge_waiting.store(false, std::memory_order_release);
        return;
    }

    if (_instance->_pairing_result_waiting.load(std::memory_order_acquire) &&
        memcmp(mac, _instance->_pairing_callback_mac, 6) == 0) {
        _instance->_pairing_send_ok.store(
            status == ESP_NOW_SEND_SUCCESS, std::memory_order_relaxed);
        _instance->_pairing_send_done.store(true, std::memory_order_release);
    }
}
