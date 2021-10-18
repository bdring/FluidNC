// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Completer.h"
#include "../Machine/MachineConfig.h"

#include "../Report.h"

#include <cstdlib>
#include <atomic>

namespace Configuration {
    Completer::Completer(const char* key, int reqMatch, char* matchedStr) :
        _key(key), _reqMatch(reqMatch), _matchedStr(matchedStr), _currentPath("/"), _numMatches(0) {}

    void Completer::addCandidate(String fullName) {
        if (_numMatches == _reqMatch) {
            strcpy(_matchedStr, fullName.c_str());
        }
        ++_numMatches;
    }

    void Completer::enterSection(const char* name, Configuration::Configurable* value) {
        auto previous = _currentPath;
        _currentPath += name;
        _currentPath += "/";
        if (_currentPath.startsWith(_key)) {
            // If _key is an initial substring of _currentPath, this section
            // is a candidate.  Example:  _key = /axes/x/h _currentPath=/axes/x/homing
            addCandidate(_currentPath);
        } else if (_key.startsWith(_currentPath)) {
            // If _currentPath is an initial substring of _key, this section
            // is part of a path leading to the key, so we have to check
            // this section's children
            // Example: _key = /axes/x/motor0/cy _currentPath=/axes/x/motor0
            value->group(*this);
        }
        _currentPath = previous;
    }

    void Completer::item(const char* name) {
        String fullItemName = _currentPath + name;
        if (fullItemName.startsWith(_key)) {
            addCandidate(fullItemName);
        }
    }

    Completer::~Completer() {}
}

// This provides the interface to the completion routines in lineedit.cpp
// The argument signature is idiosyncratic, based on the needs of the
// Forth implementation for which the completion code was first developed.
//
// key, keylen is the address and length of an array of bytes, not null-terminated,
//    for which we seek matches.
// matchnum is the index of the match that we will return
// matchname is the matchnum'th match

int num_initial_matches(char* key, int keylen, int matchnum, char** matchname, int* matchlen) {
    static char matchedstr[100];
    matchedstr[0] = '\0';
    *matchname    = matchedstr;

    char keycstr[100];
    memcpy(keycstr, key, keylen);
    keycstr[keylen] = '\0';

    Configuration::Completer completer(keycstr, matchnum, &matchedstr[0]);
    config->group(completer);

    *matchlen = strlen(matchedstr);

    return completer._numMatches;
}
