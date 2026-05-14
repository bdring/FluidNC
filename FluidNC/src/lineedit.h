// Copyright (c) 2021 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Command line editor with history and completion.
// Arrow keys and other control characters let you move around in and change command lines.
// Several previous lines of history can be recalled and edited.
// Setting and configuration item names can be completed with Tab
// Adapted from similar code at https://github.com/MitchBradley/cforth

#pragma once

#include <Print.h>

class Lineedit {
private:
    static const int MAXHISTORY = 400;
    Print*           out;

    /* Use negative numbers here to co-exist with normal unicode chars */
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

    bool editing      = false;
    bool needs_reecho = false;

    char*       thisaddr;
    char*       startaddr;
    char*       endaddr;
    const char* maxaddr;

    size_t saved_length = 0;

    char lastline[MAXHISTORY] = { 0 };

    char killbuf[100] = { 0 };

    char theWord[100] = { 0 };

    uint32_t nmatches  = 0;
    uint32_t matchlen  = 0;
    uint32_t thismatch = 0;

    int32_t escaping;
    int32_t history_num = -1;

    void emit(char c);

    void echo_line();
    void addchar(char c, bool echo = true);
    void erase_char();
    void erase_line();
    void validate_history();
    bool already_in_history(const char* adr, uint32_t len);
    void add_to_history(const char* adr, uint32_t len);
    bool get_history(uint32_t history_num);
    void backward_char();
    void forward_char();
    bool is_word_delim(char c);
    void forward_word();
    void kill_forward();
    void yank();
    void backward_word();
    bool isdelim(const char* addr);
    bool find_word_under_cursor();
    void color(const char* s);
    void cyan();
    void highlight();
    void gray();
    void lowlight();
    void complete_word();
    void propose_word();
    void accept_word();
    void restart();
    void show_realtime_command(const char* s);

public:
    Lineedit(Print* out, char* line, size_t linelen);

    uint32_t finish();
    bool     step(int c);
    bool     realtime(int c);
};
