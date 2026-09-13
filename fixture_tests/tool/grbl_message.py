"""Classifies a single Grbl/FluidNC protocol line by message type.

Ported from the dispatch order in GrblParserC's `parse_report()`
(https://github.com/MitchBradley/GrblParser, src/GrblParserC.c) -- that
library exists specifically because "a GCode listener must be prepared to
receive almost anything at any time, dispatching based on message type"
(no assumption about response ordering holds on any transport, serial
included). This only ports the *classification* step (which type of line
is this, and what's its unwrapped body), not the full field-level parsing
(status report DRO/limits/overrides, [GC:] modal state, etc.) -- fixtures
only need to know what kind of line they're looking at.

Dispatch order matters and matches GrblParserC.c exactly: e.g. a line has
to fail the "ok" and "<...>" checks before "[GC:...]" is tried, etc.
"""

from dataclasses import dataclass
from enum import Enum, auto


class MessageType(Enum):
    OK = auto()
    ERROR = auto()  # "error:N"
    ALARM = auto()  # bare "ALARM:N" (NOT the same as a "[MSG:...ALARM...]" line, which is MSG)
    STATUS = auto()  # "<...>"
    GC = auto()  # "[GC:...]"
    MSG = auto()  # "[MSG:...]"
    JSON = auto()  # "[JSON:...]"
    VER = auto()  # "[VER:...]"
    PRB = auto()  # "[PRB:...]"
    SIGNON = auto()  # "Grbl ..." startup banner
    OTHER = auto()  # anything not otherwise recognized


@dataclass(frozen=True)
class GrblMessage:
    type: MessageType
    body: str  # content with the type's wrapper (if any) stripped
    raw: str  # the original, unmodified line


def _unwrap(line: str, prefix: str, suffix: str = ""):
    """None if `line` doesn't start with `prefix`; otherwise the body with
    `prefix` (and `suffix`, if present at the end) removed -- mirrors
    GrblParserC.c's is_report_type()."""
    if not line.startswith(prefix):
        return None
    body = line[len(prefix) :]
    if suffix and body.endswith(suffix):
        body = body[: -len(suffix)]
    return body


def classify(line: str) -> GrblMessage:
    if line == "ok":
        return GrblMessage(MessageType.OK, "", line)

    body = _unwrap(line, "<", ">")
    if body is not None:
        return GrblMessage(MessageType.STATUS, body, line)

    body = _unwrap(line, "[GC:", "]")
    if body is not None:
        return GrblMessage(MessageType.GC, body, line)

    body = _unwrap(line, "[MSG:", "]")
    if body is not None:
        return GrblMessage(MessageType.MSG, body, line)

    body = _unwrap(line, "[JSON:", "]")
    if body is not None:
        return GrblMessage(MessageType.JSON, body, line)

    body = _unwrap(line, "[VER:", "]")
    if body is not None:
        return GrblMessage(MessageType.VER, body, line)

    body = _unwrap(line, "error:")
    if body is not None:
        return GrblMessage(MessageType.ERROR, body, line)

    body = _unwrap(line, "ALARM:")
    if body is not None:
        return GrblMessage(MessageType.ALARM, body, line)

    body = _unwrap(line, "Grbl ")
    if body is not None:
        return GrblMessage(MessageType.SIGNON, body, line)

    body = _unwrap(line, "[PRB:", "]")
    if body is not None:
        return GrblMessage(MessageType.PRB, body, line)

    return GrblMessage(MessageType.OTHER, line, line)


# Types that the protocol can legitimately emit unsolicited, at any time,
# interleaved with the response to whatever was actually sent (status
# polling, info/warning messages, modal-state pushes, version banners).
# A fixture step that's waiting for something else should skip past these
# transparently rather than treating them as a mismatch -- see
# Controller.expect() in tool/controller.py.
#
# ERROR and ALARM are deliberately *not* in this set: those mean something
# actually went wrong, and silently skipping past one just to keep waiting
# for the originally-expected line would hide a real failure instead of
# reporting it.
ASYNC_NOISE_TYPES = frozenset(
    {MessageType.STATUS, MessageType.MSG, MessageType.GC, MessageType.VER}
)
