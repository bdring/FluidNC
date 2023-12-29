// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// The Channel class is for controlling FluidNC via a two-way communications
// channel, such as a USB serial link, a Bluetooth serial link, a Telnet-style
// TCP connection or a WebSocket stream.  Channel inherits from the Arduino
// File Stream (character input), which inherits from Print (character output).
//
// On top of those base classes, Channel adds the pollLine() method to collect
// a line of input - delimited by newline - while processing "realtime characters"
// that take effect immediately without regard to line boundaries.  It further
// adds the ack() method for flow control, to prevent GCode senders from
// overrunning input buffers.  The default implementation of ack() sends
// "ok" and "error:" messages via the standard Grbl serial protocol, but it
// could be implemented in other ways for different channel protocols.

#pragma once

#include "Error.h"  // Error
#include "GCode.h"  // gc_modal_t
#include "Types.h"  // State
#include <Stream.h>
#include <freertos/FreeRTOS.h>  // TickType_T
#include <queue>

class Channel : public Stream {
public:
    static const int maxLine = 255;

protected:
    const char* _name;
    char        _line[maxLine];
    size_t      _linelen;
    bool        _addCR     = false;
    char        _lastWasCR = false;

    std::queue<uint8_t> _queue;

    uint32_t _reportInterval = 0;
    int32_t  _nextReportTime = 0;

    gc_modal_t _lastModal;
    uint8_t    _lastTool;
    float      _lastSpindleSpeed;
    float      _lastFeedRate;
    State      _lastState;
    MotorMask  _lastLimits;
    bool       _lastProbe;

    bool       _reportWco = true;
    CoordIndex _reportNgc = CoordIndex::End;

    // Set this to false to suppress messages sent to AllChannels
    // It is useful for IO Expanders that do not want to be spammed
    // with chitchat
    bool _all_messages = true;

public:
    Channel(const char* name, bool addCR = false) : _name(name), _linelen(0), _addCR(addCR) {}
    virtual ~Channel() = default;

    virtual void     handle() {};
    virtual Channel* pollLine(char* line);
    virtual void     ack(Error status);
    const char*      name() { return _name; }

    // rx_buffer_available() is the number of bytes that can be sent without overflowing
    // a reception buffer, even if the system is busy.  Channels that can handle external
    // input via an interrupt or other background mechanism should override it to return
    // the remaining space that mechanism has available.
    // The queue can handle more than 256 characters but we don't want it to get too
    // large, so we report a limited size.
    virtual int rx_buffer_available() { return std::max(0, 256 - int(_queue.size())); }

    // flushRx() discards any characters that have already been received.  It is used
    // after a reset, so that anything already sent will not be processed.
    virtual void flushRx();

    // realtimeOkay() returns true if the channel can currently interpret the character as
    // a Grbl realtime character.  Some situations where it might return false are when
    // the channel is being used for file upload or if the channel is doing line editing
    // and is in the middle of an escape sequence that could include what would otherwise
    // be a realtime character.
    virtual bool realtimeOkay(char c) { return true; }

    // lineComplete() accumulates the character into the line, returning true if a line
    // end is seen.
    virtual bool lineComplete(char* line, char c);

    virtual size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
        setTimeout(timeout);
        return readBytes(buffer, length);
    }

    virtual void stopJob() {}

    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); }

    bool setCr(bool on) {
        bool retval = _addCR;
        _addCR      = on;
        return retval;
    }

    void notifyWco() { _reportWco = true; }
    void notifyNgc(CoordIndex coord) { _reportNgc = coord; }

    int peek() override { return -1; }
    int read() override { return -1; }
    int available() override { return _queue.size(); }

    bool all_messages() { return _all_messages; }

    uint32_t     setReportInterval(uint32_t ms);
    uint32_t     getReportInterval() { return _reportInterval; }
    virtual void autoReport();
    void         autoReportGCodeState();
};
