#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "shim.h"

// Loader-lock rules: do nothing here except record the module handle and set up
// a critical section. SDL, LoadLibrary and file I/O all happen lazily on the
// first XInput call (see EnsureInit in xinput_shim.cpp).
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        dmc::ShimAttach(inst);
    }
    return TRUE;
}
