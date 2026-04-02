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
#include "WebUI/OTA.h"
#undef protected
#undef private

#include "ArduinoOTA.h"
#include "Stage1HostSupport.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "mdns.h"

namespace {
void resetWebUiHarness() {
    Stage1HostSupport::resetWebUiState();

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
    WebUI::OTA module("ota");

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
