import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[2] / "tools" / "patch_asynctcp_diagnostics.py"
SPEC = importlib.util.spec_from_file_location("asynctcp_diagnostics_patch", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(MODULE)


def asynctcp_fixture(fragments):
    return (
        "\n\n".join(fragments)
        + "\n\n"
        + MODULE.R17_ACCEPT_ENTRY_BLOCK
        + "\n\n"
        + MODULE.R17_CLIENT_SETUP_FAILURE_BLOCK
        + "\n\n"
        + MODULE.R17_CLIENT_ALLOCATION_FAILURE_BLOCK
    )


class AsyncTcpDiagnosticsPatchTest(unittest.TestCase):
    def test_patch_is_strict_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            original = asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES)
            source.write_text(original, encoding="utf-8")

            self.assertTrue(MODULE.patch_source(source))
            patched = source.read_text(encoding="utf-8")
            self.assertIn('extern "C" uint32_t async_tcp_event_queue_high_water()', patched)
            self.assertIn("async_rx_timeout_count.fetch_add", patched)
            self.assertIn("async_tcp_pcb_snapshot", patched)
            self.assertIn('extern "C" uint32_t async_tcp_accept_event_allocation_failures()', patched)
            self.assertIn("async_accept_event_allocation_failures.fetch_add", patched)
            cleanup = patched.split("Couldn't allocate accept event", 1)[1].split("return ERR_ABRT", 1)[0]
            self.assertLess(cleanup.index("_reset_tcp_callbacks(pcb, c);"), cleanup.index("tcp_abort(pcb);"))
            self.assertLess(cleanup.index("tcp_abort(pcb);"), cleanup.index("delete c;"))
            self.assertFalse(MODULE.patch_source(source))

    def test_unknown_or_partial_upstream_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text("different upstream implementation\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "Unsupported AsyncTCP"):
                MODULE.patch_source(source)

            partial = asynctcp_fixture(
                patched if index == 0 else original
                for index, (original, patched) in enumerate(MODULE.PATCHES)
            )
            source.write_text(partial, encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "Partially patched AsyncTCP"):
                MODULE.patch_source(source)

    def test_exact_previous_patch_is_upgraded_once(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            previous_patch_count = 10
            previous = asynctcp_fixture(
                patched if index < previous_patch_count else original
                for index, (original, patched) in enumerate(MODULE.PATCHES)
            ).replace(MODULE.CURRENT_COUNTER_BLOCK, MODULE.R11_COUNTER_BLOCK, 1).replace(
                MODULE.CURRENT_ACCEPT_FAILURE_BLOCK, MODULE.R11_ACCEPT_FAILURE_BLOCK, 1
            ).replace(
                MODULE.CURRENT_EVENT_PACKET_BLOCK, MODULE.R11_EVENT_PACKET_BLOCK, 1
            )
            source.write_text(previous, encoding="utf-8")

            self.assertTrue(MODULE.patch_source(source))
            upgraded = source.read_text(encoding="utf-8")
            self.assertIn("async_tcp_accept_event_allocation_failures", upgraded)
            self.assertIn("_reset_tcp_callbacks(pcb, c);", upgraded)
            self.assertIn("delete c;", upgraded)
            self.assertFalse(MODULE.patch_source(source))

    def test_exact_r12_patch_is_upgraded_with_early_rst_ownership(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            previous_patch_count = 12
            previous = asynctcp_fixture(
                patched if index < previous_patch_count else original
                for index, (original, patched) in enumerate(MODULE.PATCHES)
            ).replace(MODULE.CURRENT_COUNTER_BLOCK, MODULE.R12_COUNTER_BLOCK, 1)
            source.write_text(previous, encoding="utf-8")

            self.assertTrue(MODULE.patch_source(source))
            upgraded = source.read_text(encoding="utf-8")
            self.assertIn("bool *removed_accept = nullptr", upgraded)
            self.assertIn("bool removed_unpublished_accept = false", upgraded)
            self.assertIn("async_accept_rst_before_dispatch_cleanups.fetch_add", upgraded)
            self.assertIn("delete client;", upgraded)
            self.assertFalse(MODULE.patch_source(source))

    def test_accept_admission_is_preallocation_bounded_and_balances_pending(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")

            MODULE.patch_source(source)
            patched = source.read_text(encoding="utf-8")
            accept = patched.split("auto server = reinterpret_cast<AsyncServer *>(arg);", 1)[1]
            gate = accept.split("AsyncClient *c = new", 1)[0]

            self.assertIn("ASYNC_SERVER_MAX_CLIENTS = 8", gate)
            self.assertIn("ASYNC_SERVER_PENDING_RESERVATION = 7 * 1024", gate)
            self.assertIn("ASYNC_SERVER_FIRST_CLIENT_FLOOR = 24 * 1024", gate)
            self.assertIn("ASYNC_SERVER_ADDITIONAL_CLIENT_FLOOR = 32 * 1024", gate)
            self.assertIn("ASYNC_SERVER_LARGEST_BLOCK_FLOOR = 20 * 1024", gate)
            self.assertIn("async_server_accept_admission_rejections.fetch_add", gate)
            self.assertIn("tcp_abort(pcb);", gate)
            self.assertIn("return ERR_ABRT;", gate)
            self.assertIn("async_server_pending_accepts.fetch_add", accept)
            failure = patched.split("Couldn't allocate accept event", 1)[1].split("return ERR_ABRT", 1)[0]
            self.assertIn("release_async_server_pending_accept();", failure)
            event_destructor = patched.split("inline ~lwip_tcp_event_packet_t()", 1)[1].split("};", 1)[0]
            self.assertIn("if (event == LWIP_TCP_ACCEPT)", event_destructor)
            self.assertIn("release_async_server_pending_accept();", event_destructor)
            tail = patched.split('_accept failed: no onConnect callback', 1)[1][:200]
            self.assertIn("tcp_abort(pcb);", tail)
            self.assertIn("return ERR_ABRT;", tail)

    def test_accept_path_reports_pre_client_failures_and_listener_backlog(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")

            MODULE.patch_source(source)
            patched = source.read_text(encoding="utf-8")

            self.assertIn("async_accept_callbacks_count.fetch_add", patched)
            self.assertIn("async_accept_null_pcb_count.fetch_add", patched)
            self.assertIn("async_accept_last_null_pcb_error_value.store", patched)
            self.assertIn("async_accept_client_allocation_failure_count.fetch_add", patched)
            self.assertIn("async_accept_client_setup_failure_count.fetch_add", patched)

            snapshot = patched.split("typedef struct {\n  struct tcpip_api_call_data call;", 1)[1].split(
                "typedef struct {", 1
            )[0]
            self.assertIn("uint32_t listen_backlog;", snapshot)
            self.assertIn("uint32_t listen_accepts_pending;", snapshot)
            self.assertIn("snapshot->listen_backlog += pcb->backlog;", snapshot)
            self.assertIn("snapshot->listen_accepts_pending += pcb->accepts_pending;", snapshot)
            self.assertIn("uint32_t *listen_backlog", patched)
            self.assertIn("uint32_t *listen_accepts_pending", patched)

    def test_exact_r17_diagnostics_upgrade_is_idempotent(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")
            self.assertTrue(MODULE.patch_source(source))
            r19 = source.read_text(encoding="utf-8")
            r17 = (
                r19.replace(MODULE.CURRENT_COUNTER_BLOCK, MODULE.R17_COUNTER_BLOCK, 1)
                .replace(MODULE.CURRENT_PCB_SNAPSHOT_BLOCK, MODULE.R17_PCB_SNAPSHOT_BLOCK, 1)
                .replace(MODULE.CURRENT_ACCEPT_ENTRY_BLOCK, MODULE.R17_ACCEPT_ENTRY_BLOCK, 1)
                .replace(MODULE.CURRENT_CLIENT_SETUP_FAILURE_BLOCK, MODULE.R17_CLIENT_SETUP_FAILURE_BLOCK, 1)
                .replace(MODULE.CURRENT_CLIENT_ALLOCATION_FAILURE_BLOCK, MODULE.R17_CLIENT_ALLOCATION_FAILURE_BLOCK, 1)
            )
            source.write_text(r17, encoding="utf-8")

            self.assertTrue(MODULE.patch_source(source))
            upgraded = source.read_text(encoding="utf-8")
            self.assertIn("async_accept_callbacks_count.fetch_add", upgraded)
            self.assertIn("snapshot->listen_accepts_pending += pcb->accepts_pending;", upgraded)
            self.assertFalse(MODULE.patch_source(source))

    def test_exact_r20_diagnostics_upgrade_adds_tcp_pcb_peak_once(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")
            self.assertTrue(MODULE.patch_source(source))
            r21 = source.read_text(encoding="utf-8")
            r20 = (
                r21.replace(MODULE.CURRENT_COUNTER_BLOCK, MODULE.R20_COUNTER_BLOCK, 1)
                .replace(MODULE.CURRENT_PCB_SNAPSHOT_BLOCK, MODULE.R20_PCB_SNAPSHOT_BLOCK, 1)
                .replace(MODULE.CURRENT_ACCEPT_ENTRY_BLOCK, MODULE.R20_ACCEPT_ENTRY_BLOCK, 1)
            )
            source.write_text(r20, encoding="utf-8")

            self.assertTrue(MODULE.patch_source(source))
            upgraded = source.read_text(encoding="utf-8")
            self.assertIn("async_tcp_pcb_peak_occupancy_counter.compare_exchange_weak", upgraded)
            self.assertIn("_note_async_tcp_pcb_occupancy();", upgraded)
            self.assertIn('extern "C" uint32_t async_tcp_accept_pcb_active_time_wait_peak()', upgraded)
            self.assertFalse(MODULE.patch_source(source))

    def test_missing_r21_tcp_pcb_peak_marker_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")
            self.assertTrue(MODULE.patch_source(source))
            broken = source.read_text(encoding="utf-8").replace(
                "async_tcp_pcb_peak_occupancy_counter.compare_exchange_weak",
                "async_tcp_pcb_peak_occupancy_counter.compare_exchange_weak_missing",
                1,
            )
            source.write_text(broken, encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "Unsupported AsyncTCP"):
                MODULE.patch_source(source)

    def test_exact_r21_diagnostics_upgrade_removes_nominal_capacity_claim_once(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")
            self.assertTrue(MODULE.patch_source(source))
            r22 = source.read_text(encoding="utf-8")
            r21 = (
                r22.replace(MODULE.CURRENT_COUNTER_BLOCK, MODULE.R21_COUNTER_BLOCK, 1)
                .replace(MODULE.CURRENT_PCB_SNAPSHOT_BLOCK, MODULE.R21_PCB_SNAPSHOT_BLOCK, 1)
            )
            source.write_text(r21, encoding="utf-8")

            self.assertTrue(MODULE.patch_source(source))
            upgraded = source.read_text(encoding="utf-8")
            self.assertIn('extern "C" uint32_t async_tcp_accept_pcb_active_time_wait_peak()', upgraded)
            self.assertNotIn('extern "C" uint32_t async_tcp_pcb_capacity()', upgraded)
            self.assertFalse(MODULE.patch_source(source))

    def test_missing_pre_client_instrumentation_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")
            self.assertTrue(MODULE.patch_source(source))
            patched = source.read_text(encoding="utf-8")

            for marker in (
                "async_accept_callbacks_count.fetch_add",
                "async_accept_null_pcb_count.fetch_add",
                "async_accept_last_null_pcb_error_value.store",
                "async_accept_client_allocation_failure_count.fetch_add",
                "async_accept_client_setup_failure_count.fetch_add",
            ):
                with self.subTest(marker=marker):
                    broken = patched.replace(marker, marker + "_missing", 1)
                    source.write_text(broken, encoding="utf-8")
                    with self.assertRaisesRegex(RuntimeError, "Unsupported AsyncTCP r17 diagnostics upgrade"):
                        MODULE.patch_source(source)

    def test_exact_unqualified_r19_diagnostics_upgrade_is_idempotent(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")
            self.assertTrue(MODULE.patch_source(source))
            r19 = source.read_text(encoding="utf-8")
            unqualified = (
                r19.replace(MODULE.CURRENT_COUNTER_BLOCK, MODULE.R19_UNQUALIFIED_COUNTER_BLOCK, 1)
                .replace(MODULE.CURRENT_ACCEPT_ENTRY_BLOCK, MODULE.R19_UNQUALIFIED_ACCEPT_ENTRY_BLOCK, 1)
                .replace(
                    MODULE.CURRENT_CLIENT_SETUP_FAILURE_BLOCK,
                    MODULE.R19_UNQUALIFIED_CLIENT_SETUP_FAILURE_BLOCK,
                    1,
                )
                .replace(
                    MODULE.CURRENT_CLIENT_ALLOCATION_FAILURE_BLOCK,
                    MODULE.R19_UNQUALIFIED_CLIENT_ALLOCATION_FAILURE_BLOCK,
                    1,
                )
            )
            source.write_text(unqualified, encoding="utf-8")

            self.assertTrue(MODULE.patch_source(source))
            repaired = source.read_text(encoding="utf-8")
            self.assertIn("async_accept_callbacks_count.fetch_add", repaired)
            self.assertNotIn("_note_async_tcp_pcb_occupancy();", MODULE.R19_UNQUALIFIED_ACCEPT_ENTRY_BLOCK)
            self.assertIn("_note_async_tcp_pcb_occupancy();", repaired)
            self.assertFalse(MODULE.patch_source(source))

    def test_early_rst_destroys_unpublished_accept_client(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncTCP.cpp"
            source.write_text(asynctcp_fixture(fragment for fragment, _ in MODULE.PATCHES), encoding="utf-8")

            MODULE.patch_source(source)
            patched = source.read_text(encoding="utf-8")
            remover = patched.split("static size_t _remove_events_for_client", 1)[1].split("};", 1)[0]
            self.assertIn("bool *removed_accept = nullptr", remover)
            self.assertIn("t->event == LWIP_TCP_ACCEPT", remover)
            self.assertIn("*removed_accept = true", remover)

            error_path = patched.split("void AsyncTCP_detail::tcp_error", 1)[1].split(
                "static void _tcp_dns_found", 1
            )[0]
            self.assertIn("bool removed_unpublished_accept = false", error_path)
            self.assertIn("_remove_events_for_client(client, &removed_unpublished_accept)", error_path)
            self.assertIn("if (removed_unpublished_accept)", error_path)
            self.assertIn("async_accept_rst_before_dispatch_cleanups.fetch_add", error_path)
            cleanup = error_path.split("if (removed_unpublished_accept)", 1)[1].split("}", 1)[0]
            self.assertIn("delete client;", cleanup)
            self.assertIn("return;", cleanup)
            self.assertLess(error_path.index("client->_pcb = nullptr"), error_path.index("delete client;"))
            self.assertLess(error_path.index("delete client;"), error_path.index("LWIP_TCP_ERROR"))


if __name__ == "__main__":
    unittest.main()
