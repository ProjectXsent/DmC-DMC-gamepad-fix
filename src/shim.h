#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace dmc {
// Called from DllMain(DLL_PROCESS_ATTACH). Must only do loader-lock-safe work.
void ShimAttach(HMODULE self);
}
