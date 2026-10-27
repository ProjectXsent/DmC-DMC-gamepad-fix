// Optional file logger (Windows). Disabled unless [General] log=1.
#pragma once

namespace dmc { namespace log {

void Init(bool enabled, const wchar_t* path);
void Write(const char* fmt, ...);

}}  // namespace dmc::log
