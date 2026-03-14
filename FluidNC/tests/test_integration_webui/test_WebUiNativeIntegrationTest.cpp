#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#define private public
#define protected public
#include "Module.h"
#include "Settings.h"
#include "WebUI/Mdns.h"
#include "WebUI/Mime.h"
#include "WebUI/NotificationsService.h"
#undef protected
#undef private

#include "ArduinoOTA.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "mdns.h"

void protocol_buffer_synchronize() {}

void delay_ms(uint32_t) {}

void localfs_unmount() {}

void trim(std::string_view& sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
}

bool read_number(const std::string_view sv, float& value, bool) {
    std::string text(sv);
    char* end = nullptr;
    value = std::strtof(text.c_str(), &end);
    return end != text.c_str() && *end == '\0';
}

std::string IP_string(uint32_t ipaddr) {
    return std::to_string((ipaddr >> 24) & 0xff) + "." + std::to_string((ipaddr >> 16) & 0xff) + "."
           + std::to_string((ipaddr >> 8) & 0xff) + "." + std::to_string(ipaddr & 0xff);
}

unsigned long millis();

namespace {
constexpr int kPushoverNotification = 1;
constexpr int kEmailNotification = 2;
constexpr int kLineNotification = 3;
constexpr int kTelegramNotification = 4;
constexpr uint32_t kPushoverTimeout = 5000;
constexpr uint32_t kLineTimeout = 5000;
constexpr uint32_t kTelegramTimeout = 5000;
constexpr uint32_t kEmailTimeout = 5000;

void resetWebUiHarness() {
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

    WebUI::NotificationsService::_started = false;
    WebUI::NotificationsService::_notificationType = 0;
    WebUI::NotificationsService::_token1.clear();
    WebUI::NotificationsService::_token2.clear();
    WebUI::NotificationsService::_settings.clear();
    WebUI::NotificationsService::_serveraddress.clear();
    WebUI::NotificationsService::_port = 0;
}

class WebUiFixture : public ::testing::Test {
protected:
    void SetUp() override {
        resetWebUiHarness();
    }
};
}  // namespace

namespace WebUI {
bool Wait4Answer(WiFiClientSecure& client, const char* linetrigger, const char* expected_answer, uint32_t timeout);
}

TEST_F(WebUiFixture, MimeLookupIsCaseInsensitiveForKnownTypes) {
    EXPECT_STREQ(getContentType("index.HTML"), "text/html");
    EXPECT_STREQ(getContentType("styles.CSS"), "text/css");
    EXPECT_STREQ(getContentType("job.GCODE"), "text/plain");
}

TEST_F(WebUiFixture, MimeLookupFallsBackToOctetStreamForUnknownTypes) {
    EXPECT_STREQ(getContentType("archive.bin"), "application/octet-stream");
    EXPECT_STREQ(getContentType("no_extension"), "application/octet-stream");
}

TEST_F(WebUiFixture, MdnsModuleInitializesAndTracksServicesOnStaWifi) {
    WebUI::Mdns module("mdns");

    WiFi.setMode(WIFI_STA);
    module.init();
    module.add("http", "tcp", 80);
    module.remove("http", "tcp");
    module.deinit();

    EXPECT_EQ(g_mdnsAddedServices.size(), 1u);
    EXPECT_EQ(g_mdnsAddedServices.front(), "http/tcp:80");
    ASSERT_EQ(g_mdnsRemovedServices.size(), 1u);
    EXPECT_EQ(g_mdnsRemovedServices.front().first, "http");
    EXPECT_EQ(g_mdnsRemovedServices.front().second, "tcp");
    EXPECT_EQ(g_mdnsFreeCalls, 1);
}

TEST_F(WebUiFixture, MdnsModuleSkipsInitializationWhenWifiIsOff) {
    WebUI::Mdns module("mdns");

    module.init();
    module.add("http", "tcp", 80);

    EXPECT_TRUE(g_mdnsAddedServices.empty());
}

TEST_F(WebUiFixture, OtaModuleConfiguresCallbacksAndPolls) {
    class HostOtaModule : public Module {
    public:
        HostOtaModule() : Module("ota") {}

        void init() override {
            if (WiFi.getMode() == WIFI_OFF) {
                return;
            }

            ArduinoOTA.setMdnsEnabled(false)
                .setHostname(WiFi.getHostname())
                .onStart([]() {})
                .onEnd([]() {})
                .onProgress([](unsigned int, unsigned int) {})
                .onError([](ota_error_t) {})
                .begin();
        }

        void deinit() override {
            ArduinoOTA.end();
        }

        void poll() override {
            ArduinoOTA.handle();
        }
    } module;

    WiFi.setMode(WIFI_STA);
    WiFi.setHostname("ota-host");

    module.init();
    module.poll();
    module.deinit();

    EXPECT_FALSE(ArduinoOTA.mdnsEnabled);
    EXPECT_STREQ(ArduinoOTA.hostname, "ota-host");
    EXPECT_EQ(ArduinoOTA.beginCalls, 1);
    EXPECT_EQ(ArduinoOTA.handleCalls, 1);
    EXPECT_EQ(ArduinoOTA.endCalls, 1);
    ASSERT_TRUE(static_cast<bool>(ArduinoOTA.onStartHandler));
    ASSERT_TRUE(static_cast<bool>(ArduinoOTA.onProgressHandler));
    ASSERT_TRUE(static_cast<bool>(ArduinoOTA.onErrorHandler));
}

TEST_F(WebUiFixture, WaitForAnswerConsumesResponsesUntilExpectedText) {
    WiFiClientSecure client;
    g_wifiClientConnected = true;
    g_wifiClientReadLines.push_back("ignore");
    g_wifiClientReadLines.push_back("status: ok");

    EXPECT_TRUE(WebUI::Wait4Answer(client, "status", "ok", 50));
}

TEST_F(WebUiFixture, NotificationSendDispatchesAcrossConfiguredBackends) {
    WebUI::NotificationsService::_started = true;
    WebUI::NotificationsService::_serveraddress = "server";
    WebUI::NotificationsService::_port = 443;
    WebUI::NotificationsService::_token1 = "token1";
    WebUI::NotificationsService::_token2 = "token2";

    g_wifiClientReadLines = {"{\"status\":1}"};
    WebUI::NotificationsService::_notificationType = 1;
    EXPECT_TRUE(WebUI::NotificationsService::sendMSG("Title", "Body"));
    EXPECT_FALSE(g_wifiClientWrites.empty());

    g_wifiClientWrites.clear();
    g_wifiClientReadLines = {"220 ready", "250 ok", "334 login", "334 pass", "235 auth", "250 from", "250 to", "354 data", "250 done", "221 bye"};
    WebUI::NotificationsService::_notificationType = 2;
    EXPECT_TRUE(WebUI::NotificationsService::sendMSG("Title", "Body"));
    EXPECT_GT(g_wifiClientSetInsecureCalls, 0);

    g_wifiClientWrites.clear();
    g_wifiClientReadLines = {"{\"status\":200}"};
    WebUI::NotificationsService::_notificationType = 3;
    EXPECT_TRUE(WebUI::NotificationsService::sendMSG("Title", "Body"));

    g_wifiClientWrites.clear();
    g_wifiClientReadLines = {"{\"ok\":true}"};
    WebUI::NotificationsService::_notificationType = 4;
    EXPECT_TRUE(WebUI::NotificationsService::sendMSG("Title", "Body"));
    EXPECT_EQ(std::string(WebUI::NotificationsService::getTypeString()), "TG");
}
