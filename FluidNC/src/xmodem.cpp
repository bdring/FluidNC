/*
 * Copyright 2001-2010 Georges Menie (www.menie.org)
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* this code needs standard functions memcpy() and memset()
   and input/output functions _inbyte() and _outbyte().
   the prototypes of the input/output functions are:
     int _inbyte(uint16_t timeout); // msec timeout
     void _outbyte(int c);
 */

#include "xmodem.h"
#include "crc.h"
// #include <freertos/FreeRTOS.h>

static Channel* serialPort;
static Print*   file;

static int _inbyte(uint16_t timeout) {
    uint8_t data;
    auto    res = serialPort->timedReadBytes(&data, 1, timeout);
    return res != 1 ? -1 : data;
}
static void _outbyte(int c) {
    serialPort->write((uint8_t)c);
}
static void _outbytes(uint8_t* buf, size_t len) {
    serialPort->write(buf, len);
}

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CTRLZ 0x1A

#define DLY_1S 1000
#define MAXRETRANS 25
#define TRANSMIT_XMODEM_1K

static int check(int crc, const uint8_t* buf, int sz) {
    if (crc) {
        uint16_t crc  = crc16_ccitt(buf, sz);
        uint16_t tcrc = (buf[sz] << 8) + buf[sz + 1];
        if (crc == tcrc)
            return 1;
    } else {
        int     i;
        uint8_t cks = 0;
        for (i = 0; i < sz; ++i) {
            cks += buf[i];
        }
        if (cks == buf[sz])
            return 1;
    }

    return 0;
}

static void flushinput(void) {
    while (_inbyte(((DLY_1S)*3) >> 1) >= 0)
        ;
}

// We delay writing each packet until the next one arrives
// so that we can remove trailing control-Z's in only the
// last one.  The Xmodem protocol has no good way to denote
// the actual size of the file in bytes as opposed to packets.
// Instead it pads the final packet with control-Z.  By removing
// those trailing control-Z's before writing to the file, it
// is possible to handle files of any length.  This heuristic
// fails with binary files that are supposed to have trailing
// control-Z's.  Doing the control-Z removal only on the final
// packet avoids removing interior control-Z's that happen to
// land at the end of a packet.
static bool    held = false;
static uint8_t held_packet[1024];
static void    flush_packet(size_t packet_len, size_t& total_len) {
       if (held) {
           // Remove trailing ctrl-z's on the final packet
        held = false;
        size_t count;
        for (count = packet_len; count > 0; --count) {
               if (held_packet[count - 1] != CTRLZ) {
                   break;
            }
        }
        file->write(held_packet, count);
        total_len += count;
    }
}
static void write_packet(uint8_t* buf, size_t packet_len, size_t& total_len) {
    if (held) {
        held = false;
        file->write(held_packet, packet_len);
        total_len += packet_len;
    }
    memcpy(held_packet, buf, packet_len);
    held = true;
}
int xmodemReceive(Channel* serial, FileStream* out) {
    serialPort = serial;
    file       = out;

    held = false;

    uint8_t  xbuff[1030]; /* 1024 for XModem 1k + 3 head chars + 2 crc + nul */
    uint8_t* p;
    int      bufsz = 0, crc = 0;
    uint8_t  trychar  = 'C';
    uint8_t  packetno = 1;
    int      i, c           = 0;
    int      retry, retrans = MAXRETRANS;

    size_t len = 0;

    for (;;) {
        for (retry = 0; retry < 16; ++retry) {
            if (trychar)
                _outbyte(trychar);
            if ((c = _inbyte((DLY_1S) << 1)) >= 0) {
                switch (c) {
                    case SOH:
                        bufsz = 128;
                        goto start_recv;
                    case STX:
                        bufsz = 1024;
                        goto start_recv;
                    case EOT:
                        flush_packet(bufsz, len);
                        _outbyte(ACK);
                        flushinput();
                        return len; /* normal end */
                    case CAN:
                        if ((c = _inbyte(DLY_1S)) == CAN) {
                            flushinput();
                            _outbyte(ACK);
                            return -1; /* canceled by remote */
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        if (trychar == 'C') {
            trychar = NAK;
            continue;
        }
        flushinput();
        _outbyte(CAN);
        _outbyte(CAN);
        _outbyte(CAN);
        return -2; /* sync error */

    start_recv:
        if (trychar == 'C')
            crc = 1;
        trychar = 0;
        p       = xbuff;
        *p++    = c;
        for (i = 0; i < (bufsz + (crc ? 1 : 0) + 3); ++i) {
            if ((c = _inbyte(DLY_1S)) < 0)
                goto reject;
            *p++ = c;
        }

        if (xbuff[1] == (uint8_t)(~xbuff[2]) && (xbuff[1] == packetno || xbuff[1] == packetno - 1) && check(crc, &xbuff[3], bufsz)) {
            if (xbuff[1] == packetno) {
                write_packet(xbuff + 3, bufsz, len);
                ++packetno;
                retrans = MAXRETRANS + 1;
            }
            if (--retrans <= 0) {
                flushinput();
                _outbyte(CAN);
                _outbyte(CAN);
                _outbyte(CAN);
                return -3; /* too many retry error */
            }
            _outbyte(ACK);
            continue;
        }
    reject:
        flushinput();
        _outbyte(NAK);
    }
}

int xmodemTransmit(Channel* serial, FileStream* infile) {
    serialPort = serial;

    uint8_t xbuff[1030]; /* 1024 for XModem 1k + 3 head chars + 2 crc + nul */
    int     bufsz, crc = -1;
    uint8_t packetno = 1;
    int     i, c = 0;
    size_t  len = 0;
    int     retry;

    for (;;) {
        for (retry = 0; retry < 16; ++retry) {
            if ((c = _inbyte((DLY_1S) << 1)) >= 0) {
                switch (c) {
                    case 'C':
                        crc = 1;
                        goto start_trans;
                    case NAK:
                        crc = 0;
                        goto start_trans;
                    case CAN:
                        if ((c = _inbyte(DLY_1S)) == CAN) {
                            _outbyte(ACK);
                            flushinput();
                            return -1; /* canceled by remote */
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        _outbyte(CAN);
        _outbyte(CAN);
        _outbyte(CAN);
        flushinput();
        return -2; /* no sync */

        for (;;) {
        start_trans:
#ifdef TRANSMIT_XMODEM_1K
            xbuff[0] = STX;
            bufsz    = 1024;
#else
            xbuff[0] = SOH;
            bufsz    = 128;
#endif
            xbuff[1] = packetno;
            xbuff[2] = ~packetno;

            auto nbytes = infile->read(&xbuff[3], bufsz);
            if (nbytes > 0) {
                while (nbytes < bufsz) {
                    xbuff[3 + nbytes] = CTRLZ;
                    nbytes++;
                }
                if (crc) {
                    uint16_t ccrc    = crc16_ccitt(&xbuff[3], bufsz);
                    xbuff[bufsz + 3] = (ccrc >> 8) & 0xFF;
                    xbuff[bufsz + 4] = ccrc & 0xFF;
                } else {
                    uint8_t ccks = 0;
                    for (i = 3; i < bufsz + 3; ++i) {
                        ccks += xbuff[i];
                    }
                    xbuff[bufsz + 3] = ccks;
                }
                for (retry = 0; retry < MAXRETRANS; ++retry) {
                    _outbytes(xbuff, bufsz + 4 + (crc ? 1 : 0));
                    if ((c = _inbyte(DLY_1S)) >= 0) {
                        switch (c) {
                            case ACK:
                                ++packetno;
                                len += bufsz;
                                goto start_trans;
                            case CAN:
                                if ((c = _inbyte(DLY_1S)) == CAN) {
                                    _outbyte(ACK);
                                    flushinput();
                                    return -1; /* canceled by remote */
                                }
                                break;
                            case NAK:
                            default:
                                break;
                        }
                    }
                }
                _outbyte(CAN);
                _outbyte(CAN);
                _outbyte(CAN);
                flushinput();
                return -4; /* xmit error */
            } else {
                for (retry = 0; retry < 10; ++retry) {
                    _outbyte(EOT);
                    if ((c = _inbyte((DLY_1S) << 1)) == ACK)
                        break;
                }
                flushinput();
                return (c == ACK) ? len : -5;
            }
        }
    }
}

#ifdef TEST_XMODEM_RECEIVE
int main(void) {
    int st;

    printf("Send data using the xmodem protocol from your terminal emulator now...\n");
    /* the following should be changed for your environment:
	   0x30000 is the download address,
	   65536 is the maximum size to be written at this address
	 */
    st = xmodemReceive((char*)0x30000, 65536);
    if (st < 0) {
        printf("Xmodem receive error: status: %d\n", st);
    } else {
        printf("Xmodem successfully received %d bytes\n", st);
    }

    return 0;
}
#endif
#ifdef TEST_XMODEM_SEND
int main(void) {
    int st;

    printf("Prepare your terminal emulator to receive data now...\n");
    /* the following should be changed for your environment:
	   0x30000 is the download address,
	   12000 is the maximum size to be send from this address
	 */
    st = xmodemTransmit((char*)0x30000, 12000);
    if (st < 0) {
        printf("Xmodem transmit error: status: %d\n", st);
    } else {
        printf("Xmodem successfully transmitted %d bytes\n", st);
    }

    return 0;
}
#endif
