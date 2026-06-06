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

struct __attribute__((packed)) DiscoveryV3Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t pair_nonce[4];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
};

struct __attribute__((packed)) PairChallengeV3Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t pair_nonce[4];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
    char hostname[ESPNOW_HOSTNAME_SIZE];
};

struct __attribute__((packed)) PairConfirmV3Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t pair_nonce[4];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) PairResultV3Pkt {
    uint8_t type;
    uint8_t version;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t pair_nonce[4];
    char hostname[ESPNOW_HOSTNAME_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};


void ESPNowModule::init() {
    static bool commands_registered = false;
    if (!commands_registered) {
        commands_registered = true;
        new UserCommand("EP", "ESPNow/Pair", espnowPairCommand, anyState, WA);
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
    _instance = this;
    if (!_peer_mutex) {
        log_error("ESP-NOW: failed to create peer mutex");
    }
}

void ESPNowChannel::init() {
    if (_initialized) {
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
        return false;
    }
    return true;
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
    int index = findPairedPeerIndex(mac);
    if (index < 0) {
        return false;
    }

    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    esp_now_del_peer(_paired_peers[index].mac);
    ESPNowCrypto::secureZero(_paired_peers[index].lmk, sizeof(_paired_peers[index].lmk));

    for (size_t i = (size_t)index; i + 1 < paired_count; ++i) {
        memcpy(_paired_peers[i].mac, _paired_peers[i + 1].mac, sizeof(_paired_peers[i].mac));
        memcpy(_paired_peers[i].lmk, _paired_peers[i + 1].lmk, sizeof(_paired_peers[i].lmk));
        resetPeerRuntime((int)i);
    }

    size_t last = paired_count - 1;
    memset(_paired_peers[last].mac, 0, sizeof(_paired_peers[last].mac));
    ESPNowCrypto::secureZero(_paired_peers[last].lmk, sizeof(_paired_peers[last].lmk));
    resetPeerRuntime((int)last);

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
    PeerStateLock peer_lock(_peer_mutex);
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        esp_now_del_peer(_paired_peers[i].mac);
        memset(_paired_peers[i].mac, 0, sizeof(_paired_peers[i].mac));
        ESPNowCrypto::secureZero(_paired_peers[i].lmk, sizeof(_paired_peers[i].lmk));
        resetPeerRuntime((int)i);
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
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (index < 0 || index >= (int)paired_count) {
        return false;
    }
    const auto& peer = _paired_peers[index];
    if (!peer.session_authenticated.load(std::memory_order_acquire)) {
        return false;
    }
    uint32_t last_rx_ms = peer.last_rx_ms.load(std::memory_order_acquire);
    if (last_rx_ms == 0) {
        return false;
    }
    return (uint32_t)(millis() - last_rx_ms) <= PENDANT_IDLE_TIMEOUT_MS;
}

void ESPNowChannel::refreshReportInterval() {
    setReportInterval(ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS);
}

void ESPNowChannel::resetPeerRuntime(int index) {
    PeerStateLock peer_lock(_peer_mutex);
    if (index < 0 || index >= (int)MAX_SERVER_PAIRINGS) {
        return;
    }

    auto& peer = _paired_peers[index];
    peer.tx_peer_known.store(false, std::memory_order_release);
    peer.session_authenticated.store(false, std::memory_order_release);
    peer.tx_peer_nonce.store(0, std::memory_order_release);
    peer.tx_counter.store(0, std::memory_order_release);
    peer.last_rx_ms.store(0, std::memory_order_release);
    peer.last_control_ms.store(0, std::memory_order_release);
    peer.echo_pending.store(false, std::memory_order_release);
    ESPNowCrypto::issueRxChallenge(peer.rx_nonce);
    peer.rx_replay = {};
    memset(&peer.frag, 0, sizeof(peer.frag));
    peer.tx_seq = 0;
    peer.was_connected = false;
}

void ESPNowChannel::notePeerAuthenticated(int index, bool control_activity, TickType_t timeout) {
    PeerStateLock peer_lock(_peer_mutex, timeout);
    if (!peer_lock.locked()) {
        return;
    }
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (index < 0 || index >= (int)paired_count) {
        return;
    }

    auto& peer = _paired_peers[index];
    bool was_authenticated = peer.session_authenticated.load(std::memory_order_acquire);
    uint32_t now = (uint32_t)millis();

    peer.session_authenticated.store(true, std::memory_order_release);
    peer.last_rx_ms.store(now, std::memory_order_release);
    if (control_activity) {
        peer.last_control_ms.store(now, std::memory_order_release);
    }
    if (!was_authenticated) {
        refreshReportInterval();
    }
}

bool ESPNowChannel::setActivePeer(int index) {
    PeerStateLock peer_lock(_peer_mutex);
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

bool ESPNowChannel::canSwitchActivePeer() const {
    PeerStateLock peer_lock(_peer_mutex);
    int active_peer_index = _active_peer_index.load(std::memory_order_acquire);
    if (active_peer_index < 0) {
        return true;
    }
    if (!peerConnected(active_peer_index)) {
        return true;
    }
    uint32_t last = _paired_peers[active_peer_index].last_control_ms.load(std::memory_order_acquire);
    return last == 0 || (uint32_t)(millis() - last) > PENDANT_IDLE_TIMEOUT_MS;
}

bool ESPNowChannel::claimControlLease(int index) {
    PeerStateLock peer_lock(_peer_mutex);
    if (index == _active_peer_index.load(std::memory_order_acquire)) {
        return true;
    }
    if (!canSwitchActivePeer()) {
        return false;
    }
    return setActivePeer(index);
}

void ESPNowChannel::clearPendingPairing(PendingPairing& pending) {
    ESPNowCrypto::secureZero(pending.private_key, sizeof(pending.private_key));
    ESPNowCrypto::secureZero(pending.peer_pubkey, sizeof(pending.peer_pubkey));
    ESPNowCrypto::secureZero(pending.public_key, sizeof(pending.public_key));
    memset(&pending, 0, sizeof(pending));
}

bool ESPNowChannel::findPendingPairing(const uint8_t* mac, uint32_t pair_nonce, PendingPairing** out_pending) {
    if (!mac || !out_pending) {
        return false;
    }

    PeerStateLock peer_lock(_peer_mutex);
    uint32_t now = (uint32_t)millis();
    for (auto& pending : _pending_pairings) {
        if (!pending.active) {
            continue;
        }
        if ((uint32_t)(now - pending.last_ms) > PAIRING_HANDSHAKE_TIMEOUT_MS) {
            clearPendingPairing(pending);
            continue;
        }
        if (pending.pair_nonce == pair_nonce && mac_eq(pending.mac, mac)) {
            *out_pending = &pending;
            return true;
        }
    }
    return false;
}

bool ESPNowChannel::reservePendingPairing(const uint8_t* mac, PendingPairing** out_pending) {
    if (!mac || !out_pending) {
        return false;
    }

    PeerStateLock peer_lock(_peer_mutex);
    uint32_t now = (uint32_t)millis();
    PendingPairing* oldest = &_pending_pairings[0];

    for (auto& pending : _pending_pairings) {
        if (pending.active && (uint32_t)(now - pending.last_ms) > PAIRING_HANDSHAKE_TIMEOUT_MS) {
            clearPendingPairing(pending);
        }
        if (pending.active && mac_eq(pending.mac, mac)) {
            clearPendingPairing(pending);
            *out_pending = &pending;
            return true;
        }
        if (!pending.active) {
            *out_pending = &pending;
            return true;
        }
        if ((int32_t)(pending.last_ms - oldest->last_ms) < 0) {
            oldest = &pending;
        }
    }

    clearPendingPairing(*oldest);
    *out_pending = oldest;
    return true;
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

bool ESPNowChannel::completePairing(const uint8_t* pendant_mac, const uint8_t* lmk, const uint8_t* ack, size_t ack_len) {
    if (!pendant_mac || !lmk || !ack || ack_len == 0) {
        return false;
    }

    esp_now_del_peer(pendant_mac);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, pendant_mac, 6);
    peer.encrypt = false;
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        log_error("ESP-NOW: failed to add pendant peer");
        return false;
    }

    int  paired_index = findPairedPeerIndex(pendant_mac);
    bool new_peer = paired_index < 0;
    if (new_peer) {
        size_t paired_count = _paired_count.load(std::memory_order_acquire);
        if (paired_count >= MAX_SERVER_PAIRINGS) {
            esp_now_del_peer(pendant_mac);
            log_error("ESP-NOW: pairing roster full, rejecting new pendant " << mac_str(pendant_mac));
            return false;
        }
        paired_index = (int)paired_count;
    } else if (peerConnected(paired_index)) {
        esp_now_del_peer(pendant_mac);
        log_error("ESP-NOW: rejecting re-pair attempt from active pendant " << mac_str(pendant_mac));
        return false;
    }

    memcpy(_paired_peers[paired_index].mac, pendant_mac, 6);
    memcpy(_paired_peers[paired_index].lmk, lmk, 16);
    resetPeerRuntime(paired_index);

    esp_now_send(pendant_mac, ack, ack_len);
    if (!registerPeer(pendant_mac, lmk)) {
        esp_now_del_peer(pendant_mac);
        resetPeerRuntime(paired_index);
        if (new_peer) {
            memset(_paired_peers[paired_index].mac, 0, sizeof(_paired_peers[paired_index].mac));
            memset(_paired_peers[paired_index].lmk, 0, sizeof(_paired_peers[paired_index].lmk));
        }
        log_error("ESP-NOW: failed to enable encrypted peer for " << mac_str(pendant_mac));
        return false;
    }
    if (!ESPNowConfig::savePairing(pendant_mac, lmk)) {
        log_error("ESP-NOW: failed to persist pairing for " << mac_str(pendant_mac));
    }
    if (new_peer) {
        _paired_count.store((size_t)paired_index + 1, std::memory_order_release);
    }
    _paired.store(true, std::memory_order_release);
    if (_active_peer_index.load(std::memory_order_acquire) < 0) {
        setActivePeer(paired_index);
    }
    refreshReportInterval();
    return true;
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

    _tx_buf.append(reinterpret_cast<const char*>(buf), size);

    if ((!_tx_buf.empty() && _tx_buf.back() == '\n') || _tx_buf.size() >= 1024) {
        sendFragmented(reinterpret_cast<const uint8_t*>(_tx_buf.c_str()), _tx_buf.size());
        _tx_buf.clear();
    }

    return size;
}

void ESPNowChannel::sendFragmented(const uint8_t* data, size_t len) {
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        if (!peerConnected((int)i)) {
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
    if (_discovery_pending.load(std::memory_order_acquire)) {
        _discovery_pending.store(false, std::memory_order_release);
        handleDiscovery(_discovery_src, _discovery_buf, _discovery_len);
    }

    if (_pair_confirm_pending.load(std::memory_order_acquire)) {
        _pair_confirm_pending.store(false, std::memory_order_release);
        handlePairConfirm(_pair_confirm_src, _pair_confirm_buf, _pair_confirm_len);
    }

    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        auto& peer = _paired_peers[i];
        bool connected = peerConnected((int)i);

        if (peer.was_connected && !connected) {
            resetPeerRuntime((int)i);
            if (_active_peer_index.load(std::memory_order_acquire) == (int)i) {
                _active_peer_index.store(-1, std::memory_order_release);
            }
            refreshReportInterval();
        }
        peer.was_connected = connected;

        if (!peer.echo_pending.exchange(false, std::memory_order_acq_rel)) {
            continue;
        }

        uint32_t advertised = peer.rx_nonce.load(std::memory_order_acquire);
        if (!peer.tx_peer_known.load(std::memory_order_acquire)) {
            uint8_t reply[5];
            reply[0] = PKT_KEEPALIVE;
            memcpy(reply + 1, &advertised, 4);
            esp_now_send(peer.mac, reply, sizeof(reply));
        } else {
            uint8_t reply[1 + 4 + ART_TAG_SIZE];
            reply[0] = PKT_KEEPALIVE;
            memcpy(reply + 1, &advertised, 4);
            if (ESPNowCrypto::stampAntiReplayTag(peer.tx_peer_known, peer.tx_peer_nonce, peer.tx_counter, reply + 5)) {
                esp_now_send(peer.mac, reply, sizeof(reply));
            }
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

#if ESP_IDF_VERSION_MAJOR >= 5
void ESPNowChannel::onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    const uint8_t* src = info ? info->src_addr : nullptr;
#else
void ESPNowChannel::onRecv(const uint8_t* src, const uint8_t* data, int len) {
#endif
    if (!_instance || len < 1) {
        return;
    }
    uint8_t pkt_type = data[0];

    int  paired_index = _instance->findPairedPeerIndex(src, 0);
    bool from_peer    = paired_index >= 0;

    switch (pkt_type) {
        case PKT_DISCOVERY:
            if (!_instance->_discovery_pending.load(std::memory_order_acquire) &&
                len == (int)sizeof(DiscoveryV3Pkt) && src) {
                memcpy(_instance->_discovery_buf, data, len);
                memcpy(_instance->_discovery_src, src, 6);
                _instance->_discovery_len = len;
                _instance->_discovery_pending.store(true, std::memory_order_release);
            }
            break;
        case PKT_PAIR_CONFIRM:
            if (!_instance->_pair_confirm_pending.load(std::memory_order_acquire) &&
                len == (int)sizeof(PairConfirmV3Pkt) && src) {
                memcpy(_instance->_pair_confirm_buf, data, len);
                memcpy(_instance->_pair_confirm_src, src, 6);
                _instance->_pair_confirm_len = len;
                _instance->_pair_confirm_pending.store(true, std::memory_order_release);
            }
            break;
        case PKT_DATA:
            if (_instance->_paired.load(std::memory_order_acquire) && from_peer) {
                _instance->handleData(paired_index, data, len);
            }
            break;
        case PKT_REALTIME:
            if (_instance->_paired.load(std::memory_order_acquire) && from_peer && len >= 1 + ART_TAG_SIZE + 1) {
                _instance->handleRealtime(paired_index, data, len);
            }
            break;
        case PKT_KEEPALIVE:
            if (_instance->_paired.load(std::memory_order_acquire) && from_peer) {
                PeerStateLock peer_lock(_instance->_peer_mutex, 0);
                if (!peer_lock.locked()) {
                    break;
                }
                size_t paired_count = _instance->_paired_count.load(std::memory_order_acquire);
                if (paired_index < 0 || paired_index >= (int)paired_count) {
                    break;
                }
                auto& peer = _instance->_paired_peers[paired_index];
                bool accepted = false;
                bool authenticated = false;
                if (len >= 1 + 4 + ART_TAG_SIZE) {
                    uint32_t advertised, nonce, counter;
                    memcpy(&advertised, data + 1, 4);
                    memcpy(&nonce,      data + 5, 4);
                    memcpy(&counter,    data + 9, 4);
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
                } else if (len >= 5 &&
                           (!peer.tx_peer_known.load(std::memory_order_acquire) ||
                            !peer.session_authenticated.load(std::memory_order_acquire))) {
                    uint32_t advertised;
                    memcpy(&advertised, data + 1, 4);
                    if (advertised != 0) {
                        peer.tx_peer_nonce.store(advertised, std::memory_order_release);
                        peer.tx_peer_known.store(true, std::memory_order_release);
                        accepted = true;
                    }
                }

                if (accepted) {
                    if (authenticated) {
                        _instance->notePeerAuthenticated(paired_index, false, 0);
                    }
                    peer.echo_pending.store(true, std::memory_order_release);
                }
            }
            break;
        default:
            break;
    }
}


void ESPNowChannel::handleDiscovery(const uint8_t* src_mac, const uint8_t* data, int len) {
    if (!pairingWindowActive()) {
        return;
    }
    if (len != (int)sizeof(DiscoveryV3Pkt) || !src_mac) {
        return;
    }

    DiscoveryV3Pkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.version != PAIRING_PROTO_V3 || pkt.mode != PAIRING_MODE_PAIR || !mac_eq(src_mac, pkt.mac)) {
        log_info("ESP-NOW: DISCOVERY v3 src/payload mismatch");
        return;
    }

    uint32_t pair_nonce;
    memcpy(&pair_nonce, pkt.pair_nonce, 4);
    if (pair_nonce == 0 || pkt.pubkey[0] != 0x04) {
        return;
    }

    PendingPairing* pending = nullptr;
    if (!reservePendingPairing(pkt.mac, &pending) || !pending) {
        return;
    }

    uint8_t our_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, our_mac);
    PairChallengeV3Pkt challenge = {};
    challenge.type = PKT_PAIR_ACK;
    challenge.version = PAIRING_PROTO_V3;
    challenge.mode = PAIRING_MODE_PAIR;
    memcpy(challenge.mac, our_mac, sizeof(challenge.mac));
    challenge.channel = (uint8_t)WiFi.channel();
    memcpy(challenge.pair_nonce, pkt.pair_nonce, sizeof(challenge.pair_nonce));
    copyHostname(challenge.hostname);

    if (!ESPNowCrypto::generateEcdhKeypair(pending->private_key, challenge.pubkey)) {
        clearPendingPairing(*pending);
        return;
    }

    memcpy(pending->mac, pkt.mac, sizeof(pending->mac));
    memcpy(pending->peer_pubkey, pkt.pubkey, sizeof(pending->peer_pubkey));
    memcpy(pending->public_key, challenge.pubkey, sizeof(pending->public_key));
    pending->channel = pkt.channel;
    pending->pair_nonce = pair_nonce;
    pending->last_ms = (uint32_t)millis();
    pending->active = true;

    esp_now_del_peer(pkt.mac);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, pkt.mac, 6);
    peer.encrypt = false;
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    if (esp_now_add_peer(&peer) == ESP_OK) {
        esp_now_send(pkt.mac, reinterpret_cast<const uint8_t*>(&challenge), sizeof(challenge));
        log_info("ESP-NOW: discovery v3 challenge sent to " << mac_str(pkt.mac));
    } else {
        clearPendingPairing(*pending);
        log_error("ESP-NOW: failed to add pendant peer for pairing challenge");
    }

    ESPNowCrypto::secureZero(&challenge, sizeof(challenge));
}

void ESPNowChannel::handlePairConfirm(const uint8_t* src_mac, const uint8_t* data, int len) {
    if (!pairingWindowActive()) {
        return;
    }
    if (len != (int)sizeof(PairConfirmV3Pkt) || !src_mac) {
        return;
    }

    PairConfirmV3Pkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.version != PAIRING_PROTO_V3 || pkt.mode != PAIRING_MODE_PAIR || !mac_eq(src_mac, pkt.mac)) {
        return;
    }

    uint32_t pair_nonce;
    memcpy(&pair_nonce, pkt.pair_nonce, 4);
    PendingPairing* pending = nullptr;
    if (!findPendingPairing(pkt.mac, pair_nonce, &pending) || !pending) {
        return;
    }

    uint8_t shared_secret[ESPNowCrypto::ECDH_SHARED_SECRET_SIZE];
    uint8_t window_lmk[16];
    uint8_t final_lmk[16];
    bool paired = false;
    memset(shared_secret, 0, sizeof(shared_secret));
    memset(window_lmk, 0, sizeof(window_lmk));
    memset(final_lmk, 0, sizeof(final_lmk));

    if (ESPNowCrypto::deriveEcdhSharedSecret(pending->private_key, pending->peer_pubkey, shared_secret)) {
        DiscoveryV3Pkt discovery = {};
        discovery.type = PKT_DISCOVERY;
        discovery.version = PAIRING_PROTO_V3;
        discovery.mode = PAIRING_MODE_PAIR;
        memcpy(discovery.mac, pending->mac, sizeof(discovery.mac));
        discovery.channel = pending->channel;
        memcpy(discovery.pair_nonce, pkt.pair_nonce, sizeof(discovery.pair_nonce));
        memcpy(discovery.pubkey, pending->peer_pubkey, sizeof(discovery.pubkey));

        uint8_t our_mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, our_mac);
        PairChallengeV3Pkt challenge = {};
        challenge.type = PKT_PAIR_ACK;
        challenge.version = PAIRING_PROTO_V3;
        challenge.mode = PAIRING_MODE_PAIR;
        memcpy(challenge.mac, our_mac, sizeof(challenge.mac));
        challenge.channel = (uint8_t)WiFi.channel();
        memcpy(challenge.pair_nonce, pkt.pair_nonce, sizeof(challenge.pair_nonce));
        memcpy(challenge.pubkey, pending->public_key, sizeof(challenge.pubkey));
        copyHostname(challenge.hostname);

        PairResultV3Pkt result = {};
        result.type = PKT_PAIR_ACK;
        result.version = PAIRING_PROTO_V3;
        result.mode = PAIRING_MODE_PAIR;
        memcpy(result.mac, our_mac, sizeof(result.mac));
        result.channel = challenge.channel;
        memcpy(result.pair_nonce, pkt.pair_nonce, sizeof(result.pair_nonce));
        copyHostname(result.hostname);

        ESPNowCrypto::derivePairingWindowLmk(window_lmk);
        ESPNowCrypto::derivePairingSessionLmk(window_lmk,
                                              shared_secret,
                                              reinterpret_cast<const uint8_t*>(&discovery),
                                              sizeof(discovery),
                                              reinterpret_cast<const uint8_t*>(&challenge),
                                              sizeof(challenge),
                                              final_lmk);
        if (ESPNowCrypto::verifyPairingAuthTag(final_lmk,
                                               reinterpret_cast<const uint8_t*>(&pkt),
                                               offsetof(PairConfirmV3Pkt, auth_tag),
                                               pkt.auth_tag)) {

            ESPNowCrypto::pairingAuthTag(final_lmk,
                                         reinterpret_cast<const uint8_t*>(&result),
                                         offsetof(PairResultV3Pkt, auth_tag),
                                         result.auth_tag);
            paired = completePairing(pkt.mac,
                                     final_lmk,
                                     reinterpret_cast<const uint8_t*>(&result),
                                     sizeof(result));
            if (paired) {
                _pairing_window_active.store(false, std::memory_order_release);
                log_info("ESP-NOW: pairing v3 confirmed from " << mac_str(pkt.mac));
            }
        }

        ESPNowCrypto::secureZero(&discovery, sizeof(discovery));
        ESPNowCrypto::secureZero(&challenge, sizeof(challenge));
        ESPNowCrypto::secureZero(&result, sizeof(result));
    }

    if (!paired) {
        log_info("ESP-NOW: pairing v3 confirmation failed from " << mac_str(pkt.mac));
    }

    ESPNowCrypto::secureZero(shared_secret, sizeof(shared_secret));
    ESPNowCrypto::secureZero(window_lmk, sizeof(window_lmk));
    ESPNowCrypto::secureZero(final_lmk, sizeof(final_lmk));
    clearPendingPairing(*pending);
}

void ESPNowChannel::handleData(int peer_index, const uint8_t* data, int len) {
    if (len < FRAG_HDR_SIZE) {
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
    if (!claimControlLease(peer_index)) {
        return;
    }
    notePeerAuthenticated(peer_index, true, 0);

    uint8_t seq         = data[9];
    uint8_t frag_idx    = data[10];
    uint8_t total_frags = data[11];
    size_t  payload_len = (size_t)(len - FRAG_HDR_SIZE);

    if (total_frags == 0 || total_frags > MAX_FRAGS || frag_idx >= total_frags) {
        return;
    }
    if (payload_len > MAX_FRAG_DATA) {
        payload_len = MAX_FRAG_DATA;
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
    if (!claimControlLease(peer_index)) {
        return;
    }
    notePeerAuthenticated(peer_index, true, 0);
    rxPush(data[1 + ART_TAG_SIZE]);
}

void ESPNowChannel::onSent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {}
