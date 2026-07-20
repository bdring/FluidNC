# FluidNC Protocol & GCode Integration Guide

## Overview

This document provides a comprehensive analysis of how FluidNC processes G-code commands, including the Protocol class, Channel interface, and recommended approaches for injecting WebSocket messages into the GCode command pipeline.

---

## 1. Architecture Overview

### Data Flow: WebSocket → GCode Execution

```
WebSocket Message
    ↓
WSChannel (queue bytes)
    ↓
polling_loop() [async task]
    ↓
Channel::pollLine() [collects line with newline]
    ↓
activeChannel + activeLine set
    ↓
protocol_main_loop()
    ↓
execute_line(activeLine, channel)
    ↓
execute_line() dispatcher
├─ $ or [ → settings_execute_line()
└─ G/M → gc_execute_line()
    ↓
Response sent via channel->ack()
```

---

## 2. Channel Interface & Protocol

### 2.1 Channel Base Class

**Location**: [src/Channel.h](src/Channel.h) (Lines 1-200), [src/Channel.cpp](src/Channel.cpp)

#### Essential Methods (Virtual)

| Method | Purpose | Called From |
|--------|---------|-------------|
| `int read()` | Get next byte from physical input | `pollLine()` → line buffer |
| `size_t write(uint8_t c)` | Send byte to output | Log/response handlers |
| `Error pollLine(char* line)` | Collect line of input | `polling_loop()` periodically |
| `void ack(Error status)` | Send "ok" or "error:XX" | `protocol_main_loop()` after execute_line() |
| `void handle()` | Periodic housekeeping | Called during polling (optional) |
| `bool realtimeOkay(char c)` | Can process realtime char? | `pollLine()` for 0x80-0xFF cmds |

#### Key Interface Methods (Non-Virtual)

| Method | Purpose |
|--------|---------|
| `void push(uint8_t byte)` | Queue a byte to internal `_queue` (blocks pollLine) |
| `void push(std::string_view data)` | Queue multiple bytes |
| `void flushRx()` | Clear input queue (called after reset) |
| `void handleRealtimeCharacter(uint8_t ch)` | Process special Grbl realtime cmds (!, ?, ~, ctrl-x, etc.) |

#### Internal State

```cpp
protected:
    std::string _name;
    char _line[maxLine];      // maxLine = 255
    size_t _linelen;
    std::queue<uint8_t> _queue;  // Input character buffer
    int8_t _ackwait;          // Flow control: 1=waiting, 0=ack'd, -1=nak'd
```

### 2.2 Realtime Command Handling

**Realtime commands** (0x80-0xFF, or specific ASCII like `!?~`) are processed **immediately** without waiting for line completion:

```cpp
// In Channel::pollLine()
if (realtimeOkay(ch) && is_realtime_command(ch)) {
    handleRealtimeCharacter((uint8_t)ch);  // Executes immediately
    continue;  // Don't add to line
}
```

**Examples**: Feed hold (`!`), Cycle start (`~`), Reset (ctrl-x), Status report (`?`)

---

## 3. Protocol Main Loop

### 3.1 Two-Task Model

FluidNC uses two coordinated FreeRTOS tasks:

#### Task 1: `polling_loop()` (Async, High Frequency)
**File**: [src/Protocol.cpp](src/Protocol.cpp) lines 118-180

```cpp
void polling_loop(void* unused) {
    for (;;) {
        pollChannels();  // Poll all channels WITHOUT blocking
        
        if (!activeChannel) {
            // No line ready yet; try to get one
            if (!Job::active()) {
                activeChannel = pollChannels(activeLine);  // WITH line arg
            } else {
                // Job channel takes priority
                auto channel = Job::channel();
                auto status = channel->pollLine(activeLine);
                if (status == Error::Ok) {
                    activeChannel = channel;
                }
            }
        }
        // Process other modules...
    }
}
```

**Key Points**:
- Sets `activeChannel` and `activeLine` once per complete command
- Job channels (file, macro) take priority over regular channels
- Polling is **non-blocking**; runs every ~1ms

#### Task 2: `protocol_main_loop()` (Blocking, Main Work)
**File**: [src/Protocol.cpp](src/Protocol.cpp) lines 241-310

```cpp
void protocol_main_loop() {
    start_polling();  // Spin up polling task
    
    for (;;) {
        if (activeChannel) {
            // We have a line ready
            if (gcode_echo->get()) {
                report_echo_line_received(activeLine, allChannels);
            }
            
            Error status_code = execute_line(activeLine, 
                                             *activeChannel, 
                                             AuthenticationLevel::LEVEL_GUEST);
            
            // Send response back through channel
            activeChannel->ack(status_code);
            
            // Clear for next line
            activeChannel = nullptr;
        }
        vTaskDelay(1);
    }
}
```

**Key Points**:
- Blocks on `activeChannel` being non-null
- Calls `execute_line()` synchronously
- Responsible for sending ACK/NAK via `channel->ack()`
- Context where actual G-code execution occurs

### 3.2 Channel Registration

**File**: [src/Serial.cpp](src/Serial.cpp) & [src/Serial.h](src/Serial.h)

```cpp
class AllChannels : public Channel {
    std::vector<Channel*> _channelq;  // List of active channels
    std::mutex _mutex_pollLine;
public:
    void registration(Channel* channel);      // Add channel
    void deregistration(Channel* channel);    // Remove channel
    Channel* poll(char* line);                // Poll all, return first with data
};

extern AllChannels allChannels;  // Global singleton

Channel* pollChannels(char* line) {
    return allChannels.poll(line);  // Wrapper function
}
```

**Usage Pattern:**
1. Create custom Channel subclass
2. Call `allChannels.registration(my_channel)` during init
3. Polling loop automatically includes it
4. Call `allChannels.deregistration(my_channel)` on cleanup

---

## 4. GCode Execution Pipeline

### 4.1 Execute Line Dispatcher

**File**: [src/ProcessSettings.cpp](src/ProcessSettings.cpp) lines 1205-1230

```cpp
Error execute_line(const char* line, Channel& channel, 
                   AuthenticationLevel auth_level) {
    // Empty line → OK
    if (line[0] == 0) {
        return Error::Ok;
    }
    
    // $ command or [WebUI] command → settings
    if (line[0] == '$' || line[0] == '[') {
        if (gc_state.skip_blocks) {
            return Error::Ok;
        }
        return settings_execute_line(line, channel, auth_level);
    }
    
    // Everything else: G-code
    if (state_is(State::Alarm) || state_is(State::ConfigAlarm) || 
        state_is(State::Jog)) {
        return Error::SystemGcLock;
    }
    
    Error result = gc_execute_line(line);
    if (result != Error::Ok) {
        log_error_to(channel, "Bad GCode: " << line);
    }
    return result;
}
```

### 4.2 GCode Parser

**File**: [src/GCode.cpp](src/GCode.cpp) & [src/GCode.h](src/GCode.h)

#### Entry Point
```cpp
Error gc_execute_line(const char* input_line);
```

#### Four-Step Process

```
STEP 1: Parse
  ├─ Copy current modal state
  ├─ Extract G/M words and parameters (X, Y, Z, F, S, T, etc.)
  └─ Track which words were present (bitmasks)

STEP 2: Error Check
  ├─ Modal group violations (e.g., two G0 and G1 in same block)
  ├─ Repeated words (e.g., X10 X20)
  ├─ Negative/invalid values
  └─ Command compatibility checks

STEP 3: Pre-Convert
  ├─ Apply unit conversions (inches → mm)
  ├─ Convert to absolute coordinates if needed
  └─ Validate soft limits

STEP 4: Execute
  ├─ Update modal state (g_state)
  ├─ Build motion plan (if motion)
  ├─ Execute spindle/coolant commands
  └─ Execute special functions (M6, M3, etc.)
```

#### Key Structures

```cpp
// Current parser state (global)
extern parser_state_t gc_state;

// Modal context (what mode are we in?)
struct gc_modal_t {
    Motion motion;           // G0/G1/G2/G3/etc
    FeedRate feed_rate;      // G93/G94
    Units units;             // G20/G21
    Distance distance;       // G90/G91
    Plane plane_select;      // G17/G18/G19
    CoordIndex coord_select; // G54-G59
    ProgramFlow program_flow;// M0/M2/M30
    // ... (spindle, coolant, etc.)
};

// Values extracted from one block
struct gc_values_t {
    float f;              // Feed rate
    float ijk[3];         // Arc offsets
    float xyz[MAX_N_AXIS];// Positions
    float s;              // Spindle speed
    // ... (P, T, L, etc.)
};
```

---

## 5. Existing Channel Implementations

### 5.1 WebSocket Channel (WSChannel)

**Location**: [src/WebUI/WSChannel.h](src/WebUI/WSChannel.h), [src/WebUI/WSChannel.cpp](src/WebUI/WSChannel.cpp)

#### Creation
```cpp
WSChannel(AsyncWebSocket* server, objnum_t clientNum, 
          std::string session);
```

#### Injection Method
```cpp
// File: src/WebUI/WSChannel.cpp, lines 166+
bool WSChannels::runGCode(uint32_t pageid, std::string_view cmd, 
                          std::string session) {
    WSChannel* wsChannel = getWSChannel(pageid, session);
    if (wsChannel) {
        if (cmd.length()) {
            if (is_realtime_command(cmd[0])) {
                // Realtime: handle immediately
                for (auto const& c : cmd) {
                    wsChannel->handleRealtimeCharacter((uint8_t)c);
                }
            } else {
                // G-code: queue for polling
                std::string _cmd = std::string(cmd);
                if (_cmd.back() != '\n') _cmd += '\n';
                wsChannel->push(cmd);
                if (cmd.back() != '\n') {
                    wsChannel->push('\n');
                }
            }
        }
        return false;  // Success
    }
    return true;  // Error - no websocket
}
```

**Key Method**: `push()` feeds data into the channel's internal queue, which `pollLine()` will collect on the next polling cycle.

### 5.2 Other Channel Types

| Type | File | Purpose |
|------|------|---------|
| UartChannel | [src/UartChannel.h](src/UartChannel.h) | Serial UART input |
| PosixConsole | [posix/Console.cpp](posix/Console.cpp) | Terminal (POSIX build only) |
| MacroChannel | [src/Machine/Macros.h](src/Machine/Macros.h) | Macro execution |
| StartupLog | [src/StartupLog.h](src/StartupLog.h) | Boot message capture |
| InputFile | [src/InputFile.h](src/InputFile.h) | G-code file playback |

---

## 6. POSIX Build Simulator

### 6.1 Simulator Engine

**Location**: [posix/simulator_engine.cpp](posix/simulator_engine.cpp), [posix/simulator_engine.h](posix/simulator_engine.h)

**Purpose**: Simulate stepper ISR for testing on POSIX (macOS/Linux)

#### Queue Interface
```cpp
// Called from mock ISR context
void simulator_queue_position(const position_update_t* pos, bool is_final);

// Called by WebSocket task to read updates
bool simulator_queue_dequeue(queue_message_t* msg);

// Get queue depth for flow control
int simulator_queue_depth(void);
```

#### Position Update Message
```cpp
typedef struct {
    int32_t steps[SIMULATOR_MAX_AXES];  // Differential steps
    uint32_t elapsed_us;
} position_update_t;

typedef struct {
    position_update_t position;
    bool is_final;
} queue_message_t;
```

### 6.2 POSIX Console Channel

**Location**: [posix/Console.cpp](posix/Console.cpp)

- Subclass of `Channel`
- Implements `read()` to get bytes from terminal stdin
- Implements `write()` to print to stdout
- Same interface as other channels; automatically polled

---

## 7. Recommended Integration Approach

### Approach 1: WebSocket Push to Existing WSChannel (RECOMMENDED)

**Best for**: Real-time command injection from Web UI

**How it works**:
1. WebSocket message arrives at handler
2. Extract command string (e.g., "G1 X10")
3. Find or create WSChannel for that client
4. Call `wsChannel->push(cmd)` to queue bytes
5. Add newline: `wsChannel->push('\n')`
6. Polling loop picks it up on next cycle → executes → sends ACK

**Code Example**:
```cpp
// In WebSocket message handler:
#include "WebUI/WSChannel.h"

void on_websocket_message(std::string_view message) {
    // Extract command...
    std::string cmd = parse_command(message);  // e.g., "G1 X10"
    
    // Get channel for this client
    WSChannel* channel = WSChannels::findChannel(client_id, session);
    if (channel) {
        // Queue the command
        channel->push(cmd);
        if (cmd.back() != '\n') {
            channel->push('\n');
        }
        // Next polling cycle will execute it
    }
}
```

**Pros**:
- Uses existing infrastructure
- Integrates seamlessly with polling
- Automatic response routing back to channel
- Supports all GCode and settings commands

**Cons**:
- Delayed execution (next polling cycle, typically ~1-5ms)
- Must ensure command has newline

---

### Approach 2: Custom Test Channel for POSIX

**Best for**: Unit testing G-code parser in POSIX build

**How it works**:
1. Create subclass of Channel with internal command queue
2. Register with `allChannels.registration()`
3. Inject commands via `channel->inject_command(cmd)`
4. Polling automatically picks them up

**Code Example**:
```cpp
// Header: posix/TestChannel.h
class TestChannel : public Channel {
private:
    std::queue<char> _command_queue;
    
public:
    TestChannel() : Channel("test_channel") {}
    
    int read() override {
        if (_command_queue.empty()) return -1;
        int c = _command_queue.front();
        _command_queue.pop();
        return c;
    }
    
    size_t write(uint8_t c) override {
        // Capture output for testing
        return 1;
    }
    
    void inject_command(const std::string& cmd) {
        for (char c : cmd) {
            _command_queue.push(c);
        }
        _command_queue.push('\n');
    }
};

// Usage:
TestChannel test_chan;
allChannels.registration(&test_chan);

test_chan.inject_command("G1 X10 F1000");
// Next polling cycle executes it
```

**Pros**:
- Full control over command timing
- Can test parser error handling
- Captures output via `write()`
- Works in POSIX build

**Cons**:
- Requires custom implementation
- No actual hardware execution
- Limited to testing, not real machine control

---

### Approach 3: Direct `execute_line()` Call

**Best for**: Immediate synchronous execution in specific contexts

**How it works**:
1. Bypass polling; call `execute_line()` directly
2. Useful for internal commands (homing, macros)

**Code Example**:
```cpp
#include "ProcessSettings.h"

Error result = execute_line("G1 X10 Y20 F1000", 
                           channel, 
                           AuthenticationLevel::LEVEL_GUEST);
if (result == Error::Ok) {
    // Command accepted; motion queued
} else {
    log_error("GCode error: " << result);
}
```

**Pros**:
- Immediate execution
- Direct error handling
- Useful for internal commands

**Cons**:
- Blocks current task during execution
- Must be called from safe context (not ISR)
- Not for high-volume command injection
- Bypasses protocol queue (not recommended for user input)

---

## 8. Flow Control & Synchronization

### ACK/NAK Protocol

FluidNC implements Grbl-compatible flow control:

```cpp
void Channel::ack(Error status) {
    if (status == Error::Ok) {
        sendLine(MsgLevelNone, "ok");  // Success
    } else {
        LogStream s(*this, "error");
        s << static_cast<int>(status);  // "error:2" etc
    }
}
```

Each G-code command receives:
- `ok` → successful execution
- `error:XX` → error code

This prevents command overflow on slow connections.

### JobChannel Priority

Job channels (file playback, macros) pause regular channels:

```cpp
if (!Job::active()) {
    // Regular channels
    activeChannel = pollChannels(activeLine);
} else {
    // Job channel only
    auto status = Job::channel()->pollLine(activeLine);
    // ...
}
```

---

## 9. Error Codes

All operations return an `Error` enum:

**File**: [src/Error.h](src/Error.h)

Key values:
- `Error::Ok` (0) - Success
- `Error::NoData` - No line ready yet
- `Error::Eof` - End of file/input
- `Error::GcodeXxx` - Parser error (see Error.h for full list)
- `Error::SystemGcLock` - Alarm/jog state blocks GCode

---

## 10. Testing & Debug

### POSIX Test Build

```bash
cd FluidNC
platformio run --environment posix
.pio/build/posix/program  # Run executable
```

**Console I/O**: Connects PosixConsole channel to stdin/stdout

### Known Test Channels

**File**: [FluidNC/src/Machine/Macros.h](src/Machine/Macros.h)

```cpp
class MacroChannel : public Channel {
    // Reads G-code from a Macro definition
    Error readLine(char* line, size_t maxlen);
};
```

Used internally for macro execution; same interface as user channels.

---

## 11. Summary: GCode Pipeline Diagram

```
┌─────────────────────────────────────┐
│  WebSocket/UART/File Message        │
└──────────────┬──────────────────────┘
               │
               ├─→ Realtime (0x80-0xFF)
               │   ├─ handleRealtimeCharacter()
               │   └─ Process immediately (!, ~, ?, etc.)
               │
               └─→ Command (G/M/$)
                   │
     ┌─────────────┴─────────────┐
     │  Channel::push()           │
     │  _queue += bytes           │
     └──────────────┬─────────────┘
                    │
     ┌──────────────▼─────────────┐
     │  polling_loop() task       │  (async, ~1ms cycle)
     │  Channel::pollLine()       │
     │  Collects _queue → line    │
     │  Sets activeChannel        │
     └──────────────┬─────────────┘
                    │
     ┌──────────────▼──────────────────┐
     │  protocol_main_loop() task      │  (main work)
     │  execute_line(activeLine, ch)   │
     │  Dispatcher:                    │
     │  ├─ $ → settings_execute_line() │
     │  └─ G → gc_execute_line()       │
     └──────────────┬──────────────────┘
                    │
     ┌──────────────▼──────────────────┐
     │  GCode Parser (4 steps)         │
     │  1. Parse words                 │
     │  2. Error check                 │
     │  3. Pre-convert                 │
     │  4. Execute motion/spindle/etc  │
     └──────────────┬──────────────────┘
                    │
     ┌──────────────▼──────────────────┐
     │  Channel::ack(status)           │
     │  Send "ok" or "error:XX"        │
     └──────────────────────────────────┘
```

---

## Files Reference

| Component | Location | Key Functions |
|-----------|----------|---|
| **Protocol** | [src/Protocol.h](src/Protocol.h), [src/Protocol.cpp](src/Protocol.cpp) | `protocol_main_loop()`, `polling_loop()` |
| **Channel Interface** | [src/Channel.h](src/Channel.h), [src/Channel.cpp](src/Channel.cpp) | `pollLine()`, `ack()`, `push()` |
| **Channel Management** | [src/Serial.h](src/Serial.h), [src/Serial.cpp](src/Serial.cpp) | `AllChannels`, `registration()`, `poll()` |
| **GCode Execution** | [src/ProcessSettings.cpp](src/ProcessSettings.cpp) | `execute_line()` |
| **GCode Parser** | [src/GCode.h](src/GCode.h), [src/GCode.cpp](src/GCode.cpp) | `gc_execute_line()` |
| **WebSocket Channel** | [src/WebUI/WSChannel.h](src/WebUI/WSChannel.h), [src/WebUI/WSChannel.cpp](src/WebUI/WSChannel.cpp) | `WSChannels::runGCode()` |
| **POSIX Simulator** | [posix/simulator_engine.h](posix/simulator_engine.h), [posix/simulator_engine.cpp](posix/simulator_engine.cpp) | `simulator_queue_position()` |

---

## Conclusion

**Recommended approach for WebSocket integration:**

1. **Receive WebSocket message** → parse command string
2. **Push to WSChannel**: `channel->push(cmd); channel->push('\n');`
3. **Polling loop** automatically picks it up on next cycle (~1-5ms)
4. **Protocol loop** executes via `execute_line()`
5. **Response sent** via `channel->ack()` back to WebSocket client

This leverages existing infrastructure and integrates seamlessly with the Protocol's flow control and error handling.
