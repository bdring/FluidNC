import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[2] / "tools" / "patch_espasyncwebserver_diagnostics.py"
SPEC = importlib.util.spec_from_file_location("espasync_diagnostics_patch", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(MODULE)


class EspAsyncDiagnosticsPatchTest(unittest.TestCase):
    def test_patch_is_strict_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            header = Path(temp_dir) / "ESPAsyncWebServer.h"
            request = Path(temp_dir) / "WebRequest.cpp"
            response = Path(temp_dir) / "WebResponses.cpp"
            header.write_text("\n\n".join(old for old, _ in MODULE.HEADER_PATCHES), encoding="utf-8")
            request.write_text("\n\n".join(old for old, _ in MODULE.REQUEST_PATCHES), encoding="utf-8")
            response.write_text("\n\n".join(old for old, _ in MODULE.RESPONSE_PATCHES), encoding="utf-8")

            self.assertTrue(MODULE.patch_sources(header, request, response))
            self.assertIn(MODULE.HEADER_MARKER, header.read_text(encoding="utf-8"))
            self.assertIn("std::bad_array_new_length", header.read_text(encoding="utf-8"))
            self.assertIn(MODULE.REQUEST_MARKER, request.read_text(encoding="utf-8"))
            response_text = response.read_text(encoding="utf-8")
            self.assertIn(MODULE.RESPONSE_MARKER, response_text)
            self.assertIn("if (payloadlen) {", response_text)
            self.assertFalse(MODULE.patch_sources(header, request, response))

    def test_unknown_upstream_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            header = Path(temp_dir) / "ESPAsyncWebServer.h"
            request = Path(temp_dir) / "WebRequest.cpp"
            response = Path(temp_dir) / "WebResponses.cpp"
            header.write_text("unknown header\n", encoding="utf-8")
            request.write_text("unknown request\n", encoding="utf-8")
            response.write_text("unknown response\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "Unsupported ESPAsyncWebServer"):
                MODULE.patch_sources(header, request, response)


if __name__ == "__main__":
    unittest.main()
