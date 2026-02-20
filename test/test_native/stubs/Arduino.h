// ============================================================================
// Arduino.h STUB — Minimal Arduino API for native unit tests
// ============================================================================
// Provides just enough of the Arduino API to compile Config.h, Types.h,
// ChaosPatterns.h, and Validators.h on the host PC (no hardware).
// ============================================================================

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>

// ============================================================================
// Arduino Constants
// ============================================================================
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef PI
#define PI 3.14159265358979323846
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 2
#endif

// ============================================================================
// Arduino Math
// ============================================================================
using std::min;
using std::max;
using std::abs;

template<typename T>
inline T constrain(T x, T lo, T hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// ============================================================================
// Arduino String (minimal implementation for compilation + basic tests)
// ============================================================================
class String {
    std::string _s;
public:
    String() : _s() {}
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(int val) : _s(std::to_string(val)) {}
    String(long val) : _s(std::to_string(val)) {}
    String(unsigned long val) : _s(std::to_string(val)) {}
    String(float val, int dec = 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", dec, (double)val);
        _s = buf;
    }
    String(double val, int dec = 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", dec, val);
        _s = buf;
    }

    const char* c_str() const { return _s.c_str(); }
    size_t length() const { return _s.length(); }

    String operator+(const String& rhs) const { return String(std::string(_s + rhs._s)); }
    String operator+(const char* rhs) const { return String(std::string(_s + (rhs ? rhs : ""))); }
    friend String operator+(const char* lhs, const String& rhs) { return String(std::string(lhs ? lhs : "") + rhs._s); }
    String& operator+=(const String& rhs) { _s += rhs._s; return *this; }
    String& operator+=(const char* rhs) { if (rhs) _s += rhs; return *this; }
    bool operator==(const String& rhs) const { return _s == rhs._s; }
    bool operator!=(const String& rhs) const { return _s != rhs._s; }
};

// ============================================================================
// Arduino Random
// ============================================================================
inline long random(long max_val) {
    if (max_val <= 0) return 0;
    return std::rand() % max_val;
}
inline long random(long min_val, long max_val) {
    if (max_val <= min_val) return min_val;
    return min_val + std::rand() % (max_val - min_val);
}

// ============================================================================
// Arduino Time (stubs — return 0 for deterministic tests)
// ============================================================================
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
