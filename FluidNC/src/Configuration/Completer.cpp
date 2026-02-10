// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Completer.h"
#include "Machine/MachineConfig.h"
#include "string_util.h"

#include "Report.h"

#include <ctype.h>

namespace Configuration {
    Completer::Completer(const std::string_view key, uint32_t reqMatch, std::string& matchedStr) :
        _key(key), _reqMatch(reqMatch), _matchedStr(matchedStr), _currentPath("/"), _numMatches(0) {}

    void Completer::addCandidate(std::string fullName) {
        if (_numMatches == _reqMatch) {
            _matchedStr = fullName.c_str();
        }
        ++_numMatches;
    }

    void Completer::enterSection(const char* name, Configuration::Configurable* value) {
        auto previous = _currentPath;
        _currentPath += name;
        _currentPath += "/";
        if (string_util::starts_with_ignore_case(_key, _currentPath)) {
            // If _currentPath is an initial substring of _key, this section
            // is part of a path leading to the key, so we have to check
            // this section's children
            // Example: _key = /axes/x/motor0/cy _currentPath=/axes/x/motor0
            value->group(*this);
        } else if (string_util::starts_with_ignore_case(_currentPath, _key)) {
            // If _key is an initial substring of _currentPath, this section
            // is a candidate.  Example:  _key = /axes/x/h _currentPath=/axes/x/homing
            addCandidate(_currentPath);
        }
        _currentPath = previous;
    }

    void Completer::item(const char* name) {
        std::string fullItemName = _currentPath + name;
        if (string_util::starts_with_ignore_case(fullItemName, _key)) {
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

uint32_t num_initial_matches(const std::string_view key, uint32_t matchnum, std::string& matchname) {
    uint32_t nfound = 0;

    if (key.length() && key[0] == '/') {
        // Match in configuration tree
        Configuration::Completer completer(key, matchnum, matchname);
        config->group(completer);
        nfound = completer._numMatches;
    } else {
        // Match NVS settings
        for (Setting* s : Setting::List) {
            if (string_util::starts_with_ignore_case(s->getName(), key)) {
                if (nfound == matchnum) {
                    matchname = s->getName();
                }
                ++nfound;
            }
        }
        // Match commands
        for (Command* cp : Command::List) {
            if (string_util::starts_with_ignore_case(cp->getName(), key)) {
                if (nfound == matchnum) {
                    matchname = cp->getName();
                }
                ++nfound;
            }
        }
    }
    return nfound;
}
