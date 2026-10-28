#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "config.h"

namespace dmc {
// Called from DllMain(DLL_PROCESS_ATTACH). Must only do loader-lock-safe work.
void ShimAttach(HMODULE self);

// Thread-safe, lazy: reads dmc_sdl.ini and opens the log on first use, then returns the
// parsed configuration. Safe to call from any thread after DllMain.
const Config& ShimConfig();

// True when the calling thread is currently inside SDL (re-entrancy guard).
bool ShimInSdlCall();
}
