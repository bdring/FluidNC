#include "WebUI/OutputUrlHttpPolicy.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL " << message << '\n';
        ++failures;
    }
}

template <typename T, typename U>
void expect_equal(const T& actual, const U& expected, const char* message) {
    if (!(actual == expected)) {
        std::cerr << "FAIL " << message << ": actual=" << actual << " expected=" << expected << '\n';
        ++failures;
    }
}

class FakeClient {
public:
    uint32_t now = 100;
    uint32_t connect_advance_ms = 0;
    uint32_t idle_advance_ms = 1;
    size_t   max_write = 1024;
    bool     connect_result = true;
    bool     fail_write = false;
    bool     throw_on_write = false;
    bool     open = true;
    int      connect_calls = 0;
    int      stop_calls = 0;
    uint32_t connect_timeout = 0;
    std::string response;
    std::string request;
    size_t response_offset = 0;

    uint32_t now_ms() const { return now; }

    void idle() { now += idle_advance_ms; }

    bool connect(const WebUI::OutputUrlHttp::ParsedUrl&, uint32_t timeout_ms) {
        ++connect_calls;
        connect_timeout = timeout_ms;
        now += connect_advance_ms;
        open = connect_result;
        return connect_result;
    }

    size_t write(const uint8_t* data, size_t size, uint32_t) {
        if (throw_on_write) {
            throw std::bad_alloc();
        }
        if (fail_write || !open) {
            return 0;
        }
        const size_t count = size < max_write ? size : max_write;
        request.append(reinterpret_cast<const char*>(data), count);
        return count;
    }

    int available() const { return response_offset < response.size() ? 1 : 0; }

    int read() {
        if (response_offset >= response.size()) {
            return -1;
        }
        return static_cast<unsigned char>(response[response_offset++]);
    }

    bool connected() const { return open; }

    void stop() {
        ++stop_calls;
        open = false;
    }
};
}  // namespace

int main() {
    using namespace WebUI::OutputUrlHttp;

    ParsedUrl parsed {};
    expect(parse_url("http://192.0.2.3/cm?cmnd=Power%20On", parsed), "parse IPv4 HTTP URL");
    expect(!parsed.secure, "HTTP URL is not secure");
    expect_equal(std::string(parsed.host), std::string("192.0.2.3"), "IPv4 host");
    expect_equal(parsed.port, uint16_t(80), "HTTP default port");
    expect_equal(std::string(parsed.target), std::string("/cm?cmnd=Power%20On"), "HTTP target");
    expect_equal(std::string(parsed.authority), std::string("192.0.2.3"), "default authority omits port");

    expect(parse_url("https://relay.example:8443?power=off", parsed), "parse HTTPS custom port");
    expect(parsed.secure, "HTTPS URL is secure");
    expect_equal(parsed.port, uint16_t(8443), "custom HTTPS port");
    expect_equal(std::string(parsed.target), std::string("/?power=off"), "query-only target gets slash");
    expect_equal(std::string(parsed.authority), std::string("relay.example:8443"), "custom authority includes port");

    expect(!parse_url(nullptr, parsed), "reject null URL");
    expect(!parse_url("ftp://relay/power", parsed), "reject unsupported scheme");
    expect(!parse_url("http:///power", parsed), "reject empty host");
    expect(!parse_url("http://user:pass@relay/power", parsed), "reject userinfo");
    expect(!parse_url("http://relay/power#fragment", parsed), "reject fragments");
    expect(!parse_url("http://relay:0/power", parsed), "reject zero port");
    expect(!parse_url("http://relay:65536/power", parsed), "reject oversized port");
    expect(!parse_url("http://relay:abc/power", parsed), "reject nonnumeric port");
    expect(!parse_url("http://relay/power\r\nX-Evil: yes", parsed), "reject header injection");
    expect(!parse_url("http://[::1]/power", parsed), "reject unsupported IPv6 literal");

    uint16_t status = 0;
    expect(parse_status_line("HTTP/1.1 204 No Content", status), "parse HTTP 1.1 status");
    expect_equal(status, uint16_t(204), "HTTP 204 status");
    expect(parse_status_line("HTTP/1.0 503 Service Unavailable", status), "parse HTTP 1.0 status");
    expect_equal(status, uint16_t(503), "HTTP 503 status");
    expect(!parse_status_line("", status), "reject empty status line without overread");
    expect(!parse_status_line("ICY 200 OK", status), "reject non-HTTP status line");
    expect(!parse_status_line("HTTP/1.1 20 OK", status), "reject short status code");
    expect(!parse_status_line("HTTP/2 200 OK", status), "reject unsupported HTTP version token");
    expect_equal(secure_phase_timeout_seconds(5000), uint32_t(2), "TLS splits five-second deadline into two-second phases");
    expect_equal(secure_phase_timeout_seconds(1999), uint32_t(0), "TLS rejects deadline too short for two phases");
    expect(2u * secure_phase_timeout_seconds(9999) * 1000u <= 9999u, "TLS phase budgets never exceed global deadline");

    expect(parse_url("http://relay.local/power/off?token=static", parsed), "parse executable URL");
    FakeClient ok;
    ok.max_write = 7;
    ok.response = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
    status = 0;
    expect(perform_get_noexcept(ok, parsed, 5000, &status), "HTTP 503 is transport success");
    expect_equal(status, uint16_t(503), "transport returns final HTTP status");
    expect_equal(ok.connect_calls, 1, "exactly one connection attempt");
    expect_equal(ok.stop_calls, 1, "socket closed after success");
    expect(ok.request.find("GET /power/off?token=static HTTP/1.1\r\n") == 0, "exact GET request line");
    expect(ok.request.find("Host: relay.local\r\n") != std::string::npos, "exact Host header");
    expect(ok.request.find("Connection: close\r\n") != std::string::npos, "connection-close header");

    FakeClient interim;
    interim.response = "HTTP/1.1 100 Continue\r\nX-Interim: yes\r\n\r\nHTTP/1.1 204 No Content\r\n\r\n";
    status = 0;
    expect(perform_get_noexcept(interim, parsed, 5000, &status), "skip bounded interim response");
    expect_equal(status, uint16_t(204), "return final status after interim response");
    expect_equal(interim.stop_calls, 1, "socket closed after interim response");

    FakeClient connect_fail;
    connect_fail.connect_result = false;
    status = 999;
    expect(!perform_get_noexcept(connect_fail, parsed, 5000, &status), "connect failure is transport failure");
    expect_equal(status, uint16_t(0), "connect failure clears status");
    expect_equal(connect_fail.stop_calls, 1, "socket closed after connect failure");

    FakeClient deadline;
    deadline.response.clear();
    deadline.idle_advance_ms = 11;
    status = 999;
    expect(!perform_get_noexcept(deadline, parsed, 25, &status), "response wait obeys global deadline");
    expect_equal(status, uint16_t(0), "deadline failure clears status");
    expect(deadline.now - 100 <= 33, "deadline has at most one polling quantum overshoot");
    expect_equal(deadline.stop_calls, 1, "socket closed after deadline");

    FakeClient thrower;
    thrower.throw_on_write = true;
    status = 999;
    expect(!perform_get_noexcept(thrower, parsed, 5000, &status), "OOM is contained at no-throw boundary");
    expect_equal(status, uint16_t(0), "OOM clears status");
    expect_equal(thrower.stop_calls, 1, "socket closed after OOM");

    FakeClient expired_connect;
    expired_connect.connect_advance_ms = 100;
    expired_connect.response = "HTTP/1.1 200 OK\r\n\r\n";
    status = 999;
    expect(!perform_get_noexcept(expired_connect, parsed, 50, &status), "connect consuming deadline aborts before write");
    expect(expired_connect.request.empty(), "no request sent after connect deadline");
    expect_equal(expired_connect.stop_calls, 1, "socket closed after expired connect");

    if (failures) {
        std::cerr << failures << " output URL HTTP policy test(s) failed\n";
        return 1;
    }
    std::cout << "output URL HTTP policy tests passed\n";
    return 0;
}
