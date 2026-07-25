// Console channel for the wasm port. Unlike posix/Console.cpp (which owns a
// real tty: termios raw mode, Lineedit-based local echo of raw keystrokes)
// there is no terminal here -- JS delivers complete lines from whatever
// input widget it has, so this just feeds bytes into the Channel's own
// push()-fed queue (the same mechanism a network channel like WSChannel
// uses) and lets the inherited Channel::pollLine()/lineComplete() do line
// assembly and realtime-character interception exactly as it already does
// for every other channel type. No local echo/editing needed on this side.

#include <emscripten.h>

#include "Channel.h"
#include "Serial.h"  // allChannels
#include "Driver/Console.h"

// write() runs on the FluidNC background thread (its own pthread/Worker),
// where `self` is that worker's own global object, not the page's window --
// plain EM_JS/EM_ASM there would silently write to the wrong `self` and
// never reach the page. MAIN_THREAD_EM_ASM proxies the call to actually run
// on the main thread regardless of which pthread it's invoked from.
void wasm_console_write(const char* buf, int len) {
    MAIN_THREAD_EM_ASM(
        {
            const text = UTF8ToString($0, $1);
            if (typeof self.fluidncOnOutput === 'function') {
                self.fluidncOnOutput(text);
            } else {
                console.log(text);
            }
        },
        buf,
        len);
}

namespace {
class WasmConsole : public Channel {
public:
    WasmConsole() : Channel("WasmConsole", false) {}

    void init() override { allChannels.registration(this); }
    void flushRx() override {}

    size_t write(uint8_t c) override {
        char ch = (char)c;
        wasm_console_write(&ch, 1);
        return 1;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        wasm_console_write(reinterpret_cast<const char*>(buffer), (int)size);
        return size;
    }

    // Input arrives via push() (called from fluidnc_send_line(), the JS
    // bridge entry point), not by polling a byte source, so these are
    // intentionally inert -- Channel::pollLine() only calls read() when its
    // own push()-fed queue is empty, which for this channel is always the
    // reason it's empty.
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
};

WasmConsole wasmConsole;
}  // namespace

Channel& Console = wasmConsole;
