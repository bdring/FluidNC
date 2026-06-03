// Copyright (c) 2026 - Figamore

#include "ESPNowChannel.h"
#include "ESPNowCrypto.h"
#include "../Module.h"
#include "../Machine/MachineConfig.h"
#include "../Serial.h"
#include "../Logging.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include <Arduino.h>
#include <stddef.h>

static constexpr uint32_t FRAG_REASSEMBLY_TIMEOUT_MS = 3000;
static constexpr uint32_t PENDANT_IDLE_TIMEOUT_MS = 10000;

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

struct __attribute__((packed)) DiscoveryPkt {
    uint8_t type;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t pair_nonce[4];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) PairAckPkt {
    uint8_t type;
    uint8_t mac[6];
    uint8_t channel;
    uint8_t pair_nonce[4];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};


void ESPNowModule::init() {
    if (!config) {
        return;
    }

    bool has_espnow = false;
    for (auto* espnow_cfg : config->_espnow) {
        if (espnow_cfg && espnow_cfg->configured()) {
            has_espnow = true;
            break;
        }
    }
    if (!has_espnow) {
        return;
    }

    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
    }

    espnowChannel.init(config->_espnow, ESPNowConfig::MAX_CONFIGS);
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

void ESPNowChannel::init(ESPNowConfig* cfgs[], size_t cfg_count) {
    _cfg_count = (cfg_count > ESPNowConfig::MAX_CONFIGS) ? ESPNowConfig::MAX_CONFIGS : cfg_count;
    for (size_t i = 0; i < _cfg_count; ++i) {
        _cfgs[i] = cfgs[i];
    }

    if (esp_now_init() != ESP_OK) {
        log_error("ESP-NOW: esp_now_init() failed");
        return;
    }

    uint8_t pmk[16];
    ESPNowCrypto::derivePmk(pmk);
    esp_now_set_pmk(pmk);

    esp_now_register_recv_cb(ESPNowChannel::onRecv);
    esp_now_register_send_cb(ESPNowChannel::onSent);

    ESPNowPairingRecord records[ESPNowConfig::MAX_PAIRINGS];
    size_t saved_count = ESPNowConfig::loadPairings(records, ESPNowConfig::MAX_PAIRINGS);
    _paired_count.store(0, std::memory_order_release);
    for (size_t i = 0; i < saved_count; ++i) {
        int cfg_index = findConfigIndexForLmk(records[i].lmk);
        if (cfg_index < 0) {
            log_info("ESP-NOW: skipping saved pairing with no matching espnow config");
            continue;
        }
        size_t paired_count = _paired_count.load(std::memory_order_acquire);
        if (paired_count >= MAX_SERVER_PAIRINGS) {
            log_error("ESP-NOW: saved pairing roster exceeds runtime capacity");
            break;
        }
        size_t new_index = paired_count;
        memcpy(_paired_peers[new_index].mac, records[i].peer_mac, 6);
        memcpy(_paired_peers[new_index].lmk, records[i].lmk, 16);
        _paired_peers[new_index].report_interval_ms = _cfgs[cfg_index]->_report_interval_ms;
        _paired_peers[new_index].name = _cfgs[cfg_index]->displayName();
        resetPeerRuntime((int)new_index);
        if (registerPeer(_paired_peers[new_index].mac, _paired_peers[new_index].lmk)) {
            _paired_count.store(new_index + 1, std::memory_order_release);
            log_info("ESP-NOW: loaded saved pairing, peer " << mac_str(_paired_peers[new_index].mac));
        } else {
            _paired_peers[new_index].name.clear();
            memset(_paired_peers[new_index].mac, 0, sizeof(_paired_peers[new_index].mac));
            memset(_paired_peers[new_index].lmk, 0, sizeof(_paired_peers[new_index].lmk));
            _paired_peers[new_index].report_interval_ms = ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS;
            log_error("ESP-NOW: failed to restore saved pairing for " << mac_str(records[i].peer_mac));
        }
    }
    _paired.store(_paired_count.load(std::memory_order_acquire) > 0, std::memory_order_release);
    if (_paired.load(std::memory_order_acquire)) {
        setActivePeer(0);
    }

    for (size_t i = 0; i < _cfg_count; ++i) {
        if (_cfgs[i] && _cfgs[i]->configured()) {
            log_info("ESP-NOW: configured peripheral " << _cfgs[i]->displayName());
        }
    }

    allChannels.registration(this);
    if (!_paired.load(std::memory_order_acquire)) {
        setReportInterval(ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS);
    }

}

int ESPNowChannel::findPairedPeerIndex(const uint8_t* mac) const {
    PeerStateLock peer_lock(_peer_mutex);
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
    PeerStateLock peer_lock(_peer_mutex);
    uint32_t interval = ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS;
    bool     found = false;

    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < paired_count; ++i) {
        if (!peerConnected((int)i)) {
            continue;
        }
        interval = found ? std::min(interval, _paired_peers[i].report_interval_ms)
                         : _paired_peers[i].report_interval_ms;
        found = true;
    }

    int active_peer_index = _active_peer_index.load(std::memory_order_acquire);
    if (!found && active_peer_index >= 0 && active_peer_index < (int)paired_count) {
        interval = _paired_peers[active_peer_index].report_interval_ms;
    }

    setReportInterval(interval);
}

void ESPNowChannel::resetPeerRuntime(int index) {
    PeerStateLock peer_lock(_peer_mutex);
    size_t paired_count = _paired_count.load(std::memory_order_acquire);
    if (index < 0 || index >= (int)paired_count) {
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

void ESPNowChannel::notePeerAuthenticated(int index, bool control_activity) {
    PeerStateLock peer_lock(_peer_mutex);
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
    if (!_paired_peers[index].name.empty()) {
        log_info("ESP-NOW: active pendant " << _paired_peers[index].name << " (" << mac_str(_paired_peers[index].mac) << ")");
    } else {
        log_info("ESP-NOW: active pendant " << mac_str(_paired_peers[index].mac));
    }
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

int ESPNowChannel::findConfigIndexForLmk(const uint8_t* lmk) const {
    if (!lmk) {
        return -1;
    }
    uint8_t derived[16];
    for (size_t i = 0; i < _cfg_count; ++i) {
        if (!_cfgs[i] || !_cfgs[i]->configured()) {
            continue;
        }
        ESPNowCrypto::deriveLmk(_cfgs[i]->_pair_code.c_str(), derived);
        if (memcmp(derived, lmk, 16) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool ESPNowChannel::matchConfiguredPeripheral(const uint8_t* discovery_pkt, uint8_t* lmk_out, size_t& cfg_index) const {
    if (!discovery_pkt || !lmk_out) {
        return false;
    }

    for (size_t i = 0; i < _cfg_count; ++i) {
        if (!_cfgs[i] || !_cfgs[i]->configured()) {
            continue;
        }

        ESPNowCrypto::deriveLmk(_cfgs[i]->_pair_code.c_str(), lmk_out);
        if (ESPNowCrypto::verifyPairingAuthTag(lmk_out,
                                               discovery_pkt,
                                               offsetof(DiscoveryPkt, auth_tag),
                                               discovery_pkt + offsetof(DiscoveryPkt, auth_tag))) {
            cfg_index = i;
            return true;
        }
    }

    return false;
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

    int  paired_index = _instance->findPairedPeerIndex(src);
    bool from_peer    = paired_index >= 0;

    switch (pkt_type) {
        case PKT_DISCOVERY:
            if (!_instance->_discovery_pending.load(std::memory_order_acquire) &&
                len == (int)sizeof(DiscoveryPkt) && src) {
                memcpy(_instance->_discovery_buf, data, len);
                memcpy(_instance->_discovery_src, src, 6);
                _instance->_discovery_len = len;
                _instance->_discovery_pending.store(true, std::memory_order_release);
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
                auto& peer = _instance->_paired_peers[paired_index];
                bool accepted = false;
                bool authenticated = false;
                if (len >= 1 + 4 + ART_TAG_SIZE) {
                    uint32_t advertised, nonce, counter;
                    memcpy(&advertised, data + 1, 4);
                    memcpy(&nonce,      data + 5, 4);
                    memcpy(&counter,    data + 9, 4);
                    PeerStateLock peer_lock(_instance->_peer_mutex, 0);
                    if (peer_lock.locked() &&
                        advertised != 0 &&
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
                        _instance->notePeerAuthenticated(paired_index, false);
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
    if (len != (int)sizeof(DiscoveryPkt) || !src_mac) {
        return;
    }

    DiscoveryPkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (!mac_eq(src_mac, pkt.mac)) {
        log_info("ESP-NOW: DISCOVERY src/payload MAC mismatch");
        return;
    }

    uint8_t lmk[16];
    size_t  cfg_index = 0;
    if (!matchConfiguredPeripheral(reinterpret_cast<const uint8_t*>(&pkt), lmk, cfg_index)) {
        log_info("ESP-NOW: DISCOVERY authentication failed");
        return;
    }

    uint32_t pair_nonce;
    memcpy(&pair_nonce, pkt.pair_nonce, 4);
    if (pair_nonce == 0) {
        return;
    }

    const uint8_t* pendant_mac = pkt.mac;

    esp_now_del_peer(pendant_mac);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, pendant_mac, 6);
    peer.encrypt = false;
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        log_error("ESP-NOW: failed to add pendant peer");
        return;
    }

    int  paired_index = findPairedPeerIndex(pendant_mac);
    bool new_peer = paired_index < 0;
    if (new_peer) {
        size_t paired_count = _paired_count.load(std::memory_order_acquire);
        if (paired_count >= MAX_SERVER_PAIRINGS) {
            esp_now_del_peer(pendant_mac);
            log_error("ESP-NOW: pairing roster full, rejecting new pendant " << mac_str(pendant_mac));
            return;
        }
        paired_index = (int)paired_count;
    } else if (peerConnected(paired_index)) {
        esp_now_del_peer(pendant_mac);
        log_error("ESP-NOW: rejecting re-pair attempt from active pendant " << mac_str(pendant_mac));
        return;
    }
    memcpy(_paired_peers[paired_index].mac, pendant_mac, 6);
    memcpy(_paired_peers[paired_index].lmk, lmk, 16);
    _paired_peers[paired_index].report_interval_ms = _cfgs[cfg_index]->_report_interval_ms;
    _paired_peers[paired_index].name = _cfgs[cfg_index]->displayName();
    resetPeerRuntime(paired_index);

    uint8_t  our_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, our_mac);
    uint8_t  ch = (uint8_t)WiFi.channel();
    PairAckPkt ack = {};
    ack.type = PKT_PAIR_ACK;
    memcpy(ack.mac, our_mac, 6);
    ack.channel = ch;
    memcpy(ack.pair_nonce, pkt.pair_nonce, 4);
    ESPNowCrypto::pairingAuthTag(lmk,
                                 reinterpret_cast<const uint8_t*>(&ack),
                                 offsetof(PairAckPkt, auth_tag),
                                 ack.auth_tag);

    esp_now_send(pendant_mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    if (!registerPeer(pendant_mac, lmk)) {
        esp_now_del_peer(pendant_mac);
        resetPeerRuntime(paired_index);
        if (new_peer) {
            _paired_peers[paired_index].name.clear();
            memset(_paired_peers[paired_index].mac, 0, sizeof(_paired_peers[paired_index].mac));
            memset(_paired_peers[paired_index].lmk, 0, sizeof(_paired_peers[paired_index].lmk));
            _paired_peers[paired_index].report_interval_ms = ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS;
        }
        log_error("ESP-NOW: failed to enable encrypted peer for " << mac_str(pendant_mac));
        return;
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

    log_info("ESP-NOW: discovery validated from " << mac_str(pendant_mac) << " — ACK sent");
}

void ESPNowChannel::handleData(int peer_index, const uint8_t* data, int len) {
    if (len < FRAG_HDR_SIZE) {
        return;
    }

    PeerStateLock peer_lock(_peer_mutex, 0);
    if (!peer_lock.locked()) {
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
    notePeerAuthenticated(peer_index, true);

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
    notePeerAuthenticated(peer_index, true);
    rxPush(data[1 + ART_TAG_SIZE]);
}

void ESPNowChannel::onSent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {}
