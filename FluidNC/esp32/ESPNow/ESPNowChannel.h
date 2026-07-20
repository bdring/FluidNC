// Copyright (c) 2026 - Figamore

#pragma once

#include "Channel.h"
#include "ESPNowCrypto.h"
#include "ESPNowConfig.h"

#include <stdint.h>
#include <string>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_idf_version.h>
#include <esp_now.h>

static constexpr uint8_t PKT_DISCOVERY           = 0x01;
static constexpr uint8_t PKT_PAIR_CHALLENGE      = 0x02;
static constexpr uint8_t PKT_DATA                = 0x03;
static constexpr uint8_t PKT_PAIR_CONFIRM        = 0x04;
static constexpr uint8_t PKT_REALTIME            = 0x05;
static constexpr uint8_t PKT_KEEPALIVE           = 0x06;
static constexpr uint8_t PKT_PAIR_RESULT         = 0x07;
static constexpr uint8_t PKT_PAIR_COMPLETE       = 0x08;
static constexpr uint8_t PAIRING_PROTO_V4        = 4;
static constexpr uint8_t PAIRING_MODE_PAIR       = 1;
static constexpr size_t  ESPNOW_HOSTNAME_SIZE    = 32;
static constexpr size_t  PAIRING_SESSION_ID_SIZE = 16;

static constexpr uint8_t MAX_ESP_PAYLOAD = 250;
static constexpr uint8_t ART_TAG_SIZE    = 8;   // anti-replay tag: nonce(4) + counter(4)
static constexpr uint8_t PAIR_TAG_SIZE   = 16;  // HMAC-SHA256 truncated tag for pairing packets
static constexpr uint8_t FRAG_HDR_SIZE   = 12;  // type + nonce(4) + counter(4) + seq + idx + total
static constexpr uint8_t MAX_FRAG_DATA   = MAX_ESP_PAYLOAD - FRAG_HDR_SIZE;
static constexpr uint8_t MAX_FRAGS       = 8;

class ESPNowChannel : public Channel {
public:
    static constexpr size_t MAX_SERVER_PAIRINGS = ESPNowConfig::MAX_PAIRINGS;

    ESPNowChannel();

    void init();
    void poll();  // drain recv ring buffer; called from ESPNowModule::poll()

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t size) override;
    int    available() override;
    Error  pollLine(char* line) override;
    void   flushRx() override;

    static ESPNowChannel* instance() { return _instance; }
    bool                  startPairingWindow(uint32_t window_ms);
    void                  cancelPairingWindow();
    bool                  listPairings(Channel& out) const;
    bool                  removePairingIndex(size_t one_based_index, uint8_t removed_mac[6]);
    void                  clearPairings();

#if ESP_IDF_VERSION_MAJOR >= 5
    static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len);
#else
    static void onRecv(const uint8_t* mac_addr, const uint8_t* data, int len);
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    // esp_now_send_cb_t was changed to take a wifi_tx_info_t (aka
    // esp_now_send_info_t) instead of a bare destination MAC address.
    static void onSent(const esp_now_send_info_t* tx_info, esp_now_send_status_t status);
#else
    static void onSent(const uint8_t* mac, esp_now_send_status_t status);
#endif
    static void handleSent(const uint8_t* mac, esp_now_send_status_t status);

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

    enum class PeerSessionState : uint8_t {
        Synchronizing,
        Connected,
    };

    struct PairedPeer {
        uint8_t mac[6]  = {};
        uint8_t lmk[16] = {};

        std::atomic<uint32_t>         rx_nonce { 0 };
        std::atomic<uint32_t>         tx_peer_nonce { 0 };
        std::atomic<bool>             tx_peer_known { false };
        std::atomic<PeerSessionState> session_state { PeerSessionState::Synchronizing };
        std::atomic<uint32_t>         tx_counter { 0 };
        std::atomic<uint32_t>         last_rx_ms { 0 };
        std::atomic<uint32_t>         last_control_ms { 0 };
        std::atomic<bool>             echo_pending { false };

        ESPNowCrypto::ReplayState rx_replay;
        FragBuf                   frag                   = {};
        uint32_t                  motion_barrier_counter = 0;
        uint8_t                   tx_seq                 = 0;

        PairedPeer()                             = default;
        PairedPeer(const PairedPeer&)            = delete;
        PairedPeer& operator=(const PairedPeer&) = delete;
        PairedPeer(PairedPeer&&)                 = delete;
        PairedPeer& operator=(PairedPeer&&)      = delete;
    };

    enum class PairingState : uint8_t {
        Idle,
        AwaitConfirm,
        ReadyResult,
        AwaitComplete,
    };

    struct PairingTransaction {
        PairingState state                               = PairingState::Idle;
        uint8_t      mac[6]                              = {};
        uint8_t      session_id[PAIRING_SESSION_ID_SIZE] = {};
        uint8_t      lmk[16]                             = {};
        uint8_t      challenge[MAX_ESP_PAYLOAD]          = {};
        uint16_t     challenge_len                       = 0;
        uint8_t      result[MAX_ESP_PAYLOAD]             = {};
        uint16_t     result_len                          = 0;
        uint8_t      send_attempts                       = 0;
        uint32_t     started_ms                          = 0;
        uint32_t     last_ms                             = 0;
    };

    struct RxPacket {
        uint8_t  src[6];
        uint16_t len;
        uint8_t  data[MAX_ESP_PAYLOAD];
    };

    std::atomic<bool>   _paired { false };
    std::atomic<size_t> _paired_count { 0 };
    std::atomic<int>    _active_peer_index { -1 };
    PairedPeer          _paired_peers[MAX_SERVER_PAIRINGS];
    SemaphoreHandle_t   _peer_mutex = nullptr;

    // TX
    std::string _tx_buf;

    // RX
    static constexpr size_t RX_BUF_SIZE = 4096;
    static uint8_t          _rx_buf[RX_BUF_SIZE];
    static std::atomic<int> _rx_head;
    static std::atomic<int> _rx_tail;
    QueueHandle_t           _packet_queue       = nullptr;
    QueueHandle_t           _pairing_queue      = nullptr;
    QueueHandle_t           _pair_confirm_queue = nullptr;

    void rxPush(uint8_t byte);
    void drainRxBuffer();
    void processPacket(const RxPacket& packet);
    void processPairingTransaction();
    void clearPairingTransaction(bool restore_peer);

    void sendFragmented(const uint8_t* data, size_t len);
    void sendFragmentedToPeer(PairedPeer& peer, const uint8_t* data, size_t len);

    void handleDiscovery(const uint8_t* src_mac, const uint8_t* data, int len);
    void handlePairConfirm(const uint8_t* src_mac, const uint8_t* data, int len);
    void handlePairComplete(const uint8_t* src_mac, const uint8_t* data, int len);
    void handleData(int peer_index, const uint8_t* data, int len);
    void handleRealtime(int peer_index, const uint8_t* data, int len);
    void handleKeepalive(int peer_index, const uint8_t* data, int len);

    bool registerPeer(const uint8_t* mac, const uint8_t* lmk);
    bool activatePairing();
    int  findPairedPeerIndex(const uint8_t* mac, TickType_t timeout = portMAX_DELAY) const;
    int  findPairedPeerIndexLocked(const uint8_t* mac) const;
    bool setActivePeer(int index);
    bool setActivePeerLocked(int index);
    bool canSwitchActivePeerLocked(uint32_t now) const;
    bool peerConnected(int index) const;
    bool peerConnectedLocked(int index, uint32_t now) const;
    bool claimControlLeaseLocked(int index, uint32_t now);
    bool claimControlLeaseLocked(int index, uint32_t now, bool force);
    void resetPeerRuntime(int index);
    void resetPeerRuntimeLocked(int index);
    bool notePeerAuthenticatedLocked(int index, bool control_activity, uint32_t now);
    bool dataMessageClaimsControl(const PairedPeer& peer) const;
    void refreshReportInterval();
    void restorePairedPeerOrDelete(const uint8_t* mac);
    bool pairingWindowActive();
    bool removeRuntimePeer(const uint8_t* mac);

    bool _initialized = false;
    bool _registered  = false;

    static constexpr uint32_t PAIRING_HANDSHAKE_TIMEOUT_MS = 10000;
    PairingTransaction        _pairing;
    uint8_t                   _pairing_callback_mac[6] = {};
    std::atomic<bool>         _pairing_challenge_waiting { false };
    std::atomic<bool>         _pairing_window_active { false };
    std::atomic<uint32_t>     _pairing_window_until_ms { 0 };
    uint32_t                  _last_pairing_packet_ms = 0;
};

extern ESPNowChannel espnowChannel;
