// Copyright (c) 2026 - Figamore

#pragma once

#include "../Channel.h"
#include "ESPNowConfig.h"

#include <stdint.h>
#include <string>
#include <esp_idf_version.h>
#include <esp_now.h>

static constexpr uint8_t PKT_DISCOVERY = 0x01;
static constexpr uint8_t PKT_PAIR_ACK  = 0x02;
static constexpr uint8_t PKT_DATA      = 0x03;
static constexpr uint8_t PKT_REALTIME  = 0x05;
static constexpr uint8_t PKT_KEEPALIVE = 0x06;

static constexpr uint8_t MAX_ESP_PAYLOAD = 250;
static constexpr uint8_t FRAG_HDR_SIZE  = 4;
static constexpr uint8_t MAX_FRAG_DATA  = MAX_ESP_PAYLOAD - FRAG_HDR_SIZE;
static constexpr uint8_t MAX_FRAGS      = 8;

class ESPNowChannel : public Channel {
public:
    ESPNowChannel();

    void init(ESPNowConfig* cfg);
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

private:
    static ESPNowChannel* _instance;

    ESPNowConfig* _cfg    = nullptr;
    bool          _paired = false;

    uint8_t _peer_mac[6] = {};
    uint8_t _peer_lmk[16] = {};

    // TX
    std::string _tx_buf;
    uint8_t     _tx_seq = 0;

    // RX
    static constexpr size_t RX_BUF_SIZE = 4096;
    static uint8_t          _rx_buf[RX_BUF_SIZE];
    static volatile int     _rx_head;
    static int              _rx_tail;

    struct FragBuf {
        uint8_t  data[MAX_FRAGS][MAX_FRAG_DATA];
        uint16_t sizes[MAX_FRAGS];
        uint8_t  received;
        uint8_t  total;
        uint8_t  seq;
        bool     active;
    };
    FragBuf _frag;

    void rxPush(uint8_t byte);
    void drainRxBuffer();

    void sendFragmented(const uint8_t* data, size_t len);

    void handleDiscovery(const uint8_t* src_mac, const uint8_t* data, int len);
    void handleData(const uint8_t* data, int len);
    void handleRealtime(const uint8_t* data, int len);

    bool registerPeer(const uint8_t* mac, const uint8_t* lmk);
};

extern ESPNowChannel espnowChannel;
