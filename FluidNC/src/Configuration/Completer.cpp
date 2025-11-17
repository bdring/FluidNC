// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Completer.h"
#include "Machine/MachineConfig.h"

#include "Report.h"

#include <ctype.h>
#include <atomic>
#include <string_view>

static bool isInitialSubstringCI(const std::string_view key, const std::string_view test) {
    if (key.length() > test.length()) {
        return false;
    }
    size_t i = 0;
    for (auto const c : key) {
        char c1 = test[i++];
        if (::tolower(c) != ::tolower(c1)) {
            return false;
        }
    }
    return true;
}

namespace Configuration {
    Completer::Completer(const char* key, uint32_t reqMatch, char* matchedStr) :
        _key(key), _reqMatch(reqMatch), _matchedStr(matchedStr), _currentPath("/"), _numMatches(0) {}

    void Completer::addCandidate(std::string fullName) {
        if (_matchedStr && _numMatches == _reqMatch) {
            strcpy(_matchedStr, fullName.c_str());
        }
        ++_numMatches;
    }

    void Completer::enterSection(const char* name, Configuration::Configurable* value) {
        auto previous = _currentPath;
        _currentPath += name;
        _currentPath += "/";
        if (isInitialSubstringCI(_currentPath, _key)) {
            // If _currentPath is an initial substring of _key, this section
            // is part of a path leading to the key, so we have to check
            // this section's children
            // Example: _key = /axes/x/motor0/cy _currentPath=/axes/x/motor0
            value->group(*this);
        } else if (isInitialSubstringCI(_key, _currentPath)) {
            // If _key is an initial substring of _currentPath, this section
            // is a candidate.  Example:  _key = /axes/x/h _currentPath=/axes/x/homing
            addCandidate(_currentPath);
        }
        _currentPath = previous;
    }

    void Completer::item(const char* name) {
        std::string fullItemName = _currentPath + name;
        if (isInitialSubstringCI(_key, fullItemName)) {
            addCandidate(fullItemName);
        }
    }

    Completer::~Completer() {}
}

#include "Settings.h"

// This provides the interface to the completion routines in lineedit.cpp
// The argument signature is idiosyncratic, based on the needs of the
// Forth implementation for which the completion code was first developed.
//
// key, keylen is the address and length of an array of bytes, not null-terminated,
//    for which we seek matches.
// matchnum is the index of the match that we will return
// matchname is the matchnum'th match

uint32_t num_initial_matches(const char* key, uint32_t keylen, uint32_t matchnum, char* matchname) {
    uint32_t nfound = 0;

    if (key[0] == '/') {
        char keycstr[100];
        memcpy(keycstr, key, keylen);
        keycstr[keylen] = '\0';

        // Match in configuration tree
        Configuration::Completer completer(keycstr, matchnum, matchname);
        config->group(completer);
        nfound = completer._numMatches;
    } else {
        // Match NVS settings
        for (Setting* s : Setting::List) {
            if (isInitialSubstringCI(key, s->getName())) {
                if (matchname && nfound == matchnum) {
                    strcpy(matchname, s->getName());
                }
                ++nfound;
            }
        }
    }

    return nfound;
}
