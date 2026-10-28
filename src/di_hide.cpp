// IDirectInput8 vtable: 0 QueryInterface, 1 AddRef, 2 Release, 3 CreateDevice,
// 4 EnumDevices, 5 GetDeviceStatus, 6 RunControlPanel, 7 Initialize, 8 FindDevice,
// 9 EnumDevicesBySemantics, 10 ConfigureDevices.
// IDirectInput8A and IDirectInput8W have separate vtables, so both are handled
// (template parameter W).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "di_filter.h"
#include "di_hide.h"
#include "log.h"

namespace dmc { namespace dihide {

namespace {

constexpr int  kSlotEnumDevices = 4;
constexpr int  kSlotEnumBySemantics = 9;
constexpr BOOL kEnumContinue = 1;  // DIENUM_CONTINUE

// {BF798030-483A-4DA2-AA99-5D64ED369700} / {BF798031-...}
const GUID kIID_IDirectInput8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
const GUID kIID_IDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

typedef HRESULT (WINAPI *PFN_EnumDevices)(void* self, DWORD devType, void* cb, void* ref, DWORD flags);
typedef HRESULT (WINAPI *PFN_EnumBySemantics)(void* self, const void* user, void* fmt, void* cb, void* ref, DWORD flags);
typedef BOOL (WINAPI *EnumCb)(const DIDevHead*, void*);
typedef BOOL (WINAPI *EnumSemCb)(const DIDevHead*, void*, DWORD, DWORD, void*);

CRITICAL_SECTION g_lock;
bool             g_hide = true;
CallerIsSdlFn    g_guard = nullptr;

bool ShouldHide(const DIDevHead* inst) {
    bool pad = inst && IsPadDevType(inst->dwDevType);
    bool hide = pad && g_hide && !(g_guard && g_guard());
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

template <int W> void PatchOne(void* obj) {
    void** vt = *(void***)obj;
    bool a = PatchSlot(vt, kSlotEnumDevices, (void*)&Hooks<W>::Enum, &Hooks<W>::origEnum);
    bool b = PatchSlot(vt, kSlotEnumBySemantics, (void*)&Hooks<W>::EnumSem, &Hooks<W>::origSem);
    log::Write("patched IDirectInput8%c: EnumDevices=%d EnumDevicesBySemantics=%d", W ? 'W' : 'A', (int)a, (int)b);
}

}  // namespace

void Attach() { InitializeCriticalSection(&g_lock); }

void Configure(bool hide, CallerIsSdlFn guard) {
    g_hide = hide;
    g_guard = guard;
}

void PatchInterface(REFIID riid, void* iface) {
    if (!iface) return;
    EnterCriticalSection(&g_lock);
    if (IsEqualGUID(riid, kIID_IDirectInput8A)) PatchOne<0>(iface);
    else if (IsEqualGUID(riid, kIID_IDirectInput8W)) PatchOne<1>(iface);
    else log::Write("DirectInput8Create: unrecognised IID; not patched");
    LeaveCriticalSection(&g_lock);
}

}}  // namespace dmc::dihide
