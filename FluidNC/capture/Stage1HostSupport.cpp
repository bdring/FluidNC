#include "Stage1HostSupport.h"

#include "ArduinoOTA.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "mdns.h"

namespace Stage1HostSupport {
    I2CState g_i2c;
    SPIState g_spi;
    I2SOState g_i2so;

    void resetBusState() {
        g_i2c = {};
        g_spi = {};
        g_i2so = {};
    }

    void resetWebUiState() {
        WiFi.setMode(WIFI_OFF);
        WiFi.setHostname("fluidnc-host");

        g_mdnsInitResult = 0;
        g_mdnsHostnameSetResult = 0;
        g_mdnsFreeCalls = 0;
        g_mdnsAddedServices.clear();
        g_mdnsRemovedServices.clear();

        g_wifiClientConnectResult = true;
        g_wifiClientConnected = false;
        g_wifiClientStopCalls = 0;
        g_wifiClientSetInsecureCalls = 0;
        g_wifiClientWrites.clear();
        g_wifiClientReadLines.clear();
        g_wifiClientLastErrorCode = 0;
        g_wifiClientLastErrorText.clear();

        ArduinoOTA.mdnsEnabled = true;
        ArduinoOTA.hostname = nullptr;
        ArduinoOTA.command = U_FLASH;
        ArduinoOTA.beginCalls = 0;
        ArduinoOTA.endCalls = 0;
        ArduinoOTA.handleCalls = 0;
        ArduinoOTA.onStartHandler = nullptr;
        ArduinoOTA.onEndHandler = nullptr;
        ArduinoOTA.onProgressHandler = nullptr;
        ArduinoOTA.onErrorHandler = nullptr;
    }
}

bool i2c_master_init(objnum_t bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency) {
    ++Stage1HostSupport::g_i2c.initCalls;
    Stage1HostSupport::g_i2c.initBus = bus_number;
    Stage1HostSupport::g_i2c.initSda = sda_pin;
    Stage1HostSupport::g_i2c.initScl = scl_pin;
    Stage1HostSupport::g_i2c.initFrequency = frequency;
    return Stage1HostSupport::g_i2c.initError;
}

int i2c_write(objnum_t bus_number, uint8_t address, const uint8_t* data, size_t count) {
    Stage1HostSupport::g_i2c.lastWriteBus = bus_number;
    Stage1HostSupport::g_i2c.lastWriteAddress = address;
    Stage1HostSupport::g_i2c.lastWriteData.assign(data, data + count);
    return Stage1HostSupport::g_i2c.writeResult;
}

int i2c_read(objnum_t bus_number, uint8_t address, uint8_t* data, size_t count) {
    Stage1HostSupport::g_i2c.lastReadBus = bus_number;
    Stage1HostSupport::g_i2c.lastReadAddress = address;
    for (size_t i = 0; i < count; ++i) {
        data[i] = i < Stage1HostSupport::g_i2c.readData.size() ? Stage1HostSupport::g_i2c.readData[i] : 0;
    }
    return Stage1HostSupport::g_i2c.readResult;
}
