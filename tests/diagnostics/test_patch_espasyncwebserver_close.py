import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[2] / "tools" / "patch_espasyncwebserver_close.py"
SPEC = importlib.util.spec_from_file_location("espasync_close_patch", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(MODULE)


class EspAsyncClosePatchTest(unittest.TestCase):
    def test_patch_is_strict_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncWebSocket.cpp"
            header = Path(temp_dir) / "AsyncWebSocket.h"
            source.write_text(
                "prefix\n#include <algorithm>\n#include <cstdio>\nusing namespace asyncsrv;\n"
                + MODULE.ORIGINAL
                + MODULE.HANDSHAKE_REJECT_ORIGINAL
                + """void AsyncWebSocket::close(uint32_t id, uint16_t code, const char *message) {
  asyncsrv::lock_guard_type lock(_ws_clients_lock);
  if (AsyncWebSocketClient *c = client(id)) {
    c->close(code, message);
  }
}

bool AsyncWebSocket::availableForWrite(uint32_t id) {
  asyncsrv::lock_guard_type lock(_ws_clients_lock);
  const auto iter = std::find_if(std::begin(_clients), std::end(_clients), [id](const AsyncWebSocketClient &c) {
    return c.id() == id;
  });
  if (iter == std::end(_clients)) {
    return true;
  }
  return !iter->queueIsFull();
}

"""
                + "suffix\n",
                encoding="utf-8",
            )
            header.write_text(
                """prefix
  bool availableForWriteAll();
  bool availableForWrite(uint32_t id);

  size_t count() const;
  void close(uint32_t id, uint16_t code = 0, const char *message = NULL);
  void closeAll(uint16_t code = 0, const char *message = NULL);
suffix
""",
                encoding="utf-8",
            )

            self.assertTrue(MODULE.patch_source(source))
            patched = source.read_text(encoding="utf-8")
            patched_header = header.read_text(encoding="utf-8")
            self.assertIn("FluidNC resource-pressure retry fix", patched)
            self.assertIn("_status = WS_CONNECTED;\n    throw;", patched)
            self.assertIn("bool AsyncWebSocket::abort(uint32_t id)", patched)
            self.assertIn("bool AsyncWebSocket::queueLength(uint32_t id, size_t &length)", patched)
            self.assertIn("FluidNC resource-pressure handshake rejection", patched)
            self.assertIn("request->abort();", patched)
            self.assertIn(MODULE.HANDSHAKE_REJECT_TIMING_MARKER, patched)
            self.assertIn("async_web_resource_reject_abort_calls", patched)
            self.assertIn("async_web_resource_reject_abort_max_us", patched)
            self.assertNotIn(MODULE.HANDSHAKE_REJECT_ORIGINAL, patched)
            self.assertIn("bool abort(uint32_t id);", patched_header)
            self.assertIn("bool queueLength(uint32_t id, size_t &length);", patched_header)
            self.assertNotIn(MODULE.ORIGINAL, patched)
            self.assertFalse(MODULE.patch_source(source))

    def test_unknown_upstream_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "AsyncWebSocket.cpp"
            source.write_text("different upstream implementation\n", encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "Unsupported ESPAsyncWebServer"):
                MODULE.patch_source(source)


if __name__ == "__main__":
    unittest.main()
