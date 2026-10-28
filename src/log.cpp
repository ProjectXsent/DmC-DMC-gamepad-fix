#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include "log.h"

namespace dmc { namespace log {

static HANDLE g_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs;
static bool g_csInit = false;

void Init(bool enabled, const wchar_t* path) {
    if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
    if (!enabled || g_file != INVALID_HANDLE_VALUE) return;
    g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool Enabled() { return g_file != INVALID_HANDLE_VALUE; }

double NowMs() {
    static LONGLONG freq = 0;  // benign race: every thread computes the same value
    if (!freq) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = f.QuadPart; }
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq;
}

void Write(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    SYSTEMTIME t; GetLocalTime(&t);
    int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[%02d:%02d:%02d.%03d] ",
                        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    if (n < 0) n = 0;
    va_list ap; va_start(ap, fmt);
    int m = _vsnprintf_s(buf + n, sizeof(buf) - n - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (m < 0) m = (int)sizeof(buf) - n - 3;  // truncated
    int len = n + m;
    buf[len++] = '\r'; buf[len++] = '\n';
    EnterCriticalSection(&g_cs);
    DWORD written = 0;
    WriteFile(g_file, buf, (DWORD)len, &written, nullptr);
    LeaveCriticalSection(&g_cs);
}

}}  // namespace dmc::log
