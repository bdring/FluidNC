import re
from xmodem import XMODEM
import os
from tool.utils import remote_file_sha256, file_stream_sha256, color
import fnmatch


def parse_file(fixture_path):
    with open(fixture_path, "r") as f:
        op_entries = []
        for lineno, line in enumerate(f.read().splitlines()):
            if line == "":
                # skip empty lines
                continue

            if line.startswith("#"):
                # skip comment lines
                continue

            op = line.split(" ")[0]
            if op in OPS_MAP:
                ctor = OPS_MAP[op]
                line = line[len(op) + 1 :]
                if (
                    len(op_entries) > 0
                    and op_entries[-1].__class__.data_is_multi_line
                    and op_entries[-1].op == op
                ):
                    # append to previous group of matches
                    op_entries[-1].data.append(line)
                else:
                    op_entry = ctor(op, line, lineno + 1, fixture_path)
                    op_entries.append(op_entry)
            else:
                raise ValueError(
                    f"Invalid op '{op}' at line {lineno} in fixture file {fixture_path}: {line}"
                )

    return op_entries


class OpEntry:
    # When parsing the fixtures file, this is set to True if the data for this type of
    # operation spans multiple lines
    data_is_multi_line = False

    def __init__(self, op, data, lineno, fixture_path):
        self.op = op
        self.data = data
        self.lineno = lineno
        self.fixture_path = fixture_path

    def __str__(self):
        return f"{self.__class__.__name__}({self.op}, {str(self.data)}, {self.lineno})"

    def __repr__(self):
        return str(self)

    def execute(self, controller):
        raise NotImplementedError

    def _op_str(self):
        return color.dark_grey(self.op) + " "


class IgnoreLineOpEntry(OpEntry):
    def execute(self, controller):
        print(self._op_str() + color.dark_grey(self.data, dark=True, bold=True))
        controller.ignore_line(self.data)
        return True


class SendLineOpEntry(OpEntry):
    def execute(self, controller):
        print(self._op_str() + color.sent_line(self.data))
        controller.send_line(self.data)
        return True


class StringMatchOpEntry(OpEntry):
    def __init__(self, op, data, lineno, fixture_path):
        super().__init__(op, data, lineno, fixture_path)
        self.optional = op == "<~"

    def execute(self, controller):
        # optional unconditionally matches all lines
        line = controller.current_line()
        matches = self.data == line
        if matches:
            print(self._op_str() + color.received_line(line))
            controller.clear_line()
        elif self.optional and not matches:
            print(self._op_str() + color.dark_grey(self.data, dark=True, bold=True))
        else:
            print(color.error("Expected: ") + self.data)
            print(color.error("Actual  : ") + line)
            return False
        return True


class AnyStringMatchOpEntry(OpEntry):
    data_is_multi_line = True

    def __init__(self, op, data, lineno, fixture_path):
        super().__init__(op, [data], lineno, fixture_path)

    def execute(self, controller):
        line = controller.current_line()
        if line not in self.data:
            print(color.error("Expected one of: "))
            for fline in self.data:
                print(f" -      `{color.error(fline)}'")
            print(color.error("Actual:   ") + line)
            return False
        else:
            print(self._op_str() + color.received_line(line))
            controller.clear_line()
            return True


class UntilStringMatchOpEntry(OpEntry):
    def __init__(self, op, data, lineno, fixture_path):
        self._glob_match = data.startswith("* ")
        super().__init__(op, data.removeprefix("* "), lineno, fixture_path)

    def execute(self, controller):
        while True:
            line = controller.current_line()
            matches = self._line_matches(line)

            opstr = self._op_str()
            if self._glob_match:
                opstr += color.dark_grey("* ", bold=True)

            print(opstr + color.green(line, dark=True, bold=matches))
            controller.clear_line()
            if matches:
                break
        return True

    def _line_matches(self, line):
        if self._glob_match:
            return fnmatch.fnmatch(line, self.data)
        else:
            return self.data == line


class SendFileOpEntry(OpEntry):
    def __init__(self, op, data, lineno, fixture_path):
        super().__init__(op, data, lineno, fixture_path)
        data = data.split(" ")
        # make local file path relative to the fixture file
        self.local_file_path = os.path.normpath(
            os.path.join(os.path.dirname(fixture_path), data[0])
        )
        self.remote_file_path = data[1]

        # validate the local path exists
        if not os.path.exists(self.local_file_path):
            raise ValueError(
                f"Local file '{self.local_file_path}' does not exist at line {lineno} in fixture file {fixture_path}"
            )

    def execute(self, controller):
        with open(self.local_file_path, "rb") as file_stream:
            remote_sha256 = remote_file_sha256(controller, self.remote_file_path)
            local_sha256 = file_stream_sha256(file_stream)

            print(
                self._op_str()
                + color.green(self.local_file_path)
                + color.dark_grey(" => ")
                + color.green(self.remote_file_path),
                end=" ",
            )
            if remote_sha256 == local_sha256:
                print(color.dark_grey(f"(up-to-date, hash={local_sha256[:8]})"))
                return True
            elif remote_sha256 is None:
                print(color.green("(file does not exist)"))
            else:
                print(
                    color.green(
                        f"(file changed, "
                        f"local:{local_sha256[:8]} != remote:{remote_sha256[:8]}f"
                        f")",
                    )
                )

            controller.send_line(f"$XModem/Receive={remote_file}")
            while True:
                # wait for the 'C' character to start the transfer
                controller.timeout = 2
                c = controller.read(1)
                if c == b"C":
                    break
                if c == b"":
                    raise TimeoutError(
                        f"XModem start timeout at line {lineno} in fixture file {fixture_file}"
                    )
                controller.timeout = 1
            xmodem = XMODEM(controller.getc, controller.putc)
            xmodem.send(file_stream)
            rx_ack_line = controller.next_line()
            controller.clear_line()
            print(self._op_str() + color.received_line(rx_ack_line))
            matcher = re.match(
                r"\[MSG:INFO: Received (\d+) bytes to file ([\w\/\.]+)\]",
                rx_ack_line,
            )
            if matcher is None:
                raise ValueError(
                    f"Transfer failed (ack line): {rx_ack_line} at line {lineno} in fixture file {fixture_file}"
                )
            num_tx_bytes = int(matcher.group(1))
            name_tx_file = matcher.group(2)
            if name_tx_file != remote_file:
                print(f"Expected: {remote_file}")
                print(f"Actual: {name_tx_file}")
                raise ValueError(
                    f"Transfer failed (filename mismatch): {rx_ack_line} at line {lineno} in fixture file {fixture_file}"
                )
            print(
                self._op_str()
                + color.green(local_file, bold=True)
                + color.dark_grey(" => ")
                + color.green(remote_file, bold=True)
                + color.green(f" ({num_tx_bytes} bytes)")
            )
            return True


OPS_MAP = {
    # ignores messages consisting of only the following line (e.g. 'ok')
    "ignore": IgnoreLineOpEntry,
    # send command to controller
    "->": SendLineOpEntry,
    # send file to controller
    "=>": SendFileOpEntry,
    # expect from controller
    "<-": StringMatchOpEntry,
    # expect from controller, but optional
    "<~": StringMatchOpEntry,
    # consume lines until line is found
    "<...": UntilStringMatchOpEntry,
    # expect one of
    "<|": AnyStringMatchOpEntry,
}
