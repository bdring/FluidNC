// Validation spike for the FluidNC WASM port.
//
// Purpose: prove, before touching any real FluidNC source, that:
//  1. emcc can compile+link a small translation unit with -pthread.
//  2. A pthread actually starts and runs (Emscripten pthreads need
//     COOP/COEP + SharedArrayBuffer; if the page isn't served
//     cross-origin-isolated, thread creation fails at runtime, not
//     link time -- so this needs a live browser round-trip, not just
//     a clean build, to be a real signal).
//  3. JS can call into WASM and get a value back (the same shape the
//     real bridge will use: JS passes a command line in, C code hands
//     a response line back), without emulating a filesystem/terminal.

#include <emscripten.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
std::atomic<int> counter{ 0 };
std::thread       worker;

void worker_loop() {
    // Stand-in for a FluidNC background task (e.g. the polling/motion
    // tasks in Protocol.cpp): just prove a second thread of execution
    // is really running concurrently with the main one.
    for (int i = 0; i < 5; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void spike_start_thread() {
    if (!worker.joinable()) {
        worker = std::thread(worker_loop);
    }
}

EMSCRIPTEN_KEEPALIVE
int spike_thread_counter() {
    return counter.load(std::memory_order_relaxed);
}

// Stands in for the future JS<->WASM command bridge: JS passes a line
// of input, gets a line of output back, no sockets/filesystem/terminal
// involved -- same shape execute_line()/report status will eventually use.
EMSCRIPTEN_KEEPALIVE
const char* spike_handle_line(const char* line) {
    static char response[128];
    if (line != nullptr && std::strcmp(line, "?") == 0) {
        std::snprintf(response, sizeof(response), "<Idle|WPos:0.000,0.000,0.000|FS:0,0>");
    } else {
        std::snprintf(response, sizeof(response), "error: unrecognized line '%s'", line ? line : "(null)");
    }
    return response;
}

}  // extern "C"
