import unittest
import importlib.util
from pathlib import Path


ROOT = Path(__file__).parents[2]
POLICY = ROOT / "FluidNC" / "src" / "WebUI" / "WebResourcePolicy.h"
SERVER = ROOT / "FluidNC" / "src" / "WebUI" / "WebUIServer.cpp"
WS_CHANNEL = ROOT / "FluidNC" / "src" / "WebUI" / "WSChannel.cpp"
STATS = ROOT / "FluidNC" / "esp32" / "SysStats.cpp"
VERIFIER = ROOT / "tests" / "diagnostics" / "verify_debug_firmware.py"
VERIFIER_SPEC = importlib.util.spec_from_file_location("verify_debug_firmware", VERIFIER)
VERIFIER_MODULE = importlib.util.module_from_spec(VERIFIER_SPEC)
assert VERIFIER_SPEC and VERIFIER_SPEC.loader
VERIFIER_SPEC.loader.exec_module(VERIFIER_MODULE)


class WebResourceDiagnosticsTest(unittest.TestCase):
    def test_websocket_output_never_retains_a_raw_library_client(self):
        source = WS_CHANNEL.read_text(encoding="utf-8")
        write = source.split("size_t WSChannel::write(const uint8_t* buffer, size_t size)", 1)[1].split(
            "bool WSChannel::sendTXT", 1
        )[0]
        send_text = source.split("bool WSChannel::sendTXT", 1)[1].split("WSChannel::~WSChannel", 1)[0]

        for output_path in (write, send_text):
            self.assertNotIn("get_client(", output_path)
            self.assertNotIn("client->", output_path)
            self.assertIn("close_client_by_id(_server, _clientNum)", output_path)

        self.assertIn("_server->queueLength(_clientNum, queue_length)", write)
        self.assertIn("_server->binary(_clientNum, out, outlen)", write)
        self.assertIn("_server->text(_clientNum, s.data(), s.length())", send_text)
        append_guard = write.split("_output_line.append", 1)[0].rsplit("try {", 1)
        self.assertEqual(len(append_guard), 2)
        append_failure = write.split("_output_line.append", 1)[1].split("if (!complete_line)", 1)[0]
        self.assertIn("catch (const std::bad_alloc&)", append_failure)
        self.assertIn("close_client_by_id(_server, _clientNum)", append_failure)

    def test_runtime_snapshot_exposes_slots_and_rejection_reasons(self):
        policy = POLICY.read_text(encoding="utf-8")
        server = SERVER.read_text(encoding="utf-8")
        stats = STATS.read_text(encoding="utf-8")

        for field in (
            "pendingWebSockets",
            "activeWebSockets",
            "connectingWebSockets",
            "deferredWebSocketCloses",
            "activeFileStreams",
            "activeHeavyHttpResponses",
            "webSocketClientLimitRejections",
            "webSocketHeapRejections",
            "fileStreamStarts",
            "fileStreamCompletions",
            "fileStreamRejections",
            "heavyHttpRejections",
            "lastWebSocketObservedFree",
            "lastWebSocketLargestBlock",
            "lastWebSocketEffectiveFree",
            "lastWebSocketOccupiedSlots",
        ):
            with self.subTest(field=field):
                self.assertIn(field, policy)
                self.assertIn(field, stats)

        self.assertIn("RuntimeSnapshot runtime_snapshot()", server)
        self.assertIn("WebResourceLock lock;", server[server.index("RuntimeSnapshot runtime_snapshot()") :])
        self.assertGreaterEqual(stats.count("#ifdef ENABLE_WS_CHANNEL_PINS"), 3)
        self.assertIn("increment_saturating", server)
        for counter in (
            "websocket_client_limit_rejections",
            "websocket_heap_rejections",
            "file_stream_starts",
            "file_stream_completions",
            "file_stream_rejections",
            "heavy_http_rejections",
        ):
                self.assertGreaterEqual(server.count(counter), 3)

    def test_handshake_admission_attributes_bad_alloc_to_the_heap_reject_counter(self):
        server = SERVER.read_text(encoding="utf-8")
        handshake = server.split("_socket_server->handleHandshake([](AsyncWebServerRequest* request) {", 1)[1].split(
            "        });\n        _socket_server->onEvent(", 1
        )[0]

        self.assertIn("catch (const std::bad_alloc&)", handshake)
        catch_body = handshake.split("catch (const std::bad_alloc&)", 1)[1].split("}", 1)[0]
        self.assertIn("WebResourceLock lock;", catch_body)
        self.assertIn("increment_saturating(websocket_heap_rejections);", catch_body)
        self.assertLess(
            catch_body.index("WebResourceLock lock;"),
            catch_body.index("increment_saturating(websocket_heap_rejections);"),
        )
        self.assertIn("return false;", catch_body)

    def test_artifact_verifier_binds_web_resource_sources_objects_and_symbols(self):
        verifier = VERIFIER.read_text(encoding="utf-8")

        self.assertIn("def verify_web_resource_hardening(", verifier)
        self.assertIn("verify_web_resource_hardening(args.root.resolve()", verifier)
        for marker in (
            '"WebClient.cpp.o"',
            '"WSChannel.cpp.o"',
            '"AsyncWebSocket.cpp.o"',
            '"WebUIServer.cpp.o"',
            '"SysStats.cpp.o"',
            '"Channel.cpp.o"',
            '"WebResourcePolicy.h"',
            '"WebUI::WebClient::executeCommandBackground(char const*)"',
            '"WebUI::WebClients::background_task(void*)"',
            '"WebUI::WebClient::~WebClient()"',
            '"AsyncWebSocket::queueLength(unsigned int, unsigned int&)"',
            '"FluidNC resource-pressure handshake rejection"',
            '"WebUI::ResourcePolicy::runtime_snapshot()"',
            '"Channel::~Channel()"',
            '"channel_queue_mutex_created"',
            '"channel_queue_mutex_destroyed"',
            '"platform_sys_stats(JSONencoder&)"',
            '"Web heavy HTTP active"',
            '"Web file starts"',
            '"Web file completions"',
            '"Channel queue mutexes created"',
            '"Channel queue mutexes destroyed"',
            'build / "firmware.bin"',
            "firmware.stat().st_mtime_ns >= max(",
            "firmware_bin.stat().st_mtime_ns >= firmware.stat().st_mtime_ns",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, verifier)

    def test_tcp_listener_pre_accept_diagnostics_are_exposed_and_bound(self):
        stats = STATS.read_text(encoding="utf-8")
        verifier = VERIFIER.read_text(encoding="utf-8")

        for label in (
            "Async TCP accept callbacks",
            "Async TCP accept null PCBs",
            "Async TCP accept last null PCB error",
            "Async TCP accept client allocation failures",
            "Async TCP accept client setup failures",
            "TCP accept PCB active+TIME_WAIT peak",
            "TCP listener backlog",
            "TCP listener accepts pending",
        ):
            with self.subTest(label=label):
                self.assertGreaterEqual(stats.count(label), 2)
                self.assertIn(label, verifier)

        for symbol in (
            "async_tcp_accept_callbacks",
            "async_tcp_accept_null_pcbs",
            "async_tcp_accept_last_null_pcb_error",
            "async_tcp_accept_client_allocation_failures",
            "async_tcp_accept_client_setup_failures",
            "async_tcp_accept_pcb_active_time_wait_peak",
        ):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, verifier)

    def test_websocket_reject_abort_latency_is_exposed_and_artifact_bound(self):
        stats = STATS.read_text(encoding="utf-8")
        verifier = VERIFIER.read_text(encoding="utf-8")

        for label in (
            "Async WebSocket reject abort calls",
            "Async WebSocket reject abort max us",
        ):
            with self.subTest(label=label):
                self.assertGreaterEqual(stats.count(label), 2)
                self.assertIn(label, verifier)

        for symbol in (
            "async_web_resource_reject_abort_calls",
            "async_web_resource_reject_abort_max_us",
        ):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, verifier)

    def test_websocket_reject_abort_verifier_rejects_each_missing_measurement_step(self):
        markers = VERIFIER_MODULE.REJECT_ABORT_TIMING_MARKERS
        complete = "\n".join(markers)
        VERIFIER_MODULE.require_ordered_markers(complete, markers, "test reject-abort timing")

        for marker in markers:
            with self.subTest(marker=marker):
                with self.assertRaisesRegex(AssertionError, "reject-abort timing"):
                    VERIFIER_MODULE.require_ordered_markers(
                        complete.replace(marker, "", 1), markers, "test reject-abort timing"
                    )


if __name__ == "__main__":
    unittest.main()
