// Copyright (c) 2026 - Figamore

#include "ESPNowChannel.h"
#include "../Module.h"
#include "../Machine/MachineConfig.h"
#include "../Serial.h"
#include "../Logging.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <Arduino.h>

static constexpr uint32_t FRAG_REASSEMBLY_TIMEOUT_MS = 3000;

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
std::atomic<uint32_t> ESPNowChannel::_last_rx_ms {0};
std::atomic<bool> ESPNowChannel::_echo_pending {false};
volatile int8_t ESPNowChannel::_echo_rssi        = 0;
std::atomic<bool> ESPNowChannel::_pair_ack_pending {false};
uint8_t         ESPNowChannel::_pair_ack_buf[12] = {};

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


static void deriveLmk(const char* code, uint8_t lmk_out[16]) {
    const char* prefix = "fluiddial-espnow:";
    uint8_t     hash[32];

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t*)prefix, strlen(prefix));
    mbedtls_sha256_update(&ctx, (const uint8_t*)code, strlen(code));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    memcpy(lmk_out, hash, 16);
}

static void derivePmk(uint8_t pmk_out[16]) {
    const char* key = "fluiddial-espnow-pmk-v1";
    uint8_t     hash[32];
    mbedtls_sha256((const uint8_t*)key, strlen(key), hash, 0);
    memcpy(pmk_out, hash, 16);
}


void ESPNowModule::init() {
    if (!config || !config->_espnow) {
        return;
    }

    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
    }

    espnowChannel.init(config->_espnow);
}

void ESPNowModule::poll() {
    espnowChannel.poll();
}


ESPNowChannel::ESPNowChannel() : Channel("espnow") {
    _instance = this;
    memset(&_frag, 0, sizeof(_frag));
}

void ESPNowChannel::init(ESPNowConfig* cfg) {
    _cfg = cfg;

    if (esp_now_init() != ESP_OK) {
        log_error("ESP-NOW: esp_now_init() failed");
        return;
    }

    uint8_t pmk[16];
    derivePmk(pmk);
    esp_now_set_pmk(pmk);

    esp_now_register_recv_cb(ESPNowChannel::onRecv);
    esp_now_register_send_cb(ESPNowChannel::onSent);

    uint8_t saved_mac[6], saved_lmk[16];
    if (ESPNowConfig::loadPairing(saved_mac, saved_lmk)) {
        if (registerPeer(saved_mac, saved_lmk)) {
            memcpy(_peer_mac, saved_mac, 6);
            memcpy(_peer_lmk, saved_lmk, 16);
            _paired = true;
            log_info("ESP-NOW: loaded saved pairing, peer " << mac_str(saved_mac));
        }
    }

    if (cfg && !cfg->_pair_code.empty()) {
        log_info("ESP-NOW: pairing mode active");
    }

    allChannels.registration(this);
    setReportInterval(200);

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

static constexpr uint32_t PENDANT_IDLE_TIMEOUT_MS = 10000;

size_t ESPNowChannel::write(const uint8_t* buf, size_t size) {
    if (!_paired || size == 0) {
        return size;
    }
    uint32_t last_rx_ms = _last_rx_ms.load(std::memory_order_acquire);
    if (last_rx_ms == 0) {
        return size;
    }
    if ((uint32_t)(millis() - last_rx_ms) > PENDANT_IDLE_TIMEOUT_MS) {
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
    uint8_t total_frags = (uint8_t)((len + MAX_FRAG_DATA - 1) / MAX_FRAG_DATA);
    if (total_frags == 0) {
        total_frags = 1;
    }
    if (total_frags > MAX_FRAGS) {
        log_error("ESP-NOW: outgoing message (" << len << " B) exceeds max fragmented size — truncating");
        total_frags = MAX_FRAGS;
        len         = MAX_FRAGS * MAX_FRAG_DATA;
    }

    uint8_t  seq = _tx_seq++;
    uint8_t  pkt[MAX_ESP_PAYLOAD];
    size_t   offset = 0;

    for (uint8_t i = 0; i < total_frags; i++) {
        size_t chunk = len - offset;
        if (chunk > MAX_FRAG_DATA) {
            chunk = MAX_FRAG_DATA;
        }

        pkt[0] = PKT_DATA;
        pkt[1] = seq;
        pkt[2] = i;
        pkt[3] = total_frags;
        memcpy(pkt + FRAG_HDR_SIZE, data + offset, chunk);

        esp_now_send(_peer_mac, pkt, FRAG_HDR_SIZE + chunk);
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
    if (_pair_ack_pending.load(std::memory_order_acquire)) {
        _pair_ack_pending.store(false, std::memory_order_release);
        esp_now_send(_peer_mac, _pair_ack_buf, sizeof(_pair_ack_buf));

        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, _peer_mac, 6);
        memcpy(peer.lmk, _peer_lmk, 16);
        peer.encrypt = true;
        peer.channel = 0;
        peer.ifidx   = WIFI_IF_STA;
        esp_now_mod_peer(&peer);

        _paired = true;
        log_info("ESP-NOW: paired with pendant " << mac_str(_peer_mac));
    }

    if (_echo_pending.load(std::memory_order_acquire)) {
        _echo_pending.store(false, std::memory_order_release);
        uint8_t reply[2] = {PKT_KEEPALIVE, (uint8_t)_echo_rssi};
        esp_now_send(_peer_mac, reply, 2);
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

    bool from_peer = mac_eq(src, _instance->_peer_mac);

    if (_instance->_paired && from_peer && pkt_type != PKT_DISCOVERY) {
        _instance->_last_rx_ms.store((uint32_t)millis(), std::memory_order_release);
    }

    switch (pkt_type) {
        case PKT_DISCOVERY:
            _instance->handleDiscovery(src, data, len);
            break;
        case PKT_DATA:
            if (_instance->_paired && from_peer) {
                _instance->handleData(data, len);
            }
            break;
        case PKT_REALTIME:
            if (_instance->_paired && from_peer && len >= 2) {
                _instance->handleRealtime(data, len);
            }
            break;
        case PKT_KEEPALIVE:
            if (_instance->_paired && from_peer) {
               
#if ESP_IDF_VERSION_MAJOR >= 5
                _instance->_echo_rssi = (info && info->rx_ctrl)
                                        ? (int8_t)info->rx_ctrl->rssi : 0;
#else
                _instance->_echo_rssi = 0;
#endif
                _instance->_echo_pending.store(true, std::memory_order_release);
            }
            break;
        default:
            break;
    }
}


void ESPNowChannel::handleDiscovery(const uint8_t* src_mac, const uint8_t* data, int len) {
   
    if (_pair_ack_pending.load(std::memory_order_acquire)) {
        return;
    }
    if (len < 12 || !_cfg || _cfg->_pair_code.empty()) {
        return;
    }

    uint8_t lmk[16];
    deriveLmk(_cfg->_pair_code.c_str(), lmk);

    const uint8_t* code_hash  = data + 7;
    if (memcmp(lmk, code_hash, 4) != 0) {
        log_info("ESP-NOW: DISCOVERY code_hash mismatch — wrong pair_code?");
        return;
    }

    const uint8_t* pendant_mac = data + 1;

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

    memcpy(_peer_mac, pendant_mac, 6);
    memcpy(_peer_lmk, lmk, 16);
    ESPNowConfig::savePairing(_peer_mac, _peer_lmk);

    // Build the PAIR_ACK packet and hand it off to poll() for sending.
    uint8_t  our_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, our_mac);
    uint8_t  ch = (uint8_t)WiFi.channel();
    uint32_t ts = (uint32_t)millis();

    _pair_ack_buf[0] = PKT_PAIR_ACK;
    memcpy(_pair_ack_buf + 1, our_mac, 6);
    _pair_ack_buf[7]  = ch;
    _pair_ack_buf[8]  = (uint8_t)(ts & 0xFF);
    _pair_ack_buf[9]  = (uint8_t)((ts >> 8) & 0xFF);
    _pair_ack_buf[10] = (uint8_t)((ts >> 16) & 0xFF);
    _pair_ack_buf[11] = (uint8_t)((ts >> 24) & 0xFF);
    _pair_ack_pending.store(true, std::memory_order_release);

    log_info("ESP-NOW: discovery validated from " << mac_str(pendant_mac) << " — ACK queued");
}

void ESPNowChannel::handleData(const uint8_t* data, int len) {
    if (len < FRAG_HDR_SIZE + 1) {
        return;
    }

    uint8_t seq        = data[1];
    uint8_t frag_idx   = data[2];
    uint8_t total_frags = data[3];
    size_t  payload_len = (size_t)(len - FRAG_HDR_SIZE);

    if (total_frags == 0 || total_frags > MAX_FRAGS || frag_idx >= total_frags) {
        return;
    }
    if (payload_len > MAX_FRAG_DATA) {
        payload_len = MAX_FRAG_DATA;
    }

    // Expire stale reassembly window before accepting a new sequence
    if (_frag.active) {
        bool stale_seq     = (_frag.seq != seq);
        bool stale_timeout = ((uint32_t)(millis() - _frag.start_ms)
                              > FRAG_REASSEMBLY_TIMEOUT_MS);
        if (stale_seq || stale_timeout) {
            memset(&_frag, 0, sizeof(_frag));
        }
    }

    if (!_frag.active) {
        _frag.seq      = seq;
        _frag.total    = total_frags;
        _frag.active   = true;
        _frag.start_ms = (uint32_t)millis();
        _frag.received = 0;
        memset(_frag.sizes, 0, sizeof(_frag.sizes));
    }

    if (_frag.total != total_frags) {
        return;
    }

    memcpy(_frag.data[frag_idx], data + FRAG_HDR_SIZE, payload_len);
    _frag.sizes[frag_idx]  = (uint16_t)payload_len;
    _frag.received        |= (1u << frag_idx);

    // Check whether all fragments received
    uint8_t full_mask = (uint8_t)((1u << total_frags) - 1u);
    if ((_frag.received & full_mask) != full_mask) {
        return;
    }

    // Reassembly complete — push bytes -> ring buffer
    for (uint8_t i = 0; i < total_frags; i++) {
        for (uint16_t j = 0; j < _frag.sizes[i]; j++) {
            rxPush(_frag.data[i][j]);
        }
    }

    if (_frag.sizes[total_frags - 1] == 0 ||
        _frag.data[total_frags - 1][_frag.sizes[total_frags - 1] - 1] != '\n') {
        rxPush('\n');
    }

    _frag.active = false;
}


void ESPNowChannel::handleRealtime(const uint8_t* data, int len) {
    rxPush(data[1]);
}

void ESPNowChannel::onSent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {}
