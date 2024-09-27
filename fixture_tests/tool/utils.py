import hashlib
import re
import json
from termcolor import colored


def remote_file_sha256(controller, remote_path):
    remote_path = remote_path.removeprefix("/littlefs")
    controller.send_line(f"$File/ShowHash={remote_path}")
    json_response = ""
    while True:
        line = controller.next_line()
        if line == "":
            raise TimeoutError("Timeout waiting for controller")
        if line == "ok":
            break
        if line.startswith("error:"):
            raise ValueError(f"Error from controller: {line}")
        matcher = re.match(r"\[JSON:(.+)\]", line)
        if matcher is None:
            raise ValueError(f"Invalid response from controller: {line}")
        json_response += matcher.group(1)

    json_response = json.loads(json_response)
    signature = json_response["signature"]
    if signature["algorithm"] != "SHA2-256":
        raise ValueError(f"Unsupported algorithm: {signature['algorithm']}")

    if signature["value"] != "":
        return signature["value"].lower()
    else:
        return None


def file_stream_sha256(file_stream):
    file_stream.seek(0)
    sha256 = hashlib.sha256()
    while True:
        data = file_stream.read(4096)
        if not data:
            break
        sha256.update(data)
    file_stream.seek(0)
    return sha256.hexdigest().lower()


class ColorHelper:
    def green(self, s, **kwargs):
        return self._impl(s, "green", **kwargs)

    def red(self, s, **kwargs):
        return self._impl(s, "red", **kwargs)

    def dark_grey(self, s, **kwargs):
        return self._impl(s, "dark_grey", **kwargs)

    def received_line(self, s):
        return self.green(s)

    def sent_line(self, s):
        return self.green(s, dark=True)

    def error(self, s):
        return self.red(s)

    def _impl(self, s, color, dark=False, bold=False):
        attrs = []
        if dark:
            attrs.append("dark")
        if bold:
            attrs.append("bold")
        return colored(s, color, attrs=attrs)


color = ColorHelper()
