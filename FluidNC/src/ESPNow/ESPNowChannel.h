// Copyright (c) 2026 - Figamore

#pragma once

#include "../Channel.h"
#include "ESPNowCrypto.h"
#include "ESPNowConfig.h"

#include <stdint.h>
#include <string>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_idf_version.h>
#include <esp_now.h>

static constexpr uint8_t PKT_DISCOVERY = 0x01;
static constexpr uint8_t PKT_PAIR_ACK  = 0x02;
static constexpr uint8_t PKT_DATA      = 0x03;
static constexpr uint8_t PKT_REALTIME  = 0x05;
static constexpr uint8_t PKT_KEEPALIVE = 0x06;

static constexpr uint8_t MAX_ESP_PAYLOAD = 250;
static constexpr uint8_t ART_TAG_SIZE   = 8;   // anti-replay tag: nonce(4) + counter(4)
static constexpr uint8_t PAIR_TAG_SIZE  = 16;  // HMAC-SHA256 truncated tag for pairing packets
static constexpr uint8_t FRAG_HDR_SIZE  = 12;  // type + nonce(4) + counter(4) + seq + idx + total
static constexpr uint8_t MAX_FRAG_DATA  = MAX_ESP_PAYLOAD - FRAG_HDR_SIZE;
static constexpr uint8_t MAX_FRAGS      = 8;

class ESPNowChannel : public Channel {
public:
    static constexpr size_t MAX_SERVER_PAIRINGS = ESPNowConfig::MAX_PAIRINGS;

    ESPNowChannel();

    void init(ESPNowConfig* cfgs[], size_t cfg_count);
    void poll();  // drain recv ring buffer; called from ESPNowModule::poll()

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t size) override;
    int    available() override;
    Error  pollLine(char* line) override;

    static ESPNowChannel* instance() { return _instance; }

#if ESP_IDF_VERSION_MAJOR >= 5
    static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len);
#else
    static void onRecv(const uint8_t* mac_addr, const uint8_t* data, int len);
#endif
    static void onSent(const uint8_t* mac, esp_now_send_status_t status);

private:
    static ESPNowChannel* _instance;

    struct FragBuf {
        uint8_t  data[MAX_FRAGS][MAX_FRAG_DATA];
        uint16_t sizes[MAX_FRAGS];
        uint8_t  received;
        uint8_t  total;
        uint8_t  seq;
        bool     active;
        uint32_t start_ms;
    };

    struct PairedPeer {
        uint8_t mac[6] = {};
        uint8_t lmk[16] = {};
        uint32_t    report_interval_ms = ESPNowConfig::DEFAULT_REPORT_INTERVAL_MS;
        std::string name;

        std::atomic<uint32_t> rx_nonce {0};
        std::atomic<uint32_t> tx_peer_nonce {0};
        std::atomic<bool>     tx_peer_known {false};
        std::atomic<bool>     session_authenticated {false};
        std::atomic<uint32_t> tx_counter {0};
        std::atomic<uint32_t> last_rx_ms {0};
        std::atomic<uint32_t> last_control_ms {0};
        std::atomic<bool>     echo_pending {false};

        ESPNowCrypto::ReplayState rx_replay;
        FragBuf                  frag = {};
        uint8_t                  tx_seq = 0;
        bool                     was_connected = false;

        PairedPeer() = default;
        PairedPeer(const PairedPeer&) = delete;
        PairedPeer& operator=(const PairedPeer&) = delete;
        PairedPeer(PairedPeer&&) = delete;
        PairedPeer& operator=(PairedPeer&&) = delete;
    };

    ESPNowConfig* _cfgs[ESPNowConfig::MAX_CONFIGS] = { nullptr };
    size_t        _cfg_count = 0;
    std::atomic<bool>   _paired {false};
    std::atomic<size_t> _paired_count {0};
    std::atomic<int>    _active_peer_index {-1};
    PairedPeer    _paired_peers[MAX_SERVER_PAIRINGS];
    SemaphoreHandle_t _peer_mutex = nullptr;

    // TX
    std::string _tx_buf;

    // RX
    static constexpr size_t RX_BUF_SIZE = 4096;
    static uint8_t          _rx_buf[RX_BUF_SIZE];
    static std::atomic<int> _rx_head;
    static std::atomic<int> _rx_tail;

    void rxPush(uint8_t byte);
    void drainRxBuffer();

    void sendFragmented(const uint8_t* data, size_t len);
    void sendFragmentedToPeer(PairedPeer& peer, const uint8_t* data, size_t len);

    void handleDiscovery(const uint8_t* src_mac, const uint8_t* data, int len);
    void handleData(int peer_index, const uint8_t* data, int len);
    void handleRealtime(int peer_index, const uint8_t* data, int len);

    bool registerPeer(const uint8_t* mac, const uint8_t* lmk);
    int  findPairedPeerIndex(const uint8_t* mac) const;
    bool setActivePeer(int index);
    bool canSwitchActivePeer() const;
    bool peerConnected(int index) const;
    bool claimControlLease(int index);
    void resetPeerRuntime(int index);
    void notePeerAuthenticated(int index, bool control_activity);
    void refreshReportInterval();
    int  findConfigIndexForLmk(const uint8_t* lmk) const;
    bool matchConfiguredPeripheral(const uint8_t* discovery_pkt, uint8_t* lmk_out, size_t& cfg_index) const;

    std::atomic<bool> _discovery_pending {false};
    uint8_t           _discovery_buf[32] = {};
    uint8_t           _discovery_src[6]  = {};
    int               _discovery_len     = 0;
};

extern ESPNowChannel espnowChannel;
