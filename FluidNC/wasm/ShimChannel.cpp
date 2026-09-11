// A second, independent channel for the wasm port: while Console.cpp
// (WasmConsole) drives the interactive xterm.js terminal via Lineedit,
// ShimChannel exists purely for a program to talk to -- specifically, a
// real WebUI build loaded into an iframe alongside the demo terminal,
// bridged over window.postMessage (see demo/index.html) since the WebUI's
// own WebSocket/fetch calls can't reach a WASM instance running in the
// same tab.
//
// Unlike WasmConsole, there is no Lineedit here: whatever sends to this
// channel (WebUI's JS) already assembles complete lines itself, the same
// way it would build an HTTP request body -- so the base Channel class's
// plain accumulate-until-CR/LF (Channel::lineComplete(), unoverridden) and
// default realtimeOkay() (unoverridden, always true) are exactly right,
// matching how UartChannel/WSChannel behave for programmatic senders that
// don't need local echo or intra-line editing.
//
// It's a singleton, not a per-connection object like WSChannel: the demo
// only ever has one iframe and one WASM instance, so there's no PAGEID/
// CURRENT_ID multi-connection bookkeeping to do.

#include <emscripten.h>
#include <deque>
#include <mutex>

#include "Channel.h"
#include "Serial.h"  // allChannels

// write() runs on the FluidNC background thread (its own pthread/Worker),
// where `self` is that worker's own global object, not the page's window --
// plain EM_JS/EM_ASM there would silently write to the wrong `self` and
// never reach the page. MAIN_THREAD_EM_ASM proxies the call to actually run
// on the main thread regardless of which pthread it's invoked from. See
// wasm/Console.cpp's wasm_console_write() for the same pattern.
void wasm_shim_write(const char* buf, int len) {
    MAIN_THREAD_EM_ASM(
        {
            const text = UTF8ToString($0, $1);
            if (typeof self.fluidncOnShimOutput === 'function') {
                self.fluidncOnShimOutput(text);
            }
        },
        buf,
        len);
}

namespace {
class ShimChannel : public Channel {
private:
    std::mutex          _rx_mutex;
    std::deque<uint8_t> _rx_queue;

public:
    ShimChannel() : Channel("ShimChannel", false) {}

    void init() override { allChannels.registration(this); }

    void flushRx() override {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        _rx_queue.clear();
    }

    size_t write(uint8_t c) override {
        char ch = (char)c;
        wasm_shim_write(&ch, 1);
        return 1;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        wasm_shim_write(reinterpret_cast<const char*>(buffer), (int)size);
        return size;
    }

    // Matches UartChannel::out()/out_acked() (see UartChannel.cpp): this
    // channel is a plain line-oriented text stream with no message framing
    // of its own (see the class comment above), so a tagged JSONencoder
    // (e.g. WebCommands.cpp's ESP400/401/420 handlers) needs the same
    // [tag...] bracket-wrapping UART gets, or a multi-line JSON payload is
    // indistinguishable from the ok/error lines that terminate a command.
    // The base Channel::out()/out_acked() ignore `tag` entirely, which is
    // correct for WSChannel/WebClient (real WebSocket/HTTP framing already
    // disambiguates), but wrong here.
    void out(const std::string& s, const char* tag) override { log_stream(*this, "[" << tag << s); }
    void out_acked(const std::string& s, const char* tag) override { log_stream(*this, "[" << tag << s); }

    // Called from wasm_shim_receive(), which runs on the main JS thread;
    // read()/available() run on the FluidNC background thread via
    // Channel::pollLine() -- hence the mutex (same pattern as
    // WasmConsole::receive()).
    void receive(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        for (size_t i = 0; i < len; ++i) {
            _rx_queue.push_back(data[i]);
        }
    }

    int available() override {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        return (int)_rx_queue.size();
    }
    int read() override {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        if (_rx_queue.empty()) {
            return -1;
        }
        uint8_t c = _rx_queue.front();
        _rx_queue.pop_front();
        return c;
    }
    int peek() override {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        return _rx_queue.empty() ? -1 : _rx_queue.front();
    }
};

ShimChannel shimChannel;
}  // namespace

extern "C" {
// Called once from wasm_main.cpp's fluidnc_start(), before the FreeRTOS
// task thread starts -- registration itself only touches AllChannels'
// semaphores/vector, which are safe to use before setup() runs.
void wasm_shim_init() {
    shimChannel.init();
}

// Bridge target for fluidnc_shim_send() (wasm_main.cpp).
void wasm_shim_receive(const uint8_t* data, size_t len) {
    shimChannel.receive(data, len);
}
}
