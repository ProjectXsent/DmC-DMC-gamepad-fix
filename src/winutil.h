// Small Windows helpers shared by both proxy DLLs.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>

namespace dmc { namespace winutil {

std::wstring ModuleDir(HMODULE module);               // folder containing the DLL
std::string  ToUtf8(const std::wstring& w);
std::string  ReadWholeFile(const std::wstring& path); // "" if missing; strips UTF-8 BOM

}}  // namespace dmc::winutil
