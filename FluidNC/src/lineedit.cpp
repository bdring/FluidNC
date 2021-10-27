// #include "string.h"

#define NO_COMPLETION

#include "Uart.h"
// extern void emit(char c);
void emit(char c) {
    Uart0.write(c);
}

/* Use -ve numbers here to co-exist with normal unicode chars */
enum {
    SPECIAL_NONE,
    SPECIAL_UP     = -20,
    SPECIAL_DOWN   = -21,
    SPECIAL_LEFT   = -22,
    SPECIAL_RIGHT  = -23,
    SPECIAL_DELETE = -24,
    SPECIAL_HOME   = -25,
    SPECIAL_END    = -26,
};

#define CTRL(c) (c & 0x1f)

#define BS 8
#define DEL 127

static bool editing = false;

static char* thisaddr;
static char* startaddr;
static char* endaddr;
static char* maxaddr;

void addchar(char c, bool echo = true) {
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
            emit(BS);
        }
    }
}

void erase_char() {
    char* p;
    if (thisaddr > startaddr) {
        --thisaddr;
        --endaddr;
        emit(BS);
        for (p = thisaddr; p < endaddr; p++) {
            *p = *(p + 1);
            emit(*p);
        }
        emit(' ');
        for (++p; p > thisaddr; --p) {
            emit(BS);
        }
    }
}

void erase_line() {
    for (; thisaddr < endaddr; ++thisaddr)
        emit(*thisaddr);
    while (thisaddr > startaddr)
        erase_char();
    endaddr = startaddr;
}

#define MAXHISTORY 400
static int  saved_length;
static char lastline[MAXHISTORY];

void validate_history() {
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

bool already_in_history(char* adr, int len) {
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

void add_to_history(char* adr, int len) {
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
bool get_history(int history_num) {
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

void backward_char() {
    if (thisaddr > startaddr) {
        emit(BS);
        --thisaddr;
    }
}

void forward_char() {
    if (thisaddr < endaddr) {
        emit(*thisaddr);
        ++thisaddr;
    }
}

bool is_word_delim(char c) {
    return c == ' ' || c == '/' || c == '=' || c == ',';
}

void forward_word() {
    while ((thisaddr < endaddr) && is_word_delim(*thisaddr)) {
        emit(*thisaddr);
        ++thisaddr;
    }
    while ((thisaddr < endaddr) && !is_word_delim(*thisaddr)) {
        emit(*thisaddr);
        ++thisaddr;
    }
}

void backward_word() {
    if (startaddr >= endaddr) {
        return;
    }
    // If already at the beginning of a word, dislodge the cursor
    if ((thisaddr < endaddr) && !is_word_delim(*thisaddr) && (thisaddr > startaddr) && is_word_delim(thisaddr[-1])) {
        emit(BS);
        --thisaddr;
    }
    // Scan backward over spaces
    while ((thisaddr < endaddr) && !is_word_delim(*thisaddr) && (thisaddr > startaddr)) {
        emit(BS);
        --thisaddr;
    }
    // Scan backward to a non-space just after a space
    while ((thisaddr > startaddr) && !is_word_delim(thisaddr[-1])) {
        emit(BS);
        --thisaddr;
    }
}

#ifndef NO_COMPLETION
bool isdelim(char* addr) {
    return (addr < startaddr) || (addr == endaddr) || is_word_delim(*addr);
}

static char word[32];
void        find_word_under_cursor() {
    int i       = 0;
    word[i]     = '\0';
    char* taddr = thisaddr;

    if (startaddr == endaddr) {
        return;
    }
    if ((taddr < endaddr) && is_word_delim(*taddr) && (taddr > startaddr) && is_word_delim(taddr[-1])) {
        return;
    }
    while ((taddr > startaddr) && !is_word_delim(taddr[-1])) {
        --taddr;
    }
    while ((taddr < endaddr) && !is_word_delim(*taddr)) {
        if (i != 32) {
            word[i++] = *taddr;
        }
        ++taddr;
    }
    word[i] = '\0';
    while (thisaddr < taddr) {
        emit(*thisaddr++);
    }
}

int num_initial_matches(char* adr, cell len, int matchnum, char** namep, int* actual_len);

static void highlight() {
    emit(0x1b);
    emit('[');
    emit('3');
    emit('4');
    emit('m');
}

static void lowlight() {
    emit(0x1b);
    emit('[');
    emit('0');
    emit('m');
}

static int nmatches = 0;
static int matchlen;
static int thismatch = 0;

void complete_word() {
    find_word_under_cursor();
    if (*word == '\0') {
        return;
    }
    char* name;
    int   len = strlen(word);
    nmatches  = num_initial_matches(word, len, 0, &name, &matchlen);

    if (nmatches == 0) {
        return;
    }
    if (nmatches == 1) {
        while (len < matchlen) {
            addchar(name[len++]);
        }
        addchar(' ');
        nmatches = 0;
        return;
    }

    while (len < matchlen) {
        int   nmatches2, matchlen2;
        char* name2;
        word[len] = name[len];
        nmatches2 = num_initial_matches(word, len + 1, 0, &name2, &matchlen2);
        if (nmatches2 != nmatches) {
            break;
        }
        addchar(word[len++]);
    }
    word[len] = '\0';

    thismatch = 0;
    highlight();
    while (len < matchlen) {
        addchar(name[len++]);
    }
    lowlight();
}

void propose_word() {
    if (++thismatch == nmatches) {
        thismatch = 0;
    }
    int   newmatchlen;
    char* name;
    int   len      = strlen(word);
    int   nmatches = num_initial_matches(word, len, thismatch, &name, &newmatchlen);
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
#endif

static int escaping;
static int history_num = -1;

void lineedit_start(char* addr, int count) {
    startaddr = endaddr = thisaddr = addr;
    escaping                       = 0;
    maxaddr                        = addr + count;
    history_num                    = -1;
}

int lineedit_finish() {
    int length = (int)(endaddr - startaddr);
    add_to_history(startaddr, length);

    return (length);
}

// Returns true when the line is complete
bool lineedit_step(int c) {
    if (!editing) {
        if (c < ' ') {
            if (c == '\r' || c == '\n') {
                return true;
            }
            editing = true;
            // continue to editing code below
        } else {
            addchar(c, false);
            return false;
        }
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
    nmatches = 0;
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
        case DEL:
        case BS:
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
            while (thisaddr < endaddr) {
                forward_char();
                erase_char();
            }
            break;
        case CTRL('u'):
            erase_line();
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

#ifdef LINEEDIT_LOOP
extern int key();

// Line editor with history and intra-line editing
int lineedit(char* addr, int count) {
    lineedit_start(addr, count);
    while (lineedit_step(key()) == 0) {}
    return lineedit_finish();
}
#endif
