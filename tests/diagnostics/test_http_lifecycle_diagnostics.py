import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
WEB_HEADER = ROOT / ".pio" / "libdeps" / "wifi" / "ESPAsyncWebServer" / "src" / "ESPAsyncWebServer.h"
WEB_REQUEST = ROOT / ".pio" / "libdeps" / "wifi" / "ESPAsyncWebServer" / "src" / "WebRequest.cpp"
WEB_RESPONSES = ROOT / ".pio" / "libdeps" / "wifi" / "ESPAsyncWebServer" / "src" / "WebResponses.cpp"
ASYNC_TCP = ROOT / ".pio" / "libdeps" / "wifi" / "AsyncTCP" / "src" / "AsyncTCP.cpp"
SYS_STATS = ROOT / "FluidNC" / "esp32" / "SysStats.cpp"
WEB_CLIENT = ROOT / "FluidNC" / "src" / "WebUI" / "WebClient.cpp"


class HttpLifecycleDiagnosticsTest(unittest.TestCase):
    def test_chunked_try_again_keeps_the_inflight_credit_until_bytes_are_queued(self):
        source = WEB_RESPONSES.read_text(encoding="utf-8")
        function = source.split("size_t AsyncAbstractResponse::write_send_buffs", 1)[1].split(
            "size_t AsyncAbstractResponse::_readDataFromCacheOrContent", 1
        )[0]
        send_tail = function.split("// execute sending whatever we have in sock buffs now", 1)[1].split(
            "if (_send_buffer_len == 0)", 1
        )[0]

        self.assertIn("FluidNC zero-byte response retry credit fix", send_tail)
        self.assertIn("if (payloadlen) {", send_tail)
        guarded_credit = send_tail.split("if (payloadlen) {", 1)[1].split("}", 1)[0]
        self.assertIn("_in_flight += payloadlen;", guarded_credit)
        self.assertIn("--_in_flight_credit;", guarded_credit)

        # RESPONSE_TRY_AGAIN queues no body bytes.  With one available credit,
        # a zero-byte producer retry must leave a later poll/ACK able to call
        # the producer again; the first real chunk then consumes that credit.
        credit = 1
        for payloadlen in (0, 1024):
            if payloadlen:
                credit -= 1
        self.assertEqual(credit, 0)

    def test_webclient_realloc_failure_does_not_publish_unowned_capacity(self):
        source = WEB_CLIENT.read_text(encoding="utf-8")
        write = source.split("size_t WebClient::write(const uint8_t* buffer, size_t length)", 1)[1].split(
            "size_t WebClient::write(uint8_t data)", 1
        )[0]

        self.assertIn("const size_t requested_allocsize", write)
        self.assertIn("const size_t capped_required_size", write)
        self.assertIn("required_size < BUFLEN ? required_size : BUFLEN", write)
        self.assertIn("realloc((void*)_buffer, requested_allocsize)", write)
        self.assertLess(write.index("if (!new_buffer)"), write.index("_allocsize = requested_allocsize;"))
        self.assertNotIn("_allocsize       = _allocsize +", write)
        realloc_failure = write.split("if (!new_buffer)", 1)[1].split("_buffer    = new_buffer", 1)[0]
        self.assertLess(
            realloc_failure.index("xSemaphoreGive(xBufferLock);"),
            realloc_failure.index('log_info_to(Console, "Not enough memory!"'),
        )

    def test_webclient_streams_an_oversize_write_instead_of_waiting_forever(self):
        source = WEB_CLIENT.read_text(encoding="utf-8")
        write = source.split("size_t WebClient::write(const uint8_t* buffer, size_t length)", 1)[1].split(
            "size_t WebClient::write(uint8_t data)", 1
        )[0]
        bounded_branch = write.split("if (length > _allocsize)", 1)[1].split("while (_buflen", 1)[0]

        self.assertIn("const size_t chunk_capacity = _allocsize;", bounded_branch)
        self.assertIn("xSemaphoreGive(xBufferLock);", bounded_branch)
        self.assertIn("while (offset < length)", bounded_branch)
        self.assertIn("write(buffer + offset, chunk)", bounded_branch)
        self.assertIn("return length;", bounded_branch)

    def test_request_owner_control_block_has_balanced_allocator_counters(self):
        header = WEB_HEADER.read_text(encoding="utf-8")
        request = WEB_REQUEST.read_text(encoding="utf-8")

        self.assertIn("class AsyncRequestOwnerAllocator", header)
        self.assertIn("async_web_request_owner_allocated", header)
        self.assertIn("async_web_request_owner_deallocated", header)
        self.assertIn("AsyncRequestOwnerAllocator<AsyncWebServerRequest>", header)
        self.assertIn('extern "C" uint32_t async_web_request_owner_allocations()', request)
        self.assertIn('extern "C" uint32_t async_web_request_owner_deallocations()', request)

    def test_async_tcp_event_packets_have_balanced_lifecycle_counters(self):
        source = ASYNC_TCP.read_text(encoding="utf-8")

        self.assertIn("async_event_created.fetch_add", source)
        self.assertIn("async_event_destroyed.fetch_add", source)
        self.assertIn('extern "C" uint32_t async_tcp_event_created()', source)
        self.assertIn('extern "C" uint32_t async_tcp_event_destroyed()', source)
        self.assertIn('extern "C" uint32_t async_tcp_event_queue_depth()', source)
        self.assertIn('extern "C" uint32_t async_tcp_event_queue_high_water()', source)
        self.assertIn('extern "C" uint32_t async_tcp_rx_timeouts()', source)
        self.assertIn('extern "C" uint32_t async_tcp_last_rx_timeout_idle_ms()', source)

    def test_queue_depth_fails_closed_before_mutex_initialization(self):
        source = ASYNC_TCP.read_text(encoding="utf-8")
        function = source.split('extern "C" uint32_t async_tcp_event_queue_depth()', 1)[1].split("}\n", 1)[0]

        self.assertIn("if (_async_queue_mutex == nullptr)", function)
        self.assertIn("return 0;", function)
        self.assertNotIn("_async_queue.size()", function.split("queue_mutex_guard", 1)[0])

    def test_accept_event_allocation_failure_aborts_and_destroys_unpublished_client(self):
        source = ASYNC_TCP.read_text(encoding="utf-8")
        failure = source.split("Couldn't allocate accept event", 1)[1].split("return ERR_ABRT", 1)[0]

        self.assertIn("async_accept_event_allocation_failures.fetch_add", failure)
        self.assertLess(failure.index("_reset_tcp_callbacks(pcb, c);"), failure.index("tcp_abort(pcb);"))
        self.assertLess(failure.index("tcp_abort(pcb);"), failure.index("delete c;"))
        self.assertIn('extern "C" uint32_t async_tcp_accept_event_allocation_failures()', source)

    def test_tcp_accept_admission_rejects_before_client_allocation(self):
        source = ASYNC_TCP.read_text(encoding="utf-8")
        accept = source.split("int8_t AsyncTCP_detail::tcp_accept", 1)[1].split(
            "int8_t AsyncServer::_accepted", 1
        )[0]
        gate = accept.split("AsyncClient *c = new", 1)[0]

        self.assertIn("async_server_accept_admission_rejections.fetch_add", gate)
        self.assertIn("async_server_pending_accepts.load", gate)
        self.assertIn("async_client_created.load", gate)
        self.assertIn("async_client_destroyed.load", gate)
        self.assertIn("ESP.getFreeHeap()", gate)
        self.assertIn("ESP.getMaxAllocHeap()", gate)
        self.assertIn("tcp_abort(pcb);", gate)

        event_destructor = source.split("inline ~lwip_tcp_event_packet_t()", 1)[1].split("};", 1)[0]
        self.assertIn("if (event == LWIP_TCP_ACCEPT)", event_destructor)
        self.assertIn("release_async_server_pending_accept();", event_destructor)

        tail = accept.split('_accept failed: no onConnect callback', 1)[1][:200]
        self.assertIn("tcp_abort(pcb);", tail)
        self.assertIn("return ERR_ABRT;", tail)

    def test_tcp_accept_peak_telemetry_counts_active_and_time_wait_before_accept_work(self):
        source = ASYNC_TCP.read_text(encoding="utf-8")
        accept = source.split("int8_t AsyncTCP_detail::tcp_accept", 1)[1].split(
            "int8_t AsyncServer::_accepted", 1
        )[0]

        self.assertIn('extern "C" uint32_t async_tcp_accept_pcb_active_time_wait_peak()', source)
        self.assertNotIn('extern "C" uint32_t async_tcp_pcb_capacity()', source)
        self.assertIn("_note_async_tcp_pcb_occupancy();", accept)
        self.assertLess(
            accept.index("_note_async_tcp_pcb_occupancy();"),
            accept.index("if (!pcb)"),
        )

        helper = source.split("static void _note_async_tcp_pcb_occupancy()", 1)[1].split(
            "static err_t _async_tcp_pcb_snapshot_api", 1
        )[0]
        self.assertIn("for (tcp_pcb *pcb = tcp_active_pcbs", helper)
        self.assertIn("for (tcp_pcb *pcb = tcp_tw_pcbs", helper)
        self.assertIn("async_tcp_pcb_peak_occupancy_counter.compare_exchange_weak", helper)

    def test_system_stats_exposes_owner_and_event_lifecycle(self):
        source = SYS_STATS.read_text(encoding="utf-8")

        for field in (
            "Web request owners allocated",
            "Web request owners deallocated",
            "Async TCP events created",
            "Async TCP events destroyed",
            "Async TCP event queue depth",
            "Async TCP event queue high water",
            "Async TCP RX timeouts",
            "Async TCP last RX timeout idle ms",
            "Async TCP accept event allocation failures",
            "Async TCP accept admission rejections",
            "Async TCP server pending accepts",
            "Async TCP accept last observed free",
            "Async TCP accept last largest block",
            "Async TCP accept last effective free",
            "Async TCP accept last live clients",
            "TCP accept PCB active+TIME_WAIT peak",
        ):
            with self.subTest(field=field):
                self.assertEqual(source.count(field), 2)


if __name__ == "__main__":
    unittest.main()
