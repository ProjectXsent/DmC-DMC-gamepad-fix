// The exported XInput surface. Policy (see docs/DESIGN.md):
//   passthrough : always the real xinput1_3.dll
//   sdl         : only SDL pads
//   hybrid      : real XInput first; SDL pad fills in where no real pad exists
//                 (prefer_sdl=1 flips the priority)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <cstring>
#include "shim.h"
#include "config.h"
#include "mapping.h"
#include "log.h"
#include "real_xinput.h"
#include "sdl_backend.h"
#include "winutil.h"
#ifdef DMC_DI_MINHOOK
#include "di_hide.h"
#include "di_minhook.h"
#endif

using namespace dmc;

namespace dmc { namespace sdlb { unsigned long WorkerThreadId(); } }

namespace {

HMODULE          g_module = nullptr;
CRITICAL_SECTION g_lock;               // guards init + all SDL-backend state
volatile LONG    g_initState = 0;      // 0 = not started, 1 = running, 2 = done
volatile LONG    g_busyThread = 0;     // thread currently inside SDL code (re-entrancy guard)
volatile LONG    g_realOk[kMaxUsers];  // last observed "real pad connected" per user
Config           g_cfg;
bool             g_sdlOk = false;
bool             g_enabled = true;     // XInputEnable state

struct SdlSlotState { XInputGamepad last; DWORD packet; bool wasActive; };

// Diagnostic: with log=1, calls that take >= 4 ms are written to dmc_sdl.log (first 40 only).
// A long call on the game's thread is what looks like a freeze / lost focus.
volatile LONG g_slowCount = 0;
struct SlowScope {
    const char* what; DWORD user; double t0; bool on;
    SlowScope(const char* w, DWORD u) : what(w), user(u), t0(0.0), on(log::Enabled()) { if (on) t0 = log::NowMs(); }
    ~SlowScope() {
        if (!on) return;
        double dt = log::NowMs() - t0;
        if (dt >= 4.0 && InterlockedIncrement(&g_slowCount) <= 40)
            log::Write("slow %s (user %lu): %.1f ms on thread %lu", what, user, dt, GetCurrentThreadId());
    }
};
SdlSlotState g_state[kMaxUsers];

// RAII: take the lock and mark this thread as "inside SDL". If SDL (or a driver
// it loads) ends up calling back into our exports on this thread, InSdlCall()
// lets those calls bypass SDL and go straight to the real DLL.
struct SdlGuard {
    SdlGuard()  { EnterCriticalSection(&g_lock); InterlockedExchange(&g_busyThread, (LONG)GetCurrentThreadId()); }
    ~SdlGuard() { InterlockedExchange(&g_busyThread, 0); LeaveCriticalSection(&g_lock); }
};
// True on the game thread while inside SdlGuard AND on the SDL worker thread (which
// runs SDL_Init/enumeration), so SDL's own DirectInput enumeration is never hidden.
bool InSdlCall() {
    const DWORD cur = GetCurrentThreadId();
    if ((DWORD)InterlockedCompareExchange(&g_busyThread, 0, 0) == cur) return true;
    const DWORD w = (DWORD)sdlb::WorkerThreadId();
    return w != 0 && w == cur;
}

// Log real-XInput connect/disconnect transitions per user (only when log=true).
volatile LONG g_realSeen[kMaxUsers];   // 0 = unseen, 1 = not connected, 2 = connected
void NoteReal(DWORD user, DWORD r) {
    if (!log::Enabled()) return;
    LONG now = (r == XERR_SUCCESS) ? 2 : 1;
    if (InterlockedExchange(&g_realSeen[user], now) != now)
        log::Write("real XInput user %lu: %s (code %lu)", user, now == 2 ? "connected" : "not connected", r);
}

bool RealProbe(int user) {
    XInputState s;
    return real::GetState((uint32_t)user, &s) == XERR_SUCCESS;
}

bool g_cfgLoaded = false;

// Caller holds g_lock.
void LoadConfigLocked() {
    std::wstring dir = winutil::ModuleDir(g_module);
    std::string ini = winutil::ReadWholeFile(dir + L"\\dmc_sdl.ini");
    ParseConfigText(ini.c_str(), g_cfg);
    log::Init(g_cfg.log, (dir + L"\\dmc_sdl.log").c_str());
    log::Write("dmc-sdl-controller starting; mode=%d prefer_sdl=%d hide_dinput_pads=%d ini=%s", (int)g_cfg.mode,
               (int)g_cfg.prefer_sdl, (int)g_cfg.hide_dinput_pads, ini.empty() ? "(missing, defaults)" : "loaded");
#ifdef DMC_DI_MINHOOK
    log::Write("DirectInput hook (MinHook): %s", dimh::Status());
#endif
}

void DoInit() {
    const double init0 = log::NowMs();
    ShimConfig();  // no-op if the DirectInput detour already loaded it
    std::wstring dir = winutil::ModuleDir(g_module);
#ifdef DMC_DI_MINHOOK
    dimh::RetryIfNeeded();
#endif

    real::Load();

    if (g_cfg.mode != Mode::Passthrough) {
        std::string mp = g_cfg.mappings_file;
        if (!mp.empty() && mp.find(':') == std::string::npos && mp[0] != '\\' && mp[0] != '/')
            mp = winutil::ToUtf8(dir) + "\\" + mp;
        SdlGuard guard;
        g_sdlOk = sdlb::Init(g_cfg, mp, g_cfg.mode == Mode::Hybrid ? &RealProbe : nullptr);
        if (!g_sdlOk) log::Write("SDL unavailable; falling back to real XInput only");
    }
    log::Write("first XInput call: init took %.1f ms total, on thread %lu (the game blocks for this long)",
               log::NowMs() - init0, GetCurrentThreadId());
}

// Lazy, one-time init on the first XInput call. Never runs in DllMain (loader
// lock). Returns false only when called re-entrantly from within init itself.
bool EnsureInit() {
    if (InterlockedCompareExchange(&g_initState, 2, 2) == 2) return true;
    EnterCriticalSection(&g_lock);
    bool ok;
    if (g_initState == 0) {
        g_initState = 1;
        DoInit();
        InterlockedExchange(&g_initState, 2);
        ok = true;
    } else {
        ok = (g_initState == 2);  // state 1 here = same thread re-entering init
    }
    LeaveCriticalSection(&g_lock);
    return ok;
}

bool UseSdl() { return g_sdlOk && g_cfg.mode != Mode::Passthrough; }
bool SdlFirst() { return g_cfg.mode == Mode::Sdl || g_cfg.prefer_sdl; }

// Reads + maps the SDL pad in `user`'s slot. False when no pad is present.
bool ReadSdl(DWORD user, XInputState* out) {
    SdlGuard guard;
    PadSnapshot snap;
    SdlSlotState& st = g_state[user];
    if (!sdlb::ReadSlot((int)user, snap)) { st.wasActive = false; return false; }

    XInputGamepad g = g_enabled ? MapToXInput(snap, g_cfg.mapping) : XInputGamepad();
    if (!st.wasActive || memcmp(&g, &st.last, sizeof(g)) != 0) { ++st.packet; st.last = g; }
    st.wasActive = true;
    out->dwPacketNumber = st.packet;
    out->Gamepad = g;
    return true;
}

bool SdlActive(DWORD user) {
    return sdlb::SlotActive((int)user);   // cached state; never touches SDL on the game thread
}

enum class Src { Real, Sdl };
// Decides who answers capability-style queries for `user`.
Src PickSource(DWORD user) {
    if (!UseSdl()) return Src::Real;
    if (g_cfg.mode == Mode::Sdl) return Src::Sdl;
    if (SdlFirst()) return SdlActive(user) ? Src::Sdl : Src::Real;
    if (InterlockedCompareExchange(&g_realOk[user], 0, 0)) return Src::Real;
    return SdlActive(user) ? Src::Sdl : Src::Real;
}

}  // namespace

namespace dmc {
void ShimAttach(HMODULE self) {
    g_module = self;
    InitializeCriticalSection(&g_lock);
#ifdef DMC_DI_MINHOOK
    // The exe imports DINPUT8.dll statically, so it is already mapped. Install the
    // DirectInput8Create detour now, before any game code can call it.
    dihide::Attach();
    dimh::InstallEarly();
#endif
}

const Config& ShimConfig() {
    EnterCriticalSection(&g_lock);
    if (!g_cfgLoaded) { g_cfgLoaded = true; LoadConfigLocked(); }
    LeaveCriticalSection(&g_lock);
    return g_cfg;
}

bool ShimInSdlCall() { return InSdlCall(); }
}

// ---------------------------------------------------------------------------
// Exports. Names (and, for xinput1_3, ordinals) come from the generated .def;
// the 32-bit DmC exe imports XInputGetState/XInputSetState by ordinal (2 and 3).
// ---------------------------------------------------------------------------
extern "C" {

DWORD WINAPI XInputGetState(DWORD user, XInputState* state) {
    if (!state || user >= (DWORD)kMaxUsers) return XERR_BAD_ARGUMENTS;
    if (!EnsureInit() || InSdlCall() || !UseSdl()) return real::GetState(user, state);

    if (!SdlFirst()) {
        DWORD r;
        { SlowScope s("real XInputGetState", user); r = real::GetState(user, state); }
        InterlockedExchange(&g_realOk[user], r == XERR_SUCCESS);
        NoteReal(user, r);
        if (r == XERR_SUCCESS) return r;
    }
    bool gotSdl;
    { SlowScope s("SDL read (lock wait + pump)", user); gotSdl = ReadSdl(user, state); }
    if (gotSdl) return XERR_SUCCESS;
    if (g_cfg.mode == Mode::Sdl) return XERR_DEVICE_NOT_CONNECTED;
    if (SdlFirst()) {
        DWORD r = real::GetState(user, state);
        InterlockedExchange(&g_realOk[user], r == XERR_SUCCESS);
        NoteReal(user, r);
        return r;
    }
    return XERR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputSetState(DWORD user, XInputVibration* vib) {
    if (!vib || user >= (DWORD)kMaxUsers) return XERR_BAD_ARGUMENTS;
    if (!EnsureInit() || InSdlCall() || !UseSdl()) return real::SetState(user, vib);

    bool trySdl = SdlFirst() || !InterlockedCompareExchange(&g_realOk[user], 0, 0);
    if (trySdl) {
        SdlGuard guard;
        uint16_t lo = g_enabled ? ScaleRumbleMotor(vib->wLeftMotorSpeed, g_cfg.mapping.rumble_scale_pct) : 0;
        uint16_t hi = g_enabled ? ScaleRumbleMotor(vib->wRightMotorSpeed, g_cfg.mapping.rumble_scale_pct) : 0;
        if (sdlb::Rumble((int)user, lo, hi)) return XERR_SUCCESS;
    }
    if (g_cfg.mode == Mode::Sdl) return XERR_DEVICE_NOT_CONNECTED;
    return real::SetState(user, vib);
}

DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, XInputCapabilities* caps) {
    if (!caps || user >= (DWORD)kMaxUsers || (flags & ~1u)) return XERR_BAD_ARGUMENTS;
    if (!EnsureInit() || InSdlCall() || PickSource(user) == Src::Real)
        return real::GetCapabilities(user, flags, caps);
    if (!SdlActive(user)) return XERR_DEVICE_NOT_CONNECTED;

    *caps = XInputCapabilities();
    caps->Type = 0x01;     // XINPUT_DEVTYPE_GAMEPAD
    caps->SubType = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
    caps->Flags = 0x0001;  // XINPUT_CAPS_FFB_SUPPORTED
    caps->Gamepad.wButtons = 0xF3FF;
    caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 0xFF;
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY =
    caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = (int16_t)0xFFC0;
    caps->Vibration.wLeftMotorSpeed = caps->Vibration.wRightMotorSpeed = 0xFFFF;
    return XERR_SUCCESS;
}

void WINAPI XInputEnable(BOOL enable) {
    EnsureInit();          // make sure the real DLL is loaded before forwarding
    real::Enable(enable);
    if (!UseSdl()) return;
    SdlGuard guard;
    g_enabled = enable != FALSE;
    if (!g_enabled) sdlb::StopAllRumble();
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD user, void* render, void* capture) {
    EnsureInit();
    return real::GetDSoundAudioDeviceGuids(user, render, capture);
}

DWORD WINAPI XInputGetBatteryInformation(DWORD user, BYTE devType, XInputBatteryInformation* info) {
    if (!info || user >= (DWORD)kMaxUsers) return XERR_BAD_ARGUMENTS;
    if (!EnsureInit() || InSdlCall() || PickSource(user) == Src::Real)
        return real::GetBatteryInformation(user, devType, info);
    if (!SdlActive(user)) return XERR_DEVICE_NOT_CONNECTED;
    if (devType == 0) { info->BatteryType = 1; info->BatteryLevel = 3; }  // wired, full
    else { info->BatteryType = 0; info->BatteryLevel = 0; }               // no headset
    return XERR_SUCCESS;
}

DWORD WINAPI XInputGetKeystroke(DWORD user, DWORD reserved, void* keystroke) {
    if (!keystroke) return XERR_BAD_ARGUMENTS;
    if (!EnsureInit() || InSdlCall() || user >= (DWORD)kMaxUsers || PickSource(user) == Src::Real)
        return real::GetKeystroke(user, reserved, keystroke);
    return XERR_EMPTY;  // SDL path produces no keystroke events
}

}  // extern "C"