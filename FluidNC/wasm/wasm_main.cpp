// JS-driven entry point for the wasm port, replacing capture/main.cpp's
// classic blocking `main(); while(!should_exit()) loop();` (which this env
// deliberately excludes -- see platformio.ini). setup()/loop() together
// never return except on exit (loop() calls protocol_main_loop(), which
// itself only returns on should_exit()), so running that chain on the
// thread a JS call arrived on would freeze the page. Instead,
// fluidnc_start() spawns exactly that chain on its own std::thread/pthread
// (matching how xTaskCreate already spawns real threads inside setup()
// itself: the polling/output tasks in Protocol.cpp) and returns to JS
// immediately.

#include <emscripten.h>
#include <cstring>
#include <string>
#include <thread>

#include "Channel.h"
#include "Driver/Console.h"
#include "Platform.h"  // should_exit()

extern "C" {
void setup();
void loop();
}

namespace {
std::thread fluidnc_thread;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void fluidnc_start() {
    if (fluidnc_thread.joinable()) {
        return;  // already started
    }
    fluidnc_thread = std::thread([]() {
        setup();
        while (!should_exit()) {
            loop();
        }
    });
    fluidnc_thread.detach();
}

// wasm/Console.cpp's receiver: feeds a queue that WasmConsole's own
// read()/available() drain, rather than Channel::push() -- see the
// comment atop wasm/Console.cpp for why push() specifically doesn't work
// here (it would let Lineedit's realtimeOkay() veto be bypassed).
void wasm_console_receive(const uint8_t* data, size_t len);

// Bridge for JS to deliver raw input bytes exactly as typed/pasted (e.g.
// from an xterm.js onData callback) -- unlike a plain line-oriented input
// box, no newline is appended here: Console's Lineedit does its own local
// echo, intra-line editing, and realtime-character interception the same
// way it would from a real serial terminal, one byte at a time.
EMSCRIPTEN_KEEPALIVE
void fluidnc_send_text(const char* text) {
    if (text == nullptr) {
        return;
    }
    wasm_console_receive(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
}

}  // extern "C"
