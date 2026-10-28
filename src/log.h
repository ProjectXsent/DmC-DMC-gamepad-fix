// Optional file logger (Windows). Disabled unless [General] log=1.
#pragma once

namespace dmc { namespace log {

void Init(bool enabled, const wchar_t* path);
void Write(const char* fmt, ...);
bool Enabled();     // true once the log file is open
double NowMs();     // monotonic milliseconds (QueryPerformanceCounter)

}}  // namespace dmc::log
