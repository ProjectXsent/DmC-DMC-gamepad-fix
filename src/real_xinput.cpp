#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "real_xinput.h"
#include "log.h"

namespace dmc { namespace real {

typedef DWORD (WINAPI *PFN_GetState)(DWORD, XInputState*);
typedef DWORD (WINAPI *PFN_SetState)(DWORD, XInputVibration*);
typedef DWORD (WINAPI *PFN_GetCaps)(DWORD, DWORD, XInputCapabilities*);
typedef void  (WINAPI *PFN_Enable)(BOOL);
typedef DWORD (WINAPI *PFN_Battery)(DWORD, BYTE, XInputBatteryInformation*);
typedef DWORD (WINAPI *PFN_Keystroke)(DWORD, DWORD, void*);
typedef DWORD (WINAPI *PFN_DSound)(DWORD, void*, void*);

static HMODULE       g_mod = nullptr;
static bool          g_tried = false;
static PFN_GetState  p_GetState;
static PFN_SetState  p_SetState;
static PFN_GetCaps   p_GetCaps;
static PFN_Enable    p_Enable;
static PFN_Battery   p_Battery;
static PFN_Keystroke p_Keystroke;
static PFN_DSound    p_DSound;

// Resolve by name first (the real DLL exports names), falling back to the
// ordinal the game itself uses.
static FARPROC Resolve(const char* name, WORD ordinal) {
    FARPROC p = GetProcAddress(g_mod, name);
    if (!p) p = GetProcAddress(g_mod, MAKEINTRESOURCEA(ordinal));
    return p;
}

bool Load() {
    if (g_tried) return g_mod != nullptr;  // callers hold the shim's init lock
    g_tried = true;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process
    if (n == 0 || n > MAX_PATH - 20) return false;
    lstrcatW(path, L"\\xinput1_3.dll");
    g_mod = LoadLibraryW(path);
    if (!g_mod) { log::Write("real xinput1_3.dll not found (err %lu)", GetLastError()); return false; }
    p_GetState  = (PFN_GetState) Resolve("XInputGetState", 2);
    p_SetState  = (PFN_SetState) Resolve("XInputSetState", 3);
    p_GetCaps   = (PFN_GetCaps)  Resolve("XInputGetCapabilities", 4);
    p_Enable    = (PFN_Enable)   Resolve("XInputEnable", 5);
    p_DSound    = (PFN_DSound)   Resolve("XInputGetDSoundAudioDeviceGuids", 6);
    p_Battery   = (PFN_Battery)  Resolve("XInputGetBatteryInformation", 7);
    p_Keystroke = (PFN_Keystroke)Resolve("XInputGetKeystroke", 8);
    log::Write("real xinput1_3 loaded: GetState=%p SetState=%p", (void*)p_GetState, (void*)p_SetState);
    return true;
}

bool Available() { return g_mod != nullptr; }

uint32_t GetState(uint32_t u, XInputState* s) {
    return p_GetState ? p_GetState(u, s) : XERR_DEVICE_NOT_CONNECTED;
}
uint32_t SetState(uint32_t u, XInputVibration* v) {
    return p_SetState ? p_SetState(u, v) : XERR_DEVICE_NOT_CONNECTED;
}
uint32_t GetCapabilities(uint32_t u, uint32_t f, XInputCapabilities* c) {
    return p_GetCaps ? p_GetCaps(u, f, c) : XERR_DEVICE_NOT_CONNECTED;
}
void Enable(int e) { if (p_Enable) p_Enable(e ? TRUE : FALSE); }
uint32_t GetBatteryInformation(uint32_t u, uint8_t t, XInputBatteryInformation* i) {
    return p_Battery ? p_Battery(u, t, i) : XERR_DEVICE_NOT_CONNECTED;
}
uint32_t GetKeystroke(uint32_t u, uint32_t r, void* k) {
    return p_Keystroke ? p_Keystroke(u, r, k) : XERR_EMPTY;
}
uint32_t GetDSoundAudioDeviceGuids(uint32_t u, void* r, void* c) {
    return p_DSound ? p_DSound(u, r, c) : XERR_DEVICE_NOT_CONNECTED;
}

}}  // namespace dmc::real
