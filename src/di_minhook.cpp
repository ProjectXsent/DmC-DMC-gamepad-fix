#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include "config.h"
#include "di_hide.h"
#include "di_minhook.h"
#include "log.h"
#include "shim.h"

namespace dmc { namespace dimh {

namespace {

typedef HRESULT (WINAPI *PFN_Create)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);

PFN_Create     g_orig = nullptr;
volatile LONG  g_installed = 0;        // 1 once the detour is enabled
bool           g_needRetry = false;    // dinput8 wasn't mapped yet
const char*    g_status = "not attempted";
const char*    g_where = "";           // set if hooking a non-System32 dinput8.dll

// Runs on the game's thread whenever it calls DirectInput8Create.
HRESULT WINAPI Detour(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, void* outer) {
    HRESULT hr = g_orig(inst, version, riid, out, outer);
    if (hr >= 0 && out && *out) {
        const Config& cfg = ShimConfig();   // loads dmc_sdl.ini + log on first use
        bool hide = cfg.hide_dinput_pads && cfg.mode != Mode::Passthrough;
        if (hide) {
            dihide::Configure(true, &ShimInSdlCall);
            dihide::PatchInterface(riid, *out);
        } else {
            log::Write("DirectInput8Create intercepted; hiding disabled by config");
        }
    }
    return hr;
}

bool IEqualPrefix(const wchar_t* path, const wchar_t* prefix, UINT n) {
    for (UINT i = 0; i < n; ++i) {
        wchar_t a = path[i], b = prefix[i];
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a + 32);
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b + 32);
        if (a != b) return false;
    }
    return true;
}

// If the dinput8.dll we're about to hook isn't the genuine System32/SysWOW64 one
// (e.g. a dinput8.dll left in the game folder by an older version of this project,
// or another tool such as ReShade), say so in the log.
void NoteModuleOrigin() {
    HMODULE m = GetModuleHandleW(L"dinput8");
    wchar_t path[MAX_PATH], sys[MAX_PATH];
    DWORD pn = m ? GetModuleFileNameW(m, path, MAX_PATH) : 0;
    UINT sn = GetSystemDirectoryW(sys, MAX_PATH);
    if (pn && sn && !(pn > sn && IEqualPrefix(path, sys, sn)))
        g_where = " (WARNING: hooked a dinput8.dll outside the system folder -- remove it from the game folder unless intended)";
}

void TryInstall() {
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) { g_status = MH_StatusToString(st); return; }

    // MH_CreateHookApi uses GetModuleHandle only (never LoadLibrary), so it is safe here.
    st = MH_CreateHookApi(L"dinput8", "DirectInput8Create", (LPVOID)&Detour, (LPVOID*)&g_orig);
    if (st == MH_ERROR_MODULE_NOT_FOUND) { g_needRetry = true; g_status = "dinput8.dll not mapped yet; will retry"; return; }
    if (st != MH_OK) { g_status = MH_StatusToString(st); return; }

    st = MH_EnableHook(MH_ALL_HOOKS);
    if (st != MH_OK) { g_status = MH_StatusToString(st); return; }
    NoteModuleOrigin();
    g_needRetry = false;
    InterlockedExchange(&g_installed, 1);
    g_status = "installed";
}

}  // namespace

void InstallEarly() { TryInstall(); }

void RetryIfNeeded() {
    if (g_needRetry && !InterlockedCompareExchange(&g_installed, 0, 0)) {
        TryInstall();
        log::Write("DirectInput hook retry: %s", g_status);
    }
}

const char* Status() {
    static char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%s", g_status, g_where);
    return buf;
}

}}  // namespace dmc::dimh
