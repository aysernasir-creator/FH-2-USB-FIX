// SPDX-License-Identifier: MIT
// Thread-safe logging to FH2SimImmersion.log.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace fh2 {

class Log {
public:
    static void open(const wchar_t* path) {
        InitializeCriticalSection(&cs_);
        file_ = _wfopen(path, L"w");
        ready_ = true;
    }
    static void write(const char* fmt, ...) {
        if (!ready_) return;
        EnterCriticalSection(&cs_);
        if (file_) {
            SYSTEMTIME t; GetLocalTime(&t);
            fprintf(file_, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
            va_list ap; va_start(ap, fmt); vfprintf(file_, fmt, ap); va_end(ap);
            fputc('\n', file_);
            fflush(file_);
        }
        LeaveCriticalSection(&cs_);
    }
private:
    static inline CRITICAL_SECTION cs_;
    static inline FILE* file_ = nullptr;
    static inline bool ready_ = false;
};

} // namespace fh2

#define LOG(...) ::fh2::Log::write(__VA_ARGS__)
