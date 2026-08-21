import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function {signature}")


def test_isr_stop_remains_notification_free_and_late_reset_uses_task_hook():
    header = _source("FluidNC/src/CoolantControl.h")
    coolant = _source("FluidNC/src/CoolantControl.cpp")
    protocol = _source("FluidNC/src/Protocol.cpp")

    assert "void stop_and_notify();" in header
    stop = _function_body(coolant, "void CoolantControl::stop()")
    assert "notify" not in stop
    assert "fluidnc_output_url_transition" not in stop

    notifying_stop = _function_body(coolant, "void CoolantControl::stop_and_notify()")
    assert "const auto previous = _previous_state" in notifying_stop
    assert re.search(r"\bstop\s*\(\s*\)\s*;", notifying_stop)
    assert "previous.Flood" in notifying_stop
    assert "previous.Mist" in notifying_stop
    assert notifying_stop.count("notify_output_url_transition") == 2

    late_reset = _function_body(protocol, "static void protocol_do_late_reset()")
    assert "config->_coolant->stop_and_notify();" in late_reset
    assert "config->_coolant->stop();" not in late_reset
    assert protocol.count("config->_coolant->stop_and_notify();") == 1


def test_http_provider_is_isolated_noexcept_and_wifi_only():
    provider = _source("FluidNC/src/WebUI/OutputUrlHttpProvider.cpp")
    policy = _source("FluidNC/src/WebUI/OutputUrlHttpPolicy.h")
    platformio = _source("platformio.ini")

    assert 'extern "C" bool fluidnc_output_url_http_get' in provider
    assert "HttpCommand" not in provider
    assert "gc_state" not in provider
    assert "config->_" not in provider
    assert "setInsecure()" in provider
    assert "provider_busy.test_and_set" in provider
    assert "catch (...)" in provider
    assert "secure_phase_timeout_seconds(remaining)" in provider
    assert "TaskHandle_t" not in provider
    assert "xTaskNotifyGive" not in provider
    assert "StopGuard" in policy
    assert '"Connection: close\\r\\n"' in policy
    assert '"GET %s HTTP/1.1\\r\\n"' in policy

    common_wifi = platformio.split("[common_wifi]", 1)[1].split("[", 1)[0]
    assert "+<WebUI>" in common_wifi
    assert "-Wl,-u,fluidnc_output_url_http_get" in common_wifi
    common_bt = platformio.split("[common_bt]", 1)[1].split("[", 1)[0]
    common_noradio = platformio.split("[common_noradio]", 1)[1].split("[", 1)[0]
    assert "OutputUrlHttpProvider" not in common_bt
    assert "OutputUrlHttpProvider" not in common_noradio
