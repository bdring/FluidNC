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

class ESPNowModule : public Module {
public:
    ESPNowModule(const char* name) : Module(name) {}
    void init() override;
    void poll() override;
};

static ModuleFactory::InstanceBuilder<ESPNowModule> espnow_module __attribute__((init_priority(110)))("espnow", true);


ESPNowChannel* ESPNowChannel::_instance = nullptr;
uint8_t        ESPNowChannel::_rx_buf[ESPNowChannel::RX_BUF_SIZE];
volatile int   ESPNowChannel::_rx_head = 0;
int            ESPNowChannel::_rx_tail = 0;

ESPNowChannel espnowChannel;


static const char* mac_str(const uint8_t* mac) {
    static char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
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
    bool have_config  = (config && config->_espnow != nullptr);
    bool have_pairing = ESPNowConfig::hasPairing();
    log_info("ESP-NOW: module init — have_config=" << have_config << " have_pairing=" << have_pairing);

    if (!have_config && !have_pairing) {
        log_info("ESP-NOW: no config and no saved pairing — skipping init");
        return;
    }

    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
        log_info("ESP-NOW: started WiFi in STA mode for radio layer");
    }

    espnowChannel.init(have_config ? config->_espnow : nullptr);
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
        log_info("ESP-NOW: pairing mode active (pair_code set in config.yaml)");
    }

    allChannels.registration(this);
    setReportInterval(200);

    log_info("ESP-NOW: channel ready");
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
    if (!_paired || size == 0) {
        return size;
    }

    _tx_buf.append(reinterpret_cast<const char*>(buf), size);

    if (_tx_buf.back() == '\n' || _tx_buf.size() >= 1024) {
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
    int next = (_rx_head + 1) % RX_BUF_SIZE;
    if (next == _rx_tail) {
        return;  // buffer full — drop
    }
    _rx_buf[_rx_head] = byte;
    _rx_head          = next;
}

void ESPNowChannel::drainRxBuffer() {
    int head = _rx_head;
    while (_rx_tail != head) {
        uint8_t byte = _rx_buf[_rx_tail];
        _rx_tail     = (_rx_tail + 1) % RX_BUF_SIZE;
        Channel::push(byte);
    }
}

void ESPNowChannel::poll() {
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

    switch (pkt_type) {
        case PKT_DISCOVERY:
            _instance->handleDiscovery(src, data, len);
            break;
        case PKT_DATA:
            if (_instance->_paired) {
                _instance->handleData(data, len);
            }
            break;
        case PKT_REALTIME:
            if (_instance->_paired && len >= 2) {
                _instance->handleRealtime(data, len);
            }
            break;
        case PKT_KEEPALIVE:
            if (_instance->_paired) {
               
#if ESP_IDF_VERSION_MAJOR >= 5
                int8_t  rssi     = (info && info->rx_ctrl) ? (int8_t)info->rx_ctrl->rssi : 0;
                uint8_t reply[2] = {PKT_KEEPALIVE, (uint8_t)rssi};
                esp_now_send(_instance->_peer_mac, reply, 2);
#else
                uint8_t reply[1] = {PKT_KEEPALIVE};
                esp_now_send(_instance->_peer_mac, reply, 1);
#endif
            }
            break;
        default:
            break;
    }
}


void ESPNowChannel::handleDiscovery(const uint8_t* src_mac, const uint8_t* data, int len) {
    if (_paired && (!_cfg || _cfg->_pair_code.empty())) {
        return;
    }
    if (len < 12) {
        return;
    }

    if (!_cfg || _cfg->_pair_code.empty()) {
        return;
    }

    // Derive LMK from pair_code and check the 4-byte hash
    uint8_t lmk[16];
    deriveLmk(_cfg->_pair_code.c_str(), lmk);

    const uint8_t* code_hash = data + 7;
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

    // Build PKT_PAIR_ACK: [type:1][our_mac:6][our_channel:1][timestamp_ms:4]
    uint8_t  our_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, our_mac);
    uint8_t  ch  = (uint8_t)WiFi.channel();
    uint32_t ts  = (uint32_t)millis();

    uint8_t ack[12];
    ack[0] = PKT_PAIR_ACK;
    memcpy(ack + 1, our_mac, 6);
    ack[7]  = ch;
    ack[8]  = (uint8_t)(ts & 0xFF);
    ack[9]  = (uint8_t)((ts >> 8) & 0xFF);
    ack[10] = (uint8_t)((ts >> 16) & 0xFF);
    ack[11] = (uint8_t)((ts >> 24) & 0xFF);

    esp_now_send(_peer_mac, ack, sizeof(ack));

    memcpy(peer.lmk, lmk, 16);
    peer.encrypt = true;
    esp_now_mod_peer(&peer);

    _paired = true;

    log_info("ESP-NOW: paired with pendant " << mac_str(pendant_mac));
    log_info("ESP-NOW: remove pair_code from config.yaml after successful pairing");
}

void ESPNowChannel::handleData(const uint8_t* data, int len) {
    if (len < FRAG_HDR_SIZE + 1) {
        return;
    }

    uint8_t seq        = data[1];
    uint8_t frag_idx   = data[2];
    uint8_t total_frags = data[3];
    size_t  payload_len = (size_t)(len - FRAG_HDR_SIZE);

    if (frag_idx >= MAX_FRAGS || total_frags == 0 || total_frags > MAX_FRAGS) {
        return;
    }
    if (payload_len > MAX_FRAG_DATA) {
        payload_len = MAX_FRAG_DATA;
    }

    if (!_frag.active || _frag.seq != seq) {
        memset(&_frag, 0, sizeof(_frag));
        _frag.seq    = seq;
        _frag.total  = total_frags;
        _frag.active = true;
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

    _frag.active = false;
}


void ESPNowChannel::handleRealtime(const uint8_t* data, int len) {
    rxPush(data[1]);
}
