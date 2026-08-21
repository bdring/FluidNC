import unittest
from pathlib import Path


SOURCE = Path(__file__).parents[2] / "FluidNC" / "src" / "WebUI" / "WebUIServer.cpp"
FLUID_PATH_HEADER = Path(__file__).parents[2] / "FluidNC" / "src" / "FluidPath.h"
WEB_CLIENT_HEADER = Path(__file__).parents[2] / "FluidNC" / "src" / "WebUI" / "WebClient.h"
WEB_CLIENT_SOURCE = Path(__file__).parents[2] / "FluidNC" / "src" / "WebUI" / "WebClient.cpp"
SERIAL_HEADER = Path(__file__).parents[2] / "FluidNC" / "src" / "Serial.h"
SERIAL_SOURCE = Path(__file__).parents[2] / "FluidNC" / "src" / "Serial.cpp"


class WebUiServerResourceGuardTest(unittest.TestCase):
    def test_static_file_slot_is_reserved_before_allocating_or_hashing(self):
        source = SOURCE.read_text(encoding="utf-8")
        start = source.index("bool WebUI_Server::myStreamFile(")
        end = source.index("void WebUI_Server::sendWithOurAddress", start)
        body = source[start:end]

        reservation = body.index("FileStreamReservation reservation;")
        for unguarded_operation in (
            "FluidPath       fpath",
            'request->hasHeader("Accept-Encoding")',
            "HashFS::hash(",
            "std::filesystem::path gzpath",
        ):
            with self.subTest(operation=unguarded_operation):
                self.assertLess(
                    reservation,
                    body.index(unguarded_operation),
                    f"{unguarded_operation} must run only while a static-file slot is reserved",
                )

        self.assertEqual(
            body.count("FileStreamReservation reservation;"),
            1,
            "the same reservation must cover hashing and the streamed response",
        )

    def test_not_found_handler_does_not_copy_url_before_the_guard(self):
        source = SOURCE.read_text(encoding="utf-8")
        start = source.index("void WebUI_Server::handle_not_found(")
        end = source.index("uint32_t WebUI_Server::getPageid", start)
        before_stream_guard = source[start : source.index("myStreamFile(", start, end)]

        self.assertNotIn("std::string path", before_stream_guard)
        self.assertIn('const auto& path = request->url();', before_stream_guard)

    def test_error_code_fluid_path_constructor_can_propagate_bad_alloc(self):
        header = FLUID_PATH_HEADER.read_text(encoding="utf-8")
        declaration = "FluidPath(const std::string_view name, const Volume& fs, std::error_code& ec)"
        start = header.index(declaration)
        end = header.index("{}", start)

        self.assertNotIn(
            "noexcept",
            header[start:end],
            "the constructor allocates through canonPath and must not terminate on bad_alloc",
        )

    def test_heavy_http_slot_is_owned_by_webclient_until_reap(self):
        header = WEB_CLIENT_HEADER.read_text(encoding="utf-8")
        client = WEB_CLIENT_SOURCE.read_text(encoding="utf-8")
        server = SOURCE.read_text(encoding="utf-8")
        start = server.index("void WebUI_Server::synchronousCommand(")
        end = server.index("std::string getSession(", start)
        body = server[start:end]

        self.assertIn("using ResourceRelease = void (*)();", header)
        self.assertIn("explicit WebClient(ResourceRelease resourceRelease = nullptr);", header)
        self.assertRegex(header, r"ResourceRelease\s+_resourceRelease")
        self.assertIn("WebClient::WebClient(ResourceRelease resourceRelease)", client)
        self.assertIn("_resourceRelease(resourceRelease)", client)
        self.assertIn("releaseResource();", client[client.index("WebClient::~WebClient()") :])

        admission = body.index("HeavyHttpReservation reservation")
        construction = body.index("new WebClient(")
        disconnect_owner = body.index("request->onDisconnect")
        queue = body.index("executeCommandBackground")
        send = body.index("request->send(rawResponse)")
        self.assertLess(admission, construction)
        self.assertLess(construction, disconnect_owner)
        self.assertLess(disconnect_owner, queue)
        self.assertLess(queue, send)
        self.assertIn("reservation.transfer();", body[construction:disconnect_owner])
        self.assertIn("catch (const std::bad_alloc&)", body)

    def test_background_queue_full_is_reported_to_the_request_owner(self):
        header = WEB_CLIENT_HEADER.read_text(encoding="utf-8")
        client = WEB_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool executeCommandBackground(const char* cmd);", header)
        start = client.index("bool WebClient::executeCommandBackground(")
        end = client.index("size_t WebClient::write(", start)
        body = client[start:end]
        self.assertIn("xQueueSend", body)
        self.assertIn("== pdTRUE", body)
        self.assertIn("if (!queued)", body)
        self.assertIn("return queued;", body)

    def test_missing_background_runtime_fails_closed_without_sync_execution(self):
        client = WEB_CLIENT_SOURCE.read_text(encoding="utf-8")
        start = client.index("bool WebClient::executeCommandBackground(")
        end = client.index("void WebClient::cancelPendingCommand()", start)
        body = client[start:end]

        owners_missing = body.index("if (!WebClients::_background_task_queue")
        queue_send = body.index("xQueueSend", owners_missing)
        self.assertNotIn(
            "settings_execute_line",
            body[owners_missing:queue_send],
            "an unavailable worker must fail closed, never execute in AsyncTCP",
        )
        self.assertIn("done = true;", body[owners_missing:queue_send])
        self.assertIn("return false;", body[owners_missing:queue_send])

    def test_webclient_constructor_fails_closed_when_freertos_owners_are_unavailable(self):
        client = WEB_CLIENT_SOURCE.read_text(encoding="utf-8")
        constructor_start = client.index("WebClient::WebClient(ResourceRelease resourceRelease)")
        destructor_start = client.index("WebClient::~WebClient()", constructor_start)
        constructor = client[constructor_start:destructor_start]
        destructor_end = client.index("void WebClient::releaseResource()", destructor_start)
        destructor = client[destructor_start:destructor_end]

        self.assertIn("if (!xBufferLock)", constructor)
        self.assertIn("if (!WebClients::_background_task_queue)", constructor)
        self.assertIn("if (!WebClients::_background_task_handle)", constructor)
        self.assertIn("taskCreated != pdPASS", constructor)
        self.assertGreaterEqual(constructor.count("throw std::bad_alloc"), 2)

        null_lock = destructor.index("if (!xBufferLock)")
        release = destructor.index("releaseResource();")
        early_return = destructor.index("return;", null_lock)
        self.assertLess(release, early_return)

    def test_channel_reap_queue_full_is_deferred_without_losing_webclient_owner(self):
        serial_header = SERIAL_HEADER.read_text(encoding="utf-8")
        serial_source = SERIAL_SOURCE.read_text(encoding="utf-8")
        server = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool kill(Channel* channel);", serial_header)
        kill_start = serial_source.index("bool AllChannels::kill(Channel* channel)")
        kill_end = serial_source.index("void AllChannels::registration", kill_start)
        kill_body = serial_source[kill_start:kill_end]
        self.assertIn("xQueueSend", kill_body)
        self.assertIn("== pdTRUE", kill_body)

        self.assertIn("deferred_webclient_kills", server)
        self.assertIn("schedule_deferred_webclient_kill", server)
        self.assertIn("process_deferred_webclient_kills", server)
        self.assertIn("if (!allChannels.kill(clientOwner)", server)
        poll_start = server.index("void WebUI_Server::poll()")
        self.assertIn("process_deferred_webclient_kills();", server[poll_start:])


if __name__ == "__main__":
    unittest.main()
