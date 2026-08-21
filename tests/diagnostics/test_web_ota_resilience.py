import re
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
HEADER = ROOT / "FluidNC" / "src" / "WebUI" / "WebUIServer.h"
SOURCE = ROOT / "FluidNC" / "src" / "WebUI" / "WebUIServer.cpp"
SYS_STATS = ROOT / "FluidNC" / "esp32" / "SysStats.cpp"


class WebOtaResilienceTest(unittest.TestCase):
    def setUp(self):
        self.header = HEADER.read_text(encoding="utf-8")
        self.source = SOURCE.read_text(encoding="utf-8")
        self.stats = SYS_STATS.read_text(encoding="utf-8")

    def test_upload_has_a_single_request_owner(self):
        self.assertIn("_firmware_upload_request", self.header)
        self.assertIn("WebUI_Server::_firmware_upload_request", self.source)
        self.assertRegex(
            self.source,
            r"_firmware_upload_request\s*!=\s*nullptr\s*&&\s*_firmware_upload_request\s*!=\s*request",
        )
        self.assertIn("request->abort()", self.source)

    def test_upload_uses_exact_posted_file_size(self):
        self.assertIn("hasParam(sizeargname.c_str(), true)", self.source)
        self.assertIn("getParam(sizeargname.c_str(), true)", self.source)
        self.assertNotIn('request->hasHeader("Content-Length")', self._upload_function())
        self.assertIn("Update.begin(maxSketchSpace, U_FLASH)", self.source)

    def test_firmware_route_is_post_only_and_auth_failures_do_not_clobber_an_owner(self):
        self.assertIn('_webserver->on("/updatefw", HTTP_POST, handleUpdate, WebUpdateUpload)', self.source)

        handler_start = self.source.index("void WebUI_Server::handleUpdate")
        handler_end = self.source.index("#ifdef HAVE_UPDATE", handler_start)
        handler = self.source[handler_start:handler_end]
        auth_end = handler.index("//if success restart")
        self.assertNotIn("_upload_status =", handler[:auth_end])

        upload = self._upload_function()
        auth_end = upload.index("} else {")
        auth_branch = upload[:auth_end]
        self.assertIn("_firmware_upload_request == request", auth_branch)
        self.assertIn("request->abort()", auth_branch)
        self.assertIn("return;", auth_branch)

    def test_upload_disconnect_aborts_global_updater(self):
        upload = self._upload_function()
        self.assertIn("request->onDisconnect([request]()", upload)
        self.assertIn("_firmware_upload_request == request", upload)
        self.assertIn("firmware_ota_update_started.load", upload)
        self.assertIn("Update.isRunning()", upload)
        self.assertIn("Update.abort()", upload)
        self.assertIn("_firmware_upload_request = nullptr", upload)

    def test_disconnect_handler_registration_fails_closed(self):
        upload = self._upload_function()
        registration = upload.index("request->onDisconnect([request]()")
        owner = upload.index("_firmware_upload_request = request")
        self.assertLess(registration, owner)
        self.assertRegex(
            upload,
            r"(?s)try\s*\{\s*request->onDisconnect\(\[request\]\(\).*?"
            r"\}\s*catch\s*\(const std::bad_alloc&\).*?request->abort\(\);\s*return;",
        )

    def test_upload_rx_timeout_is_scoped_and_bounded(self):
        match = re.search(r"firmwareUploadRxTimeoutSeconds\s*=\s*(\d+)", self.source)
        self.assertIsNotNone(match)
        self.assertGreater(int(match.group(1)), 3)
        self.assertLessEqual(int(match.group(1)), 30)
        self.assertIn("request->client()->setRxTimeout(firmwareUploadRxTimeoutSeconds)", self._upload_function())

    def test_finalization_rejects_truncated_image(self):
        upload = self._upload_function()
        self.assertIn("index + len == maxSketchSpace", upload)
        self.assertIn("Update.end(false)", upload)
        self.assertNotIn("Update.end(true)", upload)

    def test_upload_lifecycle_exposes_bounded_diagnostics(self):
        for symbol in (
            "fluidnc_ota_active",
            "fluidnc_ota_expected_bytes",
            "fluidnc_ota_accepted_bytes",
            "fluidnc_ota_max_write_us",
            "fluidnc_ota_disconnect_aborts",
            "fluidnc_ota_failures",
            "fluidnc_ota_update_owned",
        ):
            with self.subTest(symbol=symbol):
                self.assertIn(f'extern "C" uint32_t {symbol}()', self.source)

        for field in (
            "Firmware OTA active",
            "Firmware OTA expected bytes",
            "Firmware OTA accepted bytes",
            "Firmware OTA max write us",
            "Firmware OTA disconnect aborts",
            "Firmware OTA failures",
            "Firmware OTA updater owned",
        ):
            with self.subTest(field=field):
                self.assertEqual(self.stats.count(field), 2)

    def _upload_function(self):
        start = self.source.index("void WebUI_Server::WebUpdateUpload")
        end = self.source.index("#endif", start)
        return self.source[start:end]


if __name__ == "__main__":
    unittest.main()
