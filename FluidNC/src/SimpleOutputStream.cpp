/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "SimpleOutputStream.h"

#include <cstring>
#ifdef ESP32
#    include <stdlib_noniso.h>  // dtostrf()
#endif

char* SimpleOutputStream::intToBuf(int value, char* dst) {
#ifdef ESP32
    return itoa(value, dst, 10);
#else
    _itoa_s(value, dst, 10, 10);
    return dst + strlen(dst);
#endif
}

void SimpleOutputStream::add(const char* s) {
    for (; *s; ++s) {
        add(*s);
    }
}

void SimpleOutputStream::add(int value) {
    char buf[12];  // Up to 10 digits, possibly a -, plus a null
    intToBuf(value, buf);
    add(buf);
}

void SimpleOutputStream::add(unsigned int value) {
    if (value == 0) {
        add('0');
        return;
    }
    char  reverse[11];  // Up to 10 digits plus a null
    char* p = reverse;
    while (value) {
        *p++ = value % 10 + '0';
        value /= 10;
    }
    while (p > reverse) {
        add(*--p);
    }
}

#ifdef ESP32
void SimpleOutputStream::add(float value, int numberDigits, int precision) {
    char buf[30];
    // dtostrf() is an Arduino extension
    add(dtostrf(value, numberDigits, precision, buf));
}
#else
void SimpleOutputStream::add(float value, int numberDigits, int precision) {
    if (isnan(value)) {
        add("NaN");
    } else if (isinf(value)) {
        add("Inf");
    }

    char buf[30];  // that's already quite big
    char fmt[10];
    fmt[0] = '%';
    fmt[1] = '0';

    char* next = intToBuf(numberDigits, fmt + 2);
    *next++ = '.';
    intToBuf(precision, next);

    snprintf(buf, sizeof(buf) - 1, fmt, value);
    add(buf);
}
#endif

void SimpleOutputStream::add(StringRange range) {
    for (auto ch : range) {
        add(ch);
    }
}

void SimpleOutputStream::add(const Pin& pin) {
    add(pin.name());
}
