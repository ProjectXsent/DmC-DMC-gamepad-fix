// dinput8.dll proxy. Forwards DirectInput8Create to the real system DLL, then
// patches the returned IDirectInput8 so that gamepad-class devices are not
// reported to the game (see di_filter.h for the why). Keyboard/mouse untouched.
//
// Hooking is done by patching two slots of the interface's vtable in place; this
// avoids writing a full COM wrapper. IDirectInput8A and IDirectInput8W have
// separate vtables, so both are handled (template parameter W).
//
// IDirectInput8 vtable: 0 QueryInterface, 1 AddRef, 2 Release, 3 CreateDevice,
// 4 EnumDevices, 5 GetDeviceStatus, 6 RunControlPanel, 7 Initialize, 8 FindDevice,
// 9 EnumDevicesBySemantics, 10 ConfigureDevices.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include "config.h"
#include "di_filter.h"
#include "log.h"
#include "winutil.h"

using namespace dmc;

namespace {

constexpr int kSlotEnumDevices = 4;
constexpr int kSlotEnumBySemantics = 9;
constexpr BOOL kEnumContinue = 1;  // DIENUM_CONTINUE

// {BF798030-483A-4DA2-AA99-5D64ED369700} / {BF798031-...}
const GUID kIID_IDirectInput8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
const GUID kIID_IDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

typedef HRESULT (WINAPI *PFN_Create)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
typedef HRESULT (WINAPI *PFN_CanUnload)();
typedef HRESULT (WINAPI *PFN_GetClassObject)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT (WINAPI *PFN_EnumDevices)(void* self, DWORD devType, void* cb, void* ref, DWORD flags);
typedef HRESULT (WINAPI *PFN_EnumBySemantics)(void* self, const void* user, void* fmt, void* cb, void* ref, DWORD flags);
typedef BOOL (WINAPI *EnumCb)(const DIDevHead*, void*);
typedef BOOL (WINAPI *EnumSemCb)(const DIDevHead*, void*, DWORD, DWORD, void*);
typedef BOOL (WINAPI *PFN_InSdlCall)();

HMODULE          g_module = nullptr;
HMODULE          g_real = nullptr;
CRITICAL_SECTION g_lock;
bool             g_inited = false;
bool             g_hide = true;
PFN_Create       g_realCreate = nullptr;

// Is the calling thread inside SDL (xinput1_3.dll's guard)? If SDL itself ever
// enumerates DirectInput through this proxy, it must see the pads.
bool CallerIsSdl() {
    static PFN_InSdlCall fn = nullptr;
    if (!fn) {
        HMODULE x = GetModuleHandleW(L"xinput1_3.dll");
        if (x) fn = (PFN_InSdlCall)GetProcAddress(x, "DmcInSdlCall");  // null for the genuine DLL
    }
    return fn && fn();
}

bool ShouldHide(const DIDevHead* inst) {
    bool pad = inst && IsPadDevType(inst->dwDevType);
    bool hide = pad && g_hide && !CallerIsSdl();
    log::Write("DirectInput device type=0x%08lX %s", (unsigned long)(inst ? inst->dwDevType : 0),
               hide ? "-> hidden" : (pad ? "(pad, visible: SDL caller)" : "-> visible"));
    return hide;
}

struct Ctx { void* cb; void* ref; };

BOOL WINAPI EnumThunk(const DIDevHead* inst, void* ref) {
    Ctx* c = (Ctx*)ref;
    if (ShouldHide(inst)) return kEnumContinue;
    return ((EnumCb)c->cb)(inst, c->ref);
}

// NOTE: when hiding a device here we deliberately do NOT Release the device
// object DirectInput passes in. Whether the callback owns that reference isn't
// something we can verify; a one-off leak is far safer than a double release.
BOOL WINAPI EnumSemThunk(const DIDevHead* inst, void* dev, DWORD flags, DWORD remaining, void* ref) {
    Ctx* c = (Ctx*)ref;
    if (ShouldHide(inst)) return kEnumContinue;
    return ((EnumSemCb)c->cb)(inst, dev, flags, remaining, c->ref);
}

template <int W> struct Hooks {
    static void* origEnum;
    static void* origSem;

    static HRESULT WINAPI Enum(void* self, DWORD devType, void* cb, void* ref, DWORD flags) {
        PFN_EnumDevices orig = (PFN_EnumDevices)origEnum;
        if (!cb) return orig(self, devType, cb, ref, flags);
        Ctx c = {cb, ref};
        return orig(self, devType, (void*)&EnumThunk, &c, flags);
    }
    static HRESULT WINAPI EnumSem(void* self, const void* user, void* fmt, void* cb, void* ref, DWORD flags) {
        PFN_EnumBySemantics orig = (PFN_EnumBySemantics)origSem;
        if (!cb) return orig(self, user, fmt, cb, ref, flags);
        Ctx c = {cb, ref};
        return orig(self, user, fmt, (void*)&EnumSemThunk, &c, flags);
    }
};
template <int W> void* Hooks<W>::origEnum = nullptr;
template <int W> void* Hooks<W>::origSem = nullptr;

bool PatchSlot(void** vtable, int index, void* hook, void** orig) {
    if (vtable[index] == hook) return true;  // already patched (vtable is shared by all instances)
    DWORD old = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = vtable[index];
    vtable[index] = hook;
    DWORD ignore = 0;
    VirtualProtect(&vtable[index], sizeof(void*), old, &ignore);
    return true;
}

template <int W> void PatchInterface(void* obj) {
    void** vt = *(void***)obj;
    bool a = PatchSlot(vt, kSlotEnumDevices, (void*)&Hooks<W>::Enum, &Hooks<W>::origEnum);
    bool b = PatchSlot(vt, kSlotEnumBySemantics, (void*)&Hooks<W>::EnumSem, &Hooks<W>::origSem);
    log::Write("patched IDirectInput8%c: EnumDevices=%d EnumDevicesBySemantics=%d", W ? 'W' : 'A', (int)a, (int)b);
}

void Init() {
    EnterCriticalSection(&g_lock);
    if (!g_inited) {
        g_inited = true;
        std::wstring dir = winutil::ModuleDir(g_module);
        Config cfg;
        ParseConfigText(winutil::ReadWholeFile(dir + L"\\dmc_sdl.ini").c_str(), cfg);
        g_hide = cfg.hide_dinput_pads && cfg.mode != Mode::Passthrough;
        log::Init(cfg.log, (dir + L"\\dmc_sdl_dinput.log").c_str());
        log::Write("dinput8 proxy starting; hide_dinput_pads=%d", (int)g_hide);

        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process
        if (n && n < MAX_PATH - 20) {
            lstrcatW(path, L"\\dinput8.dll");
            g_real = LoadLibraryW(path);  // full path: never loads ourselves
        }
        if (g_real) g_realCreate = (PFN_Create)GetProcAddress(g_real, "DirectInput8Create");
        log::Write("real dinput8: module=%p DirectInput8Create=%p", (void*)g_real, (void*)g_realCreate);
    }
    LeaveCriticalSection(&g_lock);
}

}  // namespace

extern "C" {

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, void* outer) {
    Init();
    if (!g_realCreate) return (HRESULT)0x80004005L;  // E_FAIL
    HRESULT hr = g_realCreate(inst, version, riid, out, outer);
    if (hr >= 0 && out && *out && g_hide) {
        EnterCriticalSection(&g_lock);
        if (IsEqualGUID(riid, kIID_IDirectInput8A)) PatchInterface<0>(*out);
        else if (IsEqualGUID(riid, kIID_IDirectInput8W)) PatchInterface<1>(*out);
        else log::Write("DirectInput8Create: unrecognised IID; not patched");
        LeaveCriticalSection(&g_lock);
    }
    return hr;
}

HRESULT WINAPI DllCanUnloadNow() {
    Init();
    PFN_CanUnload f = g_real ? (PFN_CanUnload)GetProcAddress(g_real, "DllCanUnloadNow") : nullptr;
    return f ? f() : (HRESULT)1L;  // S_FALSE
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    Init();
    PFN_GetClassObject f = g_real ? (PFN_GetClassObject)GetProcAddress(g_real, "DllGetClassObject") : nullptr;
    return f ? f(clsid, riid, out) : (HRESULT)0x80040111L;  // CLASS_E_CLASSNOTAVAILABLE
}

}  // extern "C"

// Loader-lock rules: only record the handle and set up the critical section here.
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        g_module = inst;
        InitializeCriticalSection(&g_lock);
    }
    return TRUE;
}
