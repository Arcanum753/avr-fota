#ifndef _MOCK_WSTRING_H
#define _MOCK_WSTRING_H

// Хост-мок Arduino String для native-тестов ядра.
// Наследует std::string, добавляя Arduino-специфичный API.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

class String : public std::string {
private:
    // Скрываем std::string::const_iterator от SFINAE ArduinoJson: иначе String
    // матчится и как Arduino-строка, и как контейнер (неоднозначность Reader).
    using const_iterator = void;
public:
    String() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(const String& s) : std::string(s) {}
    String(char c) : std::string(1, c) {}
    String(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); assign(b); }
    String(unsigned int v) { char b[32]; snprintf(b, sizeof(b), "%u", v); assign(b); }
    String(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); assign(b); }
    String(unsigned long v) { char b[32]; snprintf(b, sizeof(b), "%lu", v); assign(b); }
    String(long long v) { char b[32]; snprintf(b, sizeof(b), "%lld", v); assign(b); }
    String(unsigned long long v) { char b[32]; snprintf(b, sizeof(b), "%llu", v); assign(b); }
    String(float v, int decimals = 2) { char b[64]; snprintf(b, sizeof(b), "%.*f", decimals, (double)v); assign(b); }
    String(double v, int decimals = 2) { char b[64]; snprintf(b, sizeof(b), "%.*f", decimals, v); assign(b); }

    unsigned int length() const { return (unsigned int)std::string::size(); }
    char charAt(unsigned int i) const { return (i < std::string::size()) ? (*this)[i] : (char)0; }

    int toInt() const { return (int)strtol(c_str(), nullptr, 10); }
    float toFloat() const { return (float)atof(c_str()); }
    double toDouble() const { return atof(c_str()); }

    bool equals(const String& s) const { return *this == s; }
    bool equalsIgnoreCase(const String& s) const {
        if (length() != s.length()) return false;
        for (unsigned int i = 0; i < length(); i++) {
            if (tolower((*this)[i]) != tolower(s[i])) return false;
        }
        return true;
    }

    bool startsWith(const String& s) const {
        return length() >= s.length() && compare(0, s.length(), s) == 0;
    }
    bool endsWith(const String& s) const {
        return length() >= s.length() && compare(length() - s.length(), s.length(), s) == 0;
    }

    int indexOf(const String& s) const {
        size_t p = find(s);
        return (p == std::string::npos) ? -1 : (int)p;
    }
    int indexOf(char c) const {
        size_t p = find(c);
        return (p == std::string::npos) ? -1 : (int)p;
    }
    int indexOf(const String& s, unsigned int from) const {
        if (from > length()) return -1;
        size_t p = find(s, from);
        return (p == std::string::npos) ? -1 : (int)p;
    }
    int indexOf(char c, unsigned int from) const {
        if (from > length()) return -1;
        size_t p = find(c, from);
        return (p == std::string::npos) ? -1 : (int)p;
    }
    int lastIndexOf(const String& s) const {
        size_t p = rfind(s);
        return (p == std::string::npos) ? -1 : (int)p;
    }
    int lastIndexOf(char c) const {
        size_t p = rfind(c);
        return (p == std::string::npos) ? -1 : (int)p;
    }

    String substring(unsigned int from) const { return String(std::string::substr(from)); }
    String substring(unsigned int from, unsigned int to) const {
        if (from > to) return String();
        return String(std::string::substr(from, to - from));
    }

    void replace(const String& a, const String& b) {
        if (a.length() == 0) return;
        size_t pos = 0;
        while ((pos = find(a, pos)) != std::string::npos) {
            std::string::replace(pos, a.length(), b);
            pos += b.length();
        }
    }

    void trim() {
        size_t b = find_first_not_of(" \t\r\n");
        size_t e = find_last_not_of(" \t\r\n");
        if (b == std::string::npos) { clear(); return; }
        assign(substr(b, e - b + 1));
    }

    // Arduino String::concat возвращает признак успеха (нужно ArduinoJson).
    bool concat(const String& s) { append(s); return true; }
    bool concat(char c) { push_back(c); return true; }
    bool concat(const char* s) { if (s) append(s); return true; }
    void reserve(unsigned int n) { std::string::reserve(n); }
    void remove(unsigned int index) { if (index < size()) erase(index); }
    void remove(unsigned int index, unsigned int count) { if (index < size()) erase(index, count); }
    void toUpperCase() { for (auto& c : *this) c = (char)toupper((unsigned char)c); }
    void toLowerCase() { for (auto& c : *this) c = (char)tolower((unsigned char)c); }
    bool isEmpty() const { return empty(); }

    String& operator=(const char* s) { assign(s ? s : ""); return *this; }
    String& operator=(const std::string& s) { assign(s); return *this; }
    String& operator=(char c) { assign(1, c); return *this; }
    String& operator+=(const String& s) { append(s); return *this; }
    String& operator+=(const std::string& s) { append(s); return *this; }
    String& operator+=(const char* s) { if (s) append(s); return *this; }
    String& operator+=(char c) { push_back(c); return *this; }
    String& operator+=(int v) { return (*this += String(v)); }
    String& operator+=(unsigned int v) { return (*this += String(v)); }
    String& operator+=(long v) { return (*this += String(v)); }
    String& operator+=(unsigned long v) { return (*this += String(v)); }
    String& operator+=(float v) { return (*this += String(v)); }
    String& operator+=(double v) { return (*this += String(v)); }
};

inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, char b) { String r(a); r += b; return r; }
inline String operator+(char a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, int b) { String r(a); r += String(b); return r; }
inline String operator+(const String& a, unsigned int b) { String r(a); r += String(b); return r; }
inline String operator+(const String& a, long b) { String r(a); r += String(b); return r; }
inline String operator+(const String& a, unsigned long b) { String r(a); r += String(b); return r; }
inline String operator+(const String& a, float b) { String r(a); r += String(b); return r; }
inline String operator+(const String& a, double b) { String r(a); r += String(b); return r; }

inline bool operator==(const String& a, const char* b) { return strcmp(a.c_str(), b ? b : "") == 0; }
inline bool operator==(const char* a, const String& b) { return b == a; }
inline bool operator!=(const String& a, const char* b) { return !(a == b); }
inline bool operator!=(const char* a, const String& b) { return !(b == a); }

#endif // _MOCK_WSTRING_H
