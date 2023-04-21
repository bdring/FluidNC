// Simple regular expression matcher from Rob Pike per
// https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html

//    c    matches any literal character c
//    ^    matches the beginning of the input string
//    $    matches the end of the input string
//    *    matches zero or more occurrences of any character

// The code therein has been reformatted into the style used in this
// project while replacing ints used as flags by bools.  The regex
// syntax was changed by omitting '.' and making '*' equivalent to
// ".*".  This regular expression matcher is for matching setting
// names, where arbitrary repetion of literal characters is
// unlikely.  Literal character repetition is most useful for
// skipping whitespace, which does not occur in setting names.  The
// "bare * wildcard" is similar to filename wildcarding in many shells
// and CLIs.

#include <ctype.h>

static bool matchHere(const char* regexp, const char* text, bool case_sensitive);

// matchStar - search for *regexp at beginning of text
static bool matchStar(const char* regexp, const char* text, bool case_sensitive) {
    do {
        if (matchHere(regexp, text, case_sensitive)) {
            return true;
        }
    } while (*text++ != '\0');
    return false;
}

// matchHere - search for regex at beginning of text
static bool matchHere(const char* regexp, const char* text, bool case_sensitive) {
    if (regexp[0] == '\0') {
        return true;
    }
    if (regexp[0] == '*') {
        return matchStar(regexp + 1, text, case_sensitive);
    }
    if (regexp[0] == '$' && regexp[1] == '\0') {
        return *text == '\0';
    }
    if (*text != '\0' && (case_sensitive ? regexp[0] == *text : tolower(regexp[0]) == tolower(*text))) {
        return matchHere(++regexp, ++text, case_sensitive);
    }
    return false;
}

// match - search for regular expression anywhere in text
// Returns true if text contains the regular expression regexp
bool regexMatch(const char* regexp, const char* text, bool case_sensitive) {
    if (regexp[0] == '^') {
        return matchHere(++regexp, text, case_sensitive);
    }
    do {
        if (matchHere(regexp, text, case_sensitive)) {
            return true;
        }
    } while (*text++ != '\0');
    return false;
}
