import importlib.util
from pathlib import Path


TOOL = Path(__file__).parents[2] / "tools" / "stream_fluidnc_firmware.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("stream_fluidnc_firmware", TOOL)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_multipart_prefix_matches_the_live_webui_firmware_contract():
    tool = load_tool()

    prefix = tool.build_multipart_prefix(
        boundary="boundary",
        firmware_name="debug firmware.bin",
        firmware_size=1234,
    )

    assert prefix == (
        b'--boundary\r\n'
        b'Content-Disposition: form-data; name="path"\r\n\r\n'
        b'/\r\n'
        b'--boundary\r\n'
        b'Content-Disposition: form-data; name="createPath"\r\n\r\n'
        b'true\r\n'
        b'--boundary\r\n'
        b'Content-Disposition: form-data; name="/debug firmware.binS"\r\n\r\n'
        b'1234\r\n'
        b'--boundary\r\n'
        b'Content-Disposition: form-data; name="myfile[]"; '
        b'filename="/debug firmware.bin"\r\n'
        b'Content-Type: application/octet-stream\r\n\r\n'
    )


def test_multipart_prefix_can_reproduce_live_board_webui_alias():
    tool = load_tool()

    prefix = tool.build_multipart_prefix(
        boundary="boundary",
        firmware_name="firmware.bin",
        firmware_size=1234,
        file_field_name="myfiles",
    )

    assert b'name="myfiles"; filename="/firmware.bin"\r\n' in prefix


def test_upload_waits_for_server_response_without_half_closing_request():
    tool = load_tool()

    class FakeSocket:
        def __init__(self):
            self.sent = bytearray()
            self.responses = [b"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n", b"3", b""]

        def send(self, payload):
            self.sent.extend(payload)
            return len(payload)

        def recv(self, _size):
            return self.responses.pop(0)

        def shutdown(self, _direction):
            raise AssertionError("a Content-Length request must stay open for the HTTP response")

    sock = FakeSocket()
    sent_file, response = tool.transmit_firmware(
        sock,
        request=b"request",
        prefix=b"prefix",
        firmware_chunks=[b"firm", b"ware"],
        suffix=b"suffix",
        delay_ms=0,
    )

    assert sent_file == 8
    assert sock.sent == b"requestprefixfirmwaresuffix"
    assert response == b"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n3"
