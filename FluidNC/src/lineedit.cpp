// Copyright (c) 2021 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "lineedit.h"

Lineedit::Lineedit(Print* _out, char* line, int linelen) : out(_out), needs_reecho(false), startaddr(line), maxaddr(line + linelen) {
    restart();
}

#define CTRL(c) (c & 0x1f)

void Lineedit::emit(char c) {
    out->write(c);
}

void Lineedit::echo_line() {
    for (char* p = startaddr; p < endaddr; ++p) {
        emit(*p);
    }
    for (char* p = endaddr; p > thisaddr; --p) {
        emit('\b');
    }
}

void Lineedit::addchar(char c, bool echo) {
    char* p;
    if (thisaddr < maxaddr) {
        if (endaddr < maxaddr)
            ++endaddr;
        for (p = endaddr; --p >= thisaddr + 1;) {
            *p = *(p - 1);
        }
        *thisaddr++ = c;
        if (echo) {
            emit(c);
        }
        for (++p; p < endaddr; p++) {
            emit(*p);
        }
        for (; p > thisaddr; --p) {
            emit('\b');
        }
    }
}

void Lineedit::erase_char() {
    char* p;
    if (thisaddr > startaddr) {
        --thisaddr;
        --endaddr;
        emit('\b');
        for (p = thisaddr; p < endaddr; p++) {
            *p = *(p + 1);
            emit(*p);
        }
        emit(' ');
        for (++p; p > thisaddr; --p) {
            emit('\b');
        }
    }
}

void Lineedit::erase_line() {
    for (; thisaddr < endaddr; ++thisaddr)
        emit(*thisaddr);
    while (thisaddr > startaddr)
        erase_char();
    endaddr = startaddr;
}

void Lineedit::validate_history() {
    int i;

    // Clear history if it is invalid
    if (saved_length == 0 || saved_length > MAXHISTORY)
        goto clear_history;

    for (i = 0; i < MAXHISTORY; i++) {
        if (lastline[i] & 0x80)
            goto clear_history;
    }
    return;

clear_history:
    for (i = 0; i < MAXHISTORY; i++) {
        lastline[i] = '\0';
    }
    saved_length = 0;
}

bool Lineedit::already_in_history(char* adr, int len) {
    char* p;
    char* first;
    char* thischar;
    int   i;
    if (!saved_length)
        return false;

    thischar = adr;
    first    = lastline;
    for (p = lastline; p < &lastline[MAXHISTORY];) {
        if (*p == '\0') {
            if ((p - first) == len) {
                // Found a match; reorder history so the match
                // is at the beginning.
                while ((--p - lastline) > len) {
                    *p = p[-len - 1];
                }
                *p = '\0';
                for (i = 0; i < len; i++) {
                    lastline[i] = adr[i];
                }
                return true;
            }
            if (++p == &lastline[MAXHISTORY])
                return false;
            thischar = adr;
            first    = p;
            continue;
        }
        if (*p == *thischar) {
            // Match
            ++p;
            ++thischar;
        } else {
            // Mismatch
            while (*++p != '\0') {}
            ++p;
            thischar = adr;
            first    = p;
        }
    }
    return false;
}

void Lineedit::add_to_history(char* adr, int len) {
    int i;
    int new_length;

    validate_history();
    if (len && !already_in_history(adr, len)) {
        len += 1;  // Room for null
        new_length = (len > MAXHISTORY) ? MAXHISTORY : len;

        // Make room for new history line
        for (i = MAXHISTORY; --i >= new_length;)
            lastline[i] = lastline[i - new_length];

        lastline[MAXHISTORY - 1] = '\0';  // Truncate the last line
        lastline[i]              = '\0';

        while (--i >= 0)
            lastline[i] = adr[i];

        saved_length += new_length;
        if (saved_length > MAXHISTORY)
            saved_length = MAXHISTORY;
    }
}

// history_num is the number of the history line to fetch
// returns true if that line exists.
bool Lineedit::get_history(int history_num) {
    int   i;
    int   hn;
    char* p;

    validate_history();

    if (saved_length == 0)
        return false;

    if (history_num < 0)
        return false;

    p = lastline;
    for (hn = 0; hn < history_num; hn++) {
        while (*p++ != '\0') {}
        if ((p - lastline) >= saved_length)
            return false;
    }

    erase_line();
    for (i = 0; i < maxaddr - startaddr - 1; i++) {
        if (*p == '\0') {
            break;
        }
        addchar(*p++);
    }

    return true;
}

void Lineedit::backward_char() {
    if (thisaddr > startaddr) {
        emit('\b');
        --thisaddr;
    }
}

void Lineedit::forward_char() {
    if (thisaddr < endaddr) {
        emit(*thisaddr);
        ++thisaddr;
    }
}

bool Lineedit::is_word_delim(char c) {
    return c == ' ' || c == '/' || c == '=' || c == ',';
}

void Lineedit::forward_word() {
    // Skip delimiters that we are already on
    while ((thisaddr < endaddr) && is_word_delim(*thisaddr)) {
        emit(*thisaddr);
        ++thisaddr;
    }
    // Find the next delimiter
    while ((thisaddr < endaddr) && !is_word_delim(*thisaddr)) {
        emit(*thisaddr);
        ++thisaddr;
    }
    // Skip to the next non-delimiter
    while ((thisaddr < endaddr) && is_word_delim(*thisaddr)) {
        emit(*thisaddr);
        ++thisaddr;
    }
}

void Lineedit::kill_forward() {
    char* p = killbuf;
    while (thisaddr < endaddr) {
        *p++ = *thisaddr;
        forward_char();
        erase_char();
    }
    *p = '\0';
}
void Lineedit::yank() {
    for (char* p = killbuf; *p; ++p) {
        addchar(*p);
    }
}

void Lineedit::backward_word() {
    if (startaddr >= endaddr) {
        return;
    }

    // Skip over delimiters
    while ((thisaddr > startaddr) && is_word_delim(thisaddr[-1])) {
        emit('\b');
        --thisaddr;
    }
    // Scan backward over non-delimiters
    while ((thisaddr > startaddr) && !is_word_delim(thisaddr[-1])) {
        emit('\b');
        --thisaddr;
    }
#if 0
    // Scan backward to a non-space just after a space
    while ((thisaddr > startaddr) && !is_word_delim(thisaddr[-1])) {
        emit('\b');
        --thisaddr;
    }
#endif
}

#ifndef NO_COMPLETION
bool Lineedit::isdelim(char* addr) {
    return (addr < startaddr) || (addr == endaddr) || is_word_delim(*addr);
}

char theWord[100];
bool Lineedit::find_word_under_cursor() {
    if (startaddr == endaddr || *startaddr != '$') {
        return false;
    }
    int   i    = 0;
    char* addr = startaddr + 1;
    while (addr < thisaddr && i < (100 - 1)) {
        theWord[i++] = *addr++;
    }
    // Move to the end of the item name
    while (thisaddr < endaddr && i < (100 - 1) && *thisaddr != '=') {
        emit(*thisaddr);
        theWord[i++] = *thisaddr++;
    }
    theWord[i] = '\0';
    return true;
}

extern int num_initial_matches(char* key, int keylen, int matchnum, char* matchname);

void Lineedit::color(const char* s) {
    emit(0x1b);
    emit('[');
    while (*s) {
        emit(*s++);
    }
    emit('m');
}
void Lineedit::cyan() {
    color("1;36;40");
}
void Lineedit::highlight() {
    cyan();
}

void Lineedit::gray() {
    color("0;37;40");
}

void Lineedit::lowlight() {
    gray();
}

void Lineedit::complete_word() {
    if (!find_word_under_cursor()) {
        return;
    }
    char name[100];
    name[0]  = '\0';
    int len  = strlen(theWord);
    nmatches = num_initial_matches(theWord, len, 0, name);

    if (nmatches == 0) {
        return;
    }
    matchlen = strlen(name);
    if (nmatches == 1) {
        while (len < matchlen) {
            addchar(name[len++]);
        }
        nmatches = 0;
        return;
    }

    while (len < matchlen) {
        theWord[len] = name[len];
        if (nmatches != num_initial_matches(theWord, len + 1, 0, nullptr)) {
            break;
        }
        addchar(theWord[len++]);
    }
    theWord[len] = '\0';

    thismatch = 0;
    highlight();
    while (len < matchlen) {
        addchar(name[len++]);
    }
    lowlight();
}

void Lineedit::propose_word() {
    if (++thismatch == nmatches) {
        thismatch = 0;
    }
    char name[100];
    name[0]         = '\0';
    int len         = strlen(theWord);
    int nmatches    = num_initial_matches(theWord, len, thismatch, name);
    int newmatchlen = strlen(name);

    while (matchlen > len) {
        erase_char();
        --matchlen;
    }
    highlight();
    while (matchlen < newmatchlen) {
        addchar(name[matchlen++]);
    }
    lowlight();
}
void Lineedit::accept_word() {
    int len = strlen(theWord);
    int i;
    for (i = matchlen; i > len; --i) {
        emit('\b');
        --thisaddr;
    }
    lowlight();
    while (i < matchlen) {
        emit(*thisaddr++);
        ++i;
    }
}
#endif

void Lineedit::restart() {
    needs_reecho = false;
    endaddr = thisaddr = startaddr;
    endaddr            = startaddr;
    escaping           = 0;
    history_num        = -1;
}

void Lineedit::show_realtime_command(const char* s) {
    if (startaddr < endaddr) {
        emit('\n');
    }
    if (*s) {
        while (*s) {
            emit(*s++);
        }
        emit('\n');
        echo_line();
    } else {
        needs_reecho = true;
    }
}

// public

int Lineedit::finish() {
    int length = (int)(endaddr - startaddr);
    add_to_history(startaddr, length);
    restart();
    return (length);
}

// Special handling for realtime characters.
// In the middle of a SPECIAL_DELETE sequence, we treat ~ as part
// of that sequence, instead of as a realtime character.
// Otherwise, we report the character without messing up the display
// of the line that is being collected.
// Returns true if the character should be treated as realtime.

bool Lineedit::realtime(int c) {
    if (!editing) {
        return true;
    }
    if (escaping < 0 && c == '~') {
        // In the middle of a SPECIAL_DELETE sequence, treat ~ as
        // part of the escape sequence instead of as a realtime character
        return false;
    }
    switch (c) {
        case '!':
            show_realtime_command("[Feedhold]");
            break;
        case '~':
            show_realtime_command("[CycleStart]");
            break;
        case '?':
            // For status reports we do not show the command because
            // a status report will be issued immediately.  This looks
            // the same as what people are usually accustomed to.
            show_realtime_command("");
            break;
        case CTRL('x'):
            show_realtime_command("[Reset]");
            break;
    }
    return true;
}

// Returns true when the line is complete
bool Lineedit::step(int c) {
    if (!editing) {
        if (c < ' ') {
            if (c == '\r' || c == '\n') {
                return true;
            }
            needs_reecho = true;
            editing      = true;
            // continue to editing code below
        } else {
            addchar(c, false);
            return false;
        }
    }

    if (needs_reecho) {
        needs_reecho = false;
        echo_line();
    }

    // If we are running on Windows, key() returns the SPECIAL_* values
    // for non-ASCII movement keys.  Under a terminal emulator, such keys
    // generate escape sequences that we parse herein and convert to
    // those SPECIAL_* values.

    // Expecting [ as second character of escape sequence
    if (escaping == 1) {
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        switch (c) {
            case '[':
                escaping = 2;
                return false;
            case 'f':
                forward_word();
                break;
            case 'b':
                backward_word();
                break;
        }
        escaping = 0;
        return false;
    }

    // Expecting third character of escape sequence
    if (escaping == 2) {
        escaping = 0;
        switch (c) {
            // In these 3 cases we have to get one more byte, typically ~
            case '2':
                escaping = SPECIAL_HOME;
                return false;  // esc[2~ HOME
            case '5':
                escaping = SPECIAL_END;
                return false;  // esc[5~ END
            case '3':
                escaping = SPECIAL_DELETE;
                return false;  // esc[3~ DELETE

            // In these cases we are done so translate to the special code
            case '1':
                c = SPECIAL_HOME;
                break;  // esc[1 Home key
            case 'H':
                c = SPECIAL_HOME;
                break;  // esc[H Home key
            case 'F':
                c = SPECIAL_END;
                break;  // esc[F End key
            case '4':
                c = SPECIAL_END;
                break;  // esc[4 End key
            case 'A':
                c = SPECIAL_UP;
                break;  // esc[A Up arrow
            case 'B':
                c = SPECIAL_DOWN;
                break;  // esc[B Down arrow
            case 'C':
                c = SPECIAL_RIGHT;
                break;  // esc[C Right arrow
            case 'D':
                c = SPECIAL_LEFT;
                break;  // esc[D Left arrow
        }
        // Fall through to the switch below (escaping is now 0)
    }

    // This handles the ESC[n~ case - escaping is one of SPECIAL_{HOME,END,DELETE}
    if (escaping) {
        if (c == '~') {
            c        = escaping;
            escaping = 0;
        } else {
            escaping = 0;
            return false;
        }
    }

#ifndef NO_COMPLETION
    if (c == CTRL('i')) {
        if (nmatches) {
            propose_word();
        } else {
            complete_word();
        }
        return false;
    }
    if (nmatches) {
        accept_word();
        nmatches = 0;
    }
#endif

    switch (c) {
        case 27:  // Escape
            escaping = 1;
            break;
        case '\n':
        case '\r':
            emit('\n');
            return true;
        case -1:
            return true;
        case 127:  // Delete
        case '\b':
            if (thisaddr > startaddr)
                erase_char();
            break;
        case CTRL('a'):
        case SPECIAL_HOME:
            while (thisaddr > startaddr)
                backward_char();
            break;
        case CTRL('b'):
        case SPECIAL_LEFT:
            backward_char();
            break;
        case CTRL('d'):
        case SPECIAL_DELETE:
            if (thisaddr < endaddr) {
                forward_char();
                erase_char();
            }
            break;
        case CTRL('e'):
        case SPECIAL_END:
            while (thisaddr < endaddr)
                forward_char();
            break;
        case CTRL('f'):
        case SPECIAL_RIGHT:
            forward_char();
            break;
        case CTRL('k'):
            kill_forward();
            break;
        case CTRL('u'):
            erase_line();
            break;
        case CTRL('y'):
            yank();
            break;
        case CTRL('p'):
        case SPECIAL_UP:
            if (get_history(history_num + 1))
                ++history_num;
            break;
        case CTRL('n'):
        case SPECIAL_DOWN:
            if (get_history(history_num - 1))
                --history_num;
            break;
        case CTRL('w'):
            while (thisaddr > startaddr && is_word_delim(thisaddr[-1]))
                erase_char();
            while (thisaddr > startaddr && !is_word_delim(thisaddr[-1]))
                erase_char();
            break;

        default:
            if (c >= ' ')
                addchar(c);
    }
    return false;
}
