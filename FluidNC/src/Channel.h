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
#include <Stream.h>

class Channel : public Stream {
public:
    static const int maxLine = 255;

protected:
    const char* _name;
    char        _line[maxLine];
    size_t      _linelen;
    bool        _addCR     = false;
    char        _lastWasCR = false;

public:
    Channel(const char* name, bool addCR = false) : _name(name), _linelen(0), _addCR(addCR) {}
    virtual ~Channel() = default;

    virtual void     handle() {};
    virtual Channel* pollLine(char* line);
    virtual void     ack(Error status);
    const char*      name() { return _name; }
    virtual int      rx_buffer_available() = 0;
};
