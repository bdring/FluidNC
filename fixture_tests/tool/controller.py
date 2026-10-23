from collections import defaultdict, deque

from tool.grbl_message import ASYNC_NOISE_TYPES, MessageType, classify, parse_msg
from tool.transport import Transport, create_transport


class GrblFault(Exception):
    """Raised by Controller.expect() when the DUT reports a real fault
    (error:N or a bare ALARM:N) that the caller wasn't specifically
    waiting for -- see the module docstring in tool/grbl_message.py for
    why this is not just skipped over like routine async chatter."""

    def __init__(self, message):
        super().__init__(message.raw)
        self.message = message


class Controller:
    def __init__(self, transport_or_device, baudrate=115200, timeout=1.0):
        self._debug = False
        # Accept either an already-constructed Transport, or (for backward
        # compatibility with existing callers) a DUT spec string that gets
        # resolved through create_transport() -- see tool/transport/__init__.py.
        if isinstance(transport_or_device, Transport):
            self._transport = transport_or_device
        else:
            self._transport = create_transport(
                transport_or_device, baudrate=baudrate, timeout=timeout
            )
        self._current_line = None
        # Recent lines of each message type that expect() has skipped past
        # while looking for something else -- not needed for a fixture to
        # pass, but handy for a human debugging a failure to see what async
        # chatter (status reports, [MSG:...], etc.) was actually going by.
        self._recent = defaultdict(lambda: deque(maxlen=5))
        # command -> handler(arguments: str), dispatched from expect() for
        # every [MSG:<command>: ...] line it sees -- see on_msg().
        self._msg_handlers = {}

    def on_msg(self, command, handler):
        """Register `handler(arguments)` to run whenever a
        `[MSG:<command>: ...]` line is seen by expect() -- including ones
        it's transparently skipping past while a fixture step waits for
        something else entirely. Mirrors GrblParserC.h's overridable
        `handle_msg(command, arguments)` hook, driven here from inside
        expect()'s classify step instead of a dedicated poll function.

        A fixture that wants to observe e.g. `[MSG:PWM: gpio.2,2500]`
        lines emitted as a side effect of some motion doesn't need to
        explicitly wait for them at all: register the callback once, then
        run the motion normally through `<-`/`<~` fixture entries, and
        the callback fires as those entries' expect() calls skip past the
        interleaved PWM chatter on their way to whatever they're actually
        waiting for (an `ok`, a status report, ...).
        """
        self._msg_handlers[command] = handler

    def _dispatch_msg(self, msg):
        command, arguments = parse_msg(msg.body)
        handler = self._msg_handlers.get(command)
        if handler is not None:
            handler(arguments)

    def send_soft_reset(self):
        self._transport.write(b"\x18")
        self.clear_line()
        # wait for startup message
        while not self.current_line().startswith("Grbl "):
            self.clear_line()
        self.clear_line()

    def current_line(self):
        if self._current_line is None:
            line = self._transport.readline()
            self._current_line = (line or b"").decode("utf-8", errors="replace").strip()
            # print(colored("[c] <- " + self._current_line, "light_blue"))
        return self._current_line

    def clear_line(self):
        self._current_line = None

    def next_line(self):
        self.clear_line()
        return self.current_line()

    def recent(self, message_type):
        """Recent lines of `message_type` that expect() skipped past as
        noise -- for debugging a fixture failure, not for assertions."""
        return list(self._recent[message_type])

    def expect(self, want_types, skip_types=ASYNC_NOISE_TYPES):
        """Read (and classify) lines, transparently discarding any whose
        type is in `skip_types` and isn't one of `want_types`, until a
        line of a wanted type arrives -- or a genuine fault (error/alarm
        the caller wasn't waiting for) or an unrecognized-but-not-skippable
        line does, in which case that's returned/raised instead so a real
        mismatch is never silently swallowed. See tool/grbl_message.py.

        Like current_line(), the returned message is left "peeked": the
        caller decides whether to consume it via clear_line() (a real
        match) or leave it cached (an optional match that didn't pan out,
        e.g. a `<~` fixture entry -- the same line must still be visible
        to whatever fixture entry runs next).

        Returns None on timeout (nothing arrived at all).
        """
        if isinstance(want_types, MessageType):
            want_types = {want_types}

        while True:
            raw = self.current_line()
            if raw == "":
                # Ambiguous with a genuine blank line from the DUT, but
                # that ambiguity predates this method -- current_line()
                # already collapsed "timed out" and "read an empty line"
                # to the same "" (see its docstring-free implementation
                # above; pyserial's readline() does the same on timeout).
                return None

            msg = classify(raw)

            if msg.type == MessageType.MSG:
                self._dispatch_msg(msg)

            if msg.type in want_types:
                return msg

            if msg.type in (MessageType.ERROR, MessageType.ALARM):
                # Not in want_types (that case already returned above): a
                # real fault the caller wasn't expecting right now.
                raise GrblFault(msg)

            if msg.type in skip_types:
                self._recent[msg.type].append(msg)
                self.clear_line()
                continue

            # Not wanted, not a fault, not routine chatter we know is safe
            # to skip -- surface it so the caller can report the mismatch
            # rather than silently discarding something unrecognized.
            return msg

    def send_line(self, line):
        # print(colored("[c] -> " + line, "light_blue"))
        self._transport.write(line.encode("utf-8") + b"\n")

    def getc(self, size):
        return self._transport.read(size)

    def putc(self, data):
        self._transport.write(data)
        return len(data)

    def drain(self, wait_for=0.1):
        while self._transport.readline(timeout=wait_for) is not None:
            pass

    def close(self):
        self._transport.close()
