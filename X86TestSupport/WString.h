#pragma once

// Basically this is an implementation of WString, like the WString API from Arduino itself. However, contrary
// to Arduino, we use std::string as a backbuffer, thereby making it easy

#include <WString.h>
#include <string>

class StringAppender;

class String {
    std::string backbuf;

    static std::string ValueToString(int value, int base);
    static std::string DecToString(double value, int decimalPlaces);

    // Operator bool helper. Needed for older compilers:
    typedef void (String::*StringIfHelperType)() const;
    void StringIfHelper() const {}

    String(std::string& s) : backbuf(s) {}

protected:
    inline const char* buffer() const { return backbuf.c_str(); }
    inline char*       wbuffer() { return &*backbuf.begin(); }

public:
    String(const char* cstr = "") : backbuf(cstr) {}
    String(const String& str) : backbuf(str.backbuf) {}
    explicit String(char c) : backbuf({ c }) {}
    explicit String(unsigned char ch, unsigned char base = 10) : backbuf(ValueToString(ch, base)) {}
    explicit String(int ch, unsigned char base = 10) : backbuf(ValueToString(ch, base)) {}
    explicit String(unsigned int ch, unsigned char base = 10) : backbuf(ValueToString(ch, base)) {}
    explicit String(long ch, unsigned char base = 10) : backbuf(ValueToString(ch, base)) {}
    explicit String(unsigned long ch, unsigned char base = 10) : backbuf(ValueToString(ch, base)) {}
    explicit String(float fl, unsigned char decimalPlaces = 2) : backbuf(DecToString(fl, decimalPlaces)) {}
    explicit String(double fl, unsigned char decimalPlaces = 2) : backbuf(DecToString(fl, decimalPlaces)) {}

    String& operator=(const String& rhs) {
        backbuf = rhs.backbuf;
        return *this;
    }
    String& operator=(const char* cstr) {
        backbuf = cstr;
        return *this;
    }

    unsigned char reserve(unsigned int size) {
        backbuf.reserve(size);
        return true;
    }
    inline unsigned int length() const { return static_cast<unsigned int>(backbuf.size()); }

    inline void clear() { backbuf = ""; }

    inline bool isEmpty() const { return length() == 0; }

    unsigned char concat(const String& str) {
        backbuf += str.backbuf;
        return true;
    }
    unsigned char concat(const char* cstr) {
        backbuf += String(cstr).backbuf;
        return true;
    }
    unsigned char concat(char c) {
        backbuf += String(c).backbuf;
        return true;
    }

    unsigned char concat(unsigned char c) {
        backbuf += String(c).backbuf;
        return true;
    }

    unsigned char concat(int num) {
        backbuf += String(num).backbuf;
        return true;
    }

    unsigned char concat(unsigned int num) {
        backbuf += String(num).backbuf;
        return true;
    }

    unsigned char concat(long num) {
        backbuf += String(num).backbuf;
        return true;
    }

    unsigned char concat(unsigned long num) {
        backbuf += String(num).backbuf;
        return true;
    }

    unsigned char concat(float num) {
        backbuf += String(num).backbuf;
        return true;
    }

    unsigned char concat(double num) {
        backbuf += String(num).backbuf;
        return true;
    }
    String& operator+=(const String& rhs) {
        concat(rhs);
        return (*this);
    }
    String& operator+=(const char* cstr) {
        concat(cstr);
        return (*this);
    }
    String& operator+=(char c) {
        concat(c);
        return (*this);
    }
    String& operator+=(unsigned char num) {
        concat(num);
        return (*this);
    }
    String& operator+=(int num) {
        concat(num);
        return (*this);
    }
    String& operator+=(unsigned int num) {
        concat(num);
        return (*this);
    }
    String& operator+=(long num) {
        concat(num);
        return (*this);
    }
    String& operator+=(unsigned long num) {
        concat(num);
        return (*this);
    }
    String& operator+=(float num) {
        concat(num);
        return (*this);
    }
    String& operator+=(double num) {
        concat(num);
        return (*this);
    }

    friend StringAppender& operator+(const StringAppender& lhs, const String& rhs);
    friend StringAppender& operator+(const StringAppender& lhs, const char* cstr);
    friend StringAppender& operator+(const StringAppender& lhs, char c);
    friend StringAppender& operator+(const StringAppender& lhs, unsigned char num);
    friend StringAppender& operator+(const StringAppender& lhs, int num);
    friend StringAppender& operator+(const StringAppender& lhs, unsigned int num);
    friend StringAppender& operator+(const StringAppender& lhs, long num);
    friend StringAppender& operator+(const StringAppender& lhs, unsigned long num);
    friend StringAppender& operator+(const StringAppender& lhs, float num);
    friend StringAppender& operator+(const StringAppender& lhs, double num);

    operator StringIfHelperType() const { return buffer() ? &String::StringIfHelper : 0; }

    int           compareTo(const String& s) const { return backbuf.compare(s.backbuf); }
    unsigned char equals(const String& s) const { return backbuf == s.backbuf; }
    unsigned char equals(const char* cstr) const { return backbuf == cstr; }
    unsigned char operator==(const String& rhs) const { return equals(rhs); }
    unsigned char operator==(const char* cstr) const { return equals(cstr); }
    unsigned char operator!=(const String& rhs) const { return !equals(rhs); }
    unsigned char operator!=(const char* cstr) const { return !equals(cstr); }
    unsigned char operator<(const String& rhs) const { return compareTo(rhs) < 0; }
    unsigned char operator>(const String& rhs) const { return compareTo(rhs) > 0; }
    unsigned char operator<=(const String& rhs) const { return compareTo(rhs) <= 0; }
    unsigned char operator>=(const String& rhs) const { return compareTo(rhs) >= 0; }
    // unsigned char equalsIgnoreCase(const String& s) const;
    // unsigned char equalsConstantTime(const String& s) const;
    unsigned char startsWith(const String& prefix) const {
        auto pref = backbuf.substr(0, prefix.backbuf.size());
        return pref == prefix.backbuf;
    }
    unsigned char startsWith(const String& prefix, unsigned int offset) const {
        if (prefix.length() > backbuf.size()) {
            return false;
        }
        auto pref = backbuf.substr(offset, prefix.backbuf.size() - offset);
        return pref == prefix.backbuf;
    }
    unsigned char endsWith(const String& suffix) const {
        if (suffix.length() > backbuf.size()) {
            return false;
        }
        auto suff = backbuf.substr(backbuf.size() - suffix.backbuf.size());
        return suff == suffix.backbuf;
    }

    // character access
    char  charAt(unsigned int index) const { return backbuf[index]; }
    void  setCharAt(unsigned int index, char c) { backbuf[index] = c; }
    char  operator[](unsigned int index) const { return backbuf[index]; }
    char& operator[](unsigned int index) { return (&*backbuf.begin())[index]; }
    void  getBytes(unsigned char* buf, unsigned int bufsize, unsigned int index = 0) const {
        for (unsigned int i = 0; i < bufsize; ++i) {
            buf[i] = backbuf[i + index];
        }
    }
    void toCharArray(char* buf, unsigned int bufsize, unsigned int index = 0) const { getBytes((unsigned char*)buf, bufsize, index); }
    const char* c_str() const { return backbuf.c_str(); }
    char*       begin() { return backbuf.size() == 0 ? nullptr : &*backbuf.begin(); }
    char*       end() { return begin() + backbuf.size(); }
    const char* begin() const { return backbuf.size() == 0 ? nullptr : &*backbuf.begin(); }
    const char* end() const { return begin() + backbuf.size(); }

    // search
    int indexOf(char ch) const { return int(std::find(backbuf.begin(), backbuf.end(), ch) - backbuf.begin()); }
    int indexOf(char ch, unsigned int fromIndex) const {
        return int(std::find(backbuf.begin() + fromIndex, backbuf.end(), ch) - backbuf.begin());
    }

    // int    indexOf(const String& str) const;
    // int    indexOf(const String& str, unsigned int fromIndex) const;
    // int    lastIndexOf(char ch) const;
    // int    lastIndexOf(char ch, unsigned int fromIndex) const;
    // int    lastIndexOf(const String& str) const;
    // int    lastIndexOf(const String& str, unsigned int fromIndex) const;

    String substring(int index, int length) { return String(backbuf.substr(index, length)); }
    String substring(int index) { return String(backbuf.substr(index)); }

    // modification
    void replace(char find, char replace) {
        std::string s2 = backbuf;
        for (auto& it : s2) {
            if (it == find) {
                it = replace;
            }
        }
        backbuf = s2;
    }
    void replace(const String& find, const String& replace) {
        std::string s2  = backbuf;
        auto        idx = s2.find(find.backbuf, 0);
        s2              = s2.substr(0, idx) + replace.backbuf + s2.substr(idx + find.backbuf.size());
        backbuf         = s2;
    }
    // void remove(unsigned int index);
    // void remove(unsigned int index, unsigned int count);
    void trim();

    // parsing/conversion
    long   toInt() const { return atoi(backbuf.c_str()); }
    float  toFloat() const { return std::stof(backbuf.c_str()); }
    double toDouble() const { return std::stod(backbuf.c_str()); }

    inline void toLowerCase() {
        for (auto& it : backbuf) {
            it = std::tolower(it);
        }
    }
    inline void toUpperCase() {
        for (auto& it : backbuf) {
            it = std::toupper(it);
        }
    }
};

class StringAppender : public String {
public:
    StringAppender(const String& s) : String(s) {}
    StringAppender(const char* p) : String(p) {}
    StringAppender(char c) : String(c) {}
    StringAppender(unsigned char num) : String(num) {}
    StringAppender(int num) : String(num) {}
    StringAppender(unsigned int num) : String(num) {}
    StringAppender(long num) : String(num) {}
    StringAppender(unsigned long num) : String(num) {}
    StringAppender(float num) : String(num) {}
    StringAppender(double num) : String(num) {}
};

int strcasecmp(const char* lhs, const char* rhs);
int strncasecmp(const char* lhs, const char* rhs, size_t count);
