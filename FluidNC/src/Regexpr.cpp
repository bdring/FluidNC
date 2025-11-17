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
// names, where arbitrary repetition of literal characters is
// unlikely.  Literal character repetition is most useful for
// skipping whitespace, which does not occur in setting names.  The
// "bare * wildcard" is similar to filename wildcarding in many shells
// and CLIs.

#include <string_view>
#include <cctype>

static bool matchHere(std::string_view regexp, std::string_view text, bool case_sensitive);

// matchStar - search for *regexp at beginning of text
static bool matchStar(std::string_view regexp, std::string_view text, bool case_sensitive) {
    do {
        if (matchHere(regexp, text, case_sensitive)) {
            return true;
        }
        if (text.empty()) {
            break;
        }
        text.remove_prefix(1);
    } while (true);
    return false;
}

// matchHere - search for regex at beginning of text
static bool matchHere(std::string_view regexp, std::string_view text, bool case_sensitive) {
    if (regexp.empty()) {
        return true;
    }
    if (regexp[0] == '*') {
        return matchStar(regexp.substr(1), text, case_sensitive);
    }
    if (regexp[0] == '$' && regexp.size() == 1) {
        return text.empty();
    }
    if (!text.empty() && (case_sensitive ? regexp[0] == text[0] : ::tolower(regexp[0]) == std::tolower(text[0]))) {
        return matchHere(regexp.substr(1), text.substr(1), case_sensitive);
    }
    return false;
}

// match - search for regular expression anywhere in text
// Returns true if text contains the regular expression regexp
bool regexMatch(std::string_view regexp, std::string_view text, bool case_sensitive) {
    if (!regexp.empty() && regexp[0] == '^') {
        return matchHere(regexp.substr(1), text, case_sensitive);
    }
    do {
        if (matchHere(regexp, text, case_sensitive)) {
            return true;
        }
        if (text.empty()) {
            break;
        }
        text.remove_prefix(1);
    } while (true);
    return false;
}
