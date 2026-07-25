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

// Bridge for JS to deliver a complete line of input (a full command, not
// raw keystrokes -- there's no terminal here for FluidNC to locally echo
// against, unlike posix/Console.cpp's Lineedit). Feeds the same push()-fed
// queue every other Channel type (e.g. a network channel) already uses, so
// Channel::pollLine()'s line assembly and realtime-character interception
// apply unchanged.
EMSCRIPTEN_KEEPALIVE
void fluidnc_send_line(const char* line) {
    if (line == nullptr) {
        return;
    }
    Console.push(std::string(line) + "\n");
}

}  // extern "C"
