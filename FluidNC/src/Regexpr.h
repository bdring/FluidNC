// Simple regular expression matcher.
// See Regex.cpp for attribution, description and discussion

#include <string_view>

// Returns true if text contains the regular expression regexp
bool regexMatch(std::string_view regexp, std::string_view text, bool case_sensitive = true);
