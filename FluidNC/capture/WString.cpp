#include "WString.h"

#include <iomanip>
#include <sstream>

#pragma warning(disable : 4996)  // itoa

std::string String::ValueToString(int value, int base) {
    char        buffer[100] = { 0 };
    int         number_base = 10;
    std::string output      = itoa(value, buffer, base);
    return output;
}

std::string String::DecToString(double value, int decimalPlaces) {
    std::stringstream stream;
    stream << std::fixed << std::setprecision(decimalPlaces) << value;
    std::string s = stream.str();
    return s;
}

void String::trim() {
    auto   str      = this->backbuf;
    size_t endpos   = str.find_last_not_of(" \t");
    size_t startpos = str.find_first_not_of(" \t");
    if (startpos == std::string::npos) {
        startpos = 0;
    }
    if (endpos == std::string::npos) {
        endpos = str.size();
    }
    str           = str.substr(startpos, endpos - startpos);
    this->backbuf = str;
}

StringAppender& operator+(const StringAppender& lhs, const String& rhs) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(rhs);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, const char* cstr) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(cstr);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, char c) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(c);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, unsigned char num) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(num);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, int num) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(num);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, unsigned int num) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(num);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, long num) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(num);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, unsigned long num) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(num);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, float num) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(num);
    return a;
}

StringAppender& operator+(const StringAppender& lhs, double num) {
    StringAppender& a = const_cast<StringAppender&>(lhs);
    a.concat(num);
    return a;
}

#ifdef _MSC_VER

int strcasecmp(const char* lhs, const char* rhs) {
    while (*lhs && *rhs && tolower(*lhs) == tolower(*rhs)) {
        ++lhs;
        ++rhs;
    }
    return (*lhs) == '\0' && (*rhs) == '\0';
}
int strncasecmp(const char* lhs, const char* rhs, size_t count) {
    while (*lhs && *rhs && tolower(*lhs) == tolower(*rhs) && count > 0) {
        ++lhs;
        ++rhs;
        --count;
    }
    return count == 0 || ((*lhs) == '\0' && (*rhs) == '\0');
}

#endif
