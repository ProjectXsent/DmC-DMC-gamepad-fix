#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL.h>
#include "sdl_backend.h"
#include "log.h"

// Threading model
// ---------------
// SDL is initialised, pumped and shut down on ONE background worker thread. The game's
// thread never calls into SDL: it only copies a cached PadSnapshot (ReadSlot) or posts a
// rumble request (Rumble) under a tiny lock. Previously SDL_Init ran inside the game's
// first XInput call (~90 ms, window/device creation = lost focus + busy cursor) and
// SDL_PumpEvents ran on the game's thread every frame (multi-ms stalls).

namespace dmc { namespace sdlb {

namespace {

struct Slot {
    SDL_Gamepad*   pad = nullptr;
    SDL_JoystickID id = 0;
    bool           connected = false;   // cached SDL_GamepadConnected()
    PadSnapshot    snap;                // cached state, refreshed by the worker
    uint16_t       low = 0, high = 0;   // last rumble request from the game
    bool           rumble_dirty = false;
    ULONGLONG      rumble_tick = 0;
};

// SDL rumble effects have a finite duration; XInput's do not. We request a long
// effect and refresh it before it expires while the game keeps it non-zero.
constexpr Uint32    kRumbleDurationMs = 60000;
constexpr ULONGLONG kRumbleRefreshMs  = 30000;
constexpr Uint32    kPollDelayMs      = 2;

CRITICAL_SECTION g_slotLock;   // guards every Slot field except `pad`/`id` mutation order (see below)
Slot             g_slots[kMaxUsers];
Config           g_cfg;
RealConnectedFn  g_probe = nullptr;
std::string      g_mappingsPath;
volatile LONG    g_started = 0;
volatile LONG    g_ready = 0;
volatile LONG    g_workerTid = 0;

struct Lock {
    Lock()  { EnterCriticalSection(&g_slotLock); }
    ~Lock() { LeaveCriticalSection(&g_slotLock); }
};

// Only the worker thread ever changes pad/id, so the worker may read them without the lock.
int FindSlotById(SDL_JoystickID id) {
    for (int i = 0; i < kMaxUsers; ++i)
        if (g_slots[i].pad && g_slots[i].id == id) return i;
    return -1;
}

void AddPad(SDL_JoystickID id) {
    if (FindSlotById(id) >= 0) return;
    if (!SDL_IsGamepad(id)) return;

    // Log every gamepad SDL sees, including ones we skip, to diagnose "pad X does nothing".
    {
        const char* nm = SDL_GetGamepadNameForID(id);
        log::Write("SDL gamepad id=%u '%s' vid=%04X pid=%04X type=%d", (unsigned)id, nm ? nm : "?",
                   (unsigned)SDL_GetGamepadVendorForID(id), (unsigned)SDL_GetGamepadProductForID(id),
                   (int)SDL_GetGamepadTypeForID(id));
    }

    if (g_cfg.ignore_xbox_pads) {
        SDL_GamepadType t = SDL_GetGamepadTypeForID(id);
        if (t == SDL_GAMEPAD_TYPE_XBOX360 || t == SDL_GAMEPAD_TYPE_XBOXONE) {
            log::Write("ignoring Xbox-type pad id=%u (served by real XInput)", (unsigned)id);
            return;
        }
    }

    // Prefer a free slot that no genuine XInput pad is using; else any free slot.
    int slot = -1;
    for (int i = 0; i < kMaxUsers && slot < 0; ++i)
        if (!g_slots[i].pad && !(g_probe && g_probe(i))) slot = i;
    for (int i = 0; i < kMaxUsers && slot < 0; ++i)
        if (!g_slots[i].pad) slot = i;
    if (slot < 0) { log::Write("no free slot for pad id=%u", (unsigned)id); return; }

    double t0 = log::NowMs();
    SDL_Gamepad* pad = SDL_OpenGamepad(id);   // outside the lock: may take a while
    log::Write("SDL_OpenGamepad took %.1f ms", log::NowMs() - t0);
    if (!pad) { log::Write("SDL_OpenGamepad failed: %s", SDL_GetError()); return; }
    {
        Lock l;
        g_slots[slot] = Slot();
        g_slots[slot].pad = pad;
        g_slots[slot].id = id;
        g_slots[slot].connected = true;
    }
    log::Write("pad '%s' -> user %d", SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "?", slot);
}

void RemovePad(SDL_JoystickID id) {
    int s = FindSlotById(id);
    if (s < 0) return;
    log::Write("pad removed from user %d", s);
    SDL_Gamepad* pad;
    {
        Lock l;               // unpublish first so the game thread can no longer reach it
        pad = g_slots[s].pad;
        g_slots[s] = Slot();
    }
    SDL_CloseGamepad(pad);
}

void ScanExisting() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) AddPad(ids[i]);
    SDL_free(ids);
}

bool ReadFromSdl(SDL_Gamepad* p, PadSnapshot& out) {
    static const struct { SDL_GamepadButton sb; uint32_t pb; } map[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, PB_SOUTH}, {SDL_GAMEPAD_BUTTON_EAST, PB_EAST},
        {SDL_GAMEPAD_BUTTON_WEST, PB_WEST},   {SDL_GAMEPAD_BUTTON_NORTH, PB_NORTH},
        {SDL_GAMEPAD_BUTTON_BACK, PB_BACK},   {SDL_GAMEPAD_BUTTON_START, PB_START},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, PB_LSTICK}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PB_RSTICK},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PB_LSHOULDER}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PB_RSHOULDER},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, PB_DUP},     {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PB_DDOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PB_DLEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PB_DRIGHT},
    };
    out = PadSnapshot();
    for (const auto& e : map)
        if (SDL_GetGamepadButton(p, e.sb)) out.buttons |= e.pb;
    out.lx = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX);
    out.ly = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY);
    out.rx = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTX);
    out.ry = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTY);
    out.lt = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    out.rt = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    return SDL_GamepadConnected(p);
}

// One iteration of the worker: pump SDL, refresh cached state, push rumble.
void Tick() {
    const bool timing = log::Enabled();
    const double p0 = timing ? log::NowMs() : 0.0;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED) AddPad(ev.gdevice.which);
        else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) RemovePad(ev.gdevice.which);
    }
    if (timing) {
        static LONG slowPumps = 0;
        double dt = log::NowMs() - p0;
        if (dt >= 8.0 && InterlockedIncrement(&slowPumps) <= 20)
            log::Write("slow SDL pump: %.1f ms on the SDL worker thread (harmless to the game)", dt);
    }

    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < kMaxUsers; ++i) {
        SDL_Gamepad* pad = g_slots[i].pad;      // only this thread changes it
        if (!pad) continue;

        PadSnapshot snap;
        bool connected = ReadFromSdl(pad, snap);

        uint16_t lo, hi; bool push;
        {
            Lock l;
            g_slots[i].snap = snap;
            g_slots[i].connected = connected;
            lo = g_slots[i].low; hi = g_slots[i].high;
            push = g_slots[i].rumble_dirty ||
                   ((lo || hi) && now - g_slots[i].rumble_tick > kRumbleRefreshMs);
            if (push) { g_slots[i].rumble_dirty = false; g_slots[i].rumble_tick = now; }
        }
        if (push) {
            if (lo == 0 && hi == 0) SDL_RumbleGamepad(pad, 0, 0, 0);
            else SDL_RumbleGamepad(pad, lo, hi, kRumbleDurationMs);
        }
    }
}

DWORD WINAPI Worker(LPVOID) {
    InterlockedExchange(&g_workerTid, (LONG)GetCurrentThreadId());   // before any SDL call: see WorkerThreadId()

    // Defaults first, so the [Hints] section can override them.
    // SDL must never call XInput itself: that could load *this* proxy again and
    // would double-list Xbox pads.
    SDL_SetHint("SDL_XINPUT_ENABLED", "0");
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    SDL_SetHint("SDL_JOYSTICK_WGI", "0");
    // Xbox pads belong to the real XInput driver. With XInput disabled above, SDL would
    // otherwise try to drive them itself through its HIDAPI Xbox drivers.
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_XBOX", "0");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_XBOX_360", "0");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_XBOX_ONE", "0");
    for (const auto& h : g_cfg.hints) SDL_SetHint(h.first.c_str(), h.second.c_str());

    double t0 = log::NowMs();
    bool initOk = SDL_Init(SDL_INIT_GAMEPAD);
    log::Write("SDL_Init(gamepad) took %.1f ms on worker thread %lu", log::NowMs() - t0, GetCurrentThreadId());
    if (!initOk) {
        log::Write("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (!g_mappingsPath.empty()) {
        t0 = log::NowMs();
        int n = SDL_AddGamepadMappingsFromFile(g_mappingsPath.c_str());
        log::Write("loading mappings took %.1f ms", log::NowMs() - t0);
        if (n >= 0) log::Write("loaded %d mappings from %s", n, g_mappingsPath.c_str());
        else log::Write("no extra mappings loaded (%s)", SDL_GetError());
    }

    t0 = log::NowMs();
    ScanExisting();
    log::Write("initial device scan took %.1f ms", log::NowMs() - t0);
    InterlockedExchange(&g_ready, 1);
    log::Write("SDL backend ready");

    for (;;) {
        Tick();
        SDL_Delay(kPollDelayMs);
    }
}

}  // namespace

// Id of the SDL worker thread (0 until it starts). The DirectInput filter treats
// every call made on this thread as SDL's own, so SDL's enumeration is never hidden.
// (Move this declaration into sdl_backend.h if you prefer.)
unsigned long WorkerThreadId() { return (unsigned long)InterlockedCompareExchange(&g_workerTid, 0, 0); }

// Non-blocking: starts the worker and returns. Until the worker reports ready,
// SlotActive()/ReadSlot() say "no pad", so the shim just answers from real XInput.
bool Init(const Config& cfg, const std::string& mappingsPathUtf8, RealConnectedFn probe) {
    if (InterlockedExchange(&g_started, 1)) return true;
    InitializeCriticalSection(&g_slotLock);
    g_cfg = cfg;
    g_probe = probe;
    g_mappingsPath = mappingsPathUtf8;

    HANDLE h = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!h) {
        log::Write("could not start the SDL worker thread (error %lu)", GetLastError());
        return false;
    }
    CloseHandle(h);
    log::Write("SDL worker thread started; game thread no longer blocks on SDL");
    return true;
}

// Kept for API compatibility: the worker thread pumps SDL now.
void Pump() {}

bool SlotActive(int user) {
    if (!InterlockedCompareExchange(&g_ready, 0, 0) || user < 0 || user >= kMaxUsers) return false;
    Lock l;
    return g_slots[user].pad && g_slots[user].connected;
}

bool ReadSlot(int user, PadSnapshot& out) {
    if (!InterlockedCompareExchange(&g_ready, 0, 0) || user < 0 || user >= kMaxUsers) return false;
    Lock l;
    if (!g_slots[user].pad || !g_slots[user].connected) return false;
    out = g_slots[user].snap;
    return true;
}

bool Rumble(int user, uint16_t low, uint16_t high) {
    if (!InterlockedCompareExchange(&g_ready, 0, 0) || user < 0 || user >= kMaxUsers) return false;
    Lock l;
    Slot& s = g_slots[user];
    if (!s.pad || !s.connected) return false;
    s.low = low; s.high = high; s.rumble_dirty = true;   // applied by the worker within ~2 ms
    return true;
}

void StopAllRumble() {
    if (!InterlockedCompareExchange(&g_ready, 0, 0)) return;
    Lock l;
    for (auto& s : g_slots) {
        if (!s.pad) continue;
        s.low = s.high = 0;
        s.rumble_dirty = true;
    }
}

}}  // namespace dmc::sdlb