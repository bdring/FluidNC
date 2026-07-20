# FluidNC Output Suppression Analysis

## Summary
There are **multiple conditions** where output can be suppressed in the UartChannel output path:

---

## 1. **Message Level Filtering** (Primary)

**Code:** `Channel::print_msg()` in Channel.cpp:283
```cpp
void Channel::print_msg(MsgLevel level, const char* msg) {
    if (_message_level >= level) {
        write(msg);
        write("\n");
    }
}
```

**Condition:** Output is **suppressed** if:
- `_message_level < level` (channel's configured level is lower than message's level)

**Default Message Levels:**
- Default channel level: `MsgLevelInfo` (configured in SettingsDefinitions.cpp:96)
- Message levels hierarchy (lowest to highest):
  - `MsgLevelNone` (0) - Always shown
  - `MsgLevelError` (1)
  - `MsgLevelWarning` (2)
  - `MsgLevelInfo` (3) - **DEFAULT**
  - `MsgLevelDebug` (4)
  - `MsgLevelVerbose` (5)

**Problem:** Messages below the configured level are silently dropped. For simulator testing, if you're using `log_debug()` or `log_info()`, they may not appear on UART unless the message level is increased.

---

## 2. **Channel Active State** (WebSocket/WSChannel Only)

**Code:** `WSChannel::write()` in WebUI/WSChannel.cpp:48
```cpp
size_t WSChannel::write(const uint8_t* buffer, size_t size) {
    if (buffer == NULL || !_active || !size) {
        return 0;  // SUPPRESSED
    }
    // ... actual send logic
}
```

**Condition:** Output is **suppressed** if:
- `!_active` (channel marked as inactive)

**Also in:** 
- `WSChannel::sendTXT()` (line 107)
- `WSChannel::autoReport()` (line 119)

**Note:** This affects WebSocket channels, not UART. A WebSocket client can be marked inactive if it disconnects or encounters errors.

---

## 3. **Report Interval Control** (Status/Auto-Reporting)

**Code:** `Channel::autoReport()` in Channel.cpp:122
```cpp
void Channel::autoReport() {
    if (_reportInterval) {  // Only reports if interval is set
        if (_reportOvr || _reportWco || stateName != _lastStateName || 
            /* other conditions */) {
            // Send status report
            report_realtime_status(*this);
        }
    }
}
```

**Condition:** Auto-reports (status updates) are **suppressed** if:
- `_reportInterval == 0` (not configured)
- Status hasn't changed and time interval hasn't elapsed

**Impact:** Periodic position/status updates only appear if auto-reporting is enabled (`$I=n` setting).

---

## 4. **Input Paused State** (Input Processing)

**Code:** `Channel::pollLine()` in Channel.cpp:212
```cpp
Error Channel::pollLine(char* line) {
    if (_paused) {
        return Error::Ok;  // Input suppressed
    }
    // ... process input
}
```

**Condition:** Input processing is **suppressed** if:
- `_paused == true`

**Note:** This suppresses INPUT (incoming commands), not output. But if input is paused, the channel may not generate responses.

---

## 5. **Output Buffer Queue Control** (Flow Control)

**Code:** `WSChannel::write()` in WebUI/WSChannel.cpp:79-91
```cpp
if (!inMotionState()) {
    // During idle: wait for queue to drain
    while (_server->client(_clientNum) && 
           _server->client(_clientNum)->queueLen() >= max(WS_MAX_QUEUED_MESSAGES - 2, 1)) {
        delay(1);
    }
} else {
    // During motion: drop messages if queue is full
    if (_server->client(_clientNum) && _server->client(_clientNum)->queueIsFull()) {
        // ... messages silently dropped
    }
}
```

**Condition:** Output can be **dropped** if:
- WebSocket queue is full during motion state

---

## For Your Simulator Testing

**Key Points:**
1. **UART (UartChannel) output** is NOT suppressed by `_active` flag - only by message level
2. **WebSocket (WSChannel) output** IS suppressed if channel is marked `!_active`
3. **Message level filtering** applies to both - ensure messages meet the channel's configured level
4. **Auto-reporting** requires `_reportInterval` to be configured
5. **Direct `write()` calls** bypass message level filtering and go straight to output

**Recommendation for Simulator Position Updates:**
- Use **direct `write()` calls** for simulator data (bypasses message level filtering)
- Set message level to at least `MsgLevelInfo` if using `log_*` macros
- Ensure WebSocket channel is marked `_active` if sending via WebSocket
- Configure auto-reporting interval if relying on periodic updates

