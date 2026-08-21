import unittest
from pathlib import Path


SOURCE = Path(__file__).parents[2] / "FluidNC" / "esp32" / "SysStats.cpp"


class HeapDiagnosticsTest(unittest.TestCase):
    def test_system_stats_bind_live_soak_to_exact_hardening_build_and_boot(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn('"Diagnostic hardening ID"', source)
        self.assertIn("DebugRecovery::diagnostic_hardening_id", source)
        self.assertIn('"Diagnostic boot sequence"', source)
        self.assertIn("DebugRecovery::current_boot_sequence()", source)
        self.assertIn('"Diagnostic uptime ms"', source)
        self.assertIn('"Diagnostic reset reason"', source)

    def test_system_stats_reports_largest_contiguous_heap_block(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("#include <esp_heap_caps.h>", source)
        self.assertEqual(source.count("Largest free block"), 2)
        self.assertEqual(
            source.count("formatBytes(heapInfo.largest_free_block)"),
            2,
            "JSON and text ESP420 responses must expose the same sampled value",
        )
        self.assertEqual(source.count("formatBytes(heapInfo.total_free_bytes)"), 2)

    def test_system_stats_distinguishes_retention_from_fragmentation(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertEqual(source.count("multi_heap_info_t heapInfo {}"), 2)
        self.assertEqual(
            source.count("heap_caps_get_info(&heapInfo, MALLOC_CAP_8BIT)"),
            2,
            "JSON and text ESP420 responses must sample allocator state",
        )
        for field in (
            "Heap allocated bytes",
            "Heap free blocks",
            "Heap allocated blocks",
            "Heap total blocks",
            "Heap minimum free",
        ):
            with self.subTest(field=field):
                self.assertEqual(source.count(field), 2)


if __name__ == "__main__":
    unittest.main()
