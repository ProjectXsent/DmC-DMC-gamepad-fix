#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL.h>
#include "sdl_backend.h"
#include "log.h"

namespace dmc { namespace sdlb {

namespace {

struct Slot {
    SDL_Gamepad*   pad = nullptr;
    SDL_JoystickID id = 0;
    uint16_t       low = 0, high = 0;   // last rumble request
    ULONGLONG      rumble_tick = 0;
};

// SDL rumble effects have a finite duration; XInput's do not. We request a long
// effect and refresh it before it expires while the game keeps it non-zero.
constexpr Uint32    kRumbleDurationMs = 60000;
constexpr ULONGLONG kRumbleRefreshMs  = 30000;
constexpr double    kPumpIntervalMs   = 1.0;

Slot            g_slots[kMaxUsers];
Config          g_cfg;
RealConnectedFn g_probe = nullptr;
bool            g_ready = false;
LARGE_INTEGER   g_freq, g_lastPump;

int FindSlotById(SDL_JoystickID id) {
    for (int i = 0; i < kMaxUsers; ++i)
        if (g_slots[i].pad && g_slots[i].id == id) return i;
    return -1;
}

void AddPad(SDL_JoystickID id) {
    if (FindSlotById(id) >= 0) return;
    if (!SDL_IsGamepad(id)) return;

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

    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    if (!pad) { log::Write("SDL_OpenGamepad failed: %s", SDL_GetError()); return; }
    g_slots[slot] = Slot();
    g_slots[slot].pad = pad;
    g_slots[slot].id = id;
    log::Write("pad '%s' -> user %d", SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "?", slot);
}

void RemovePad(SDL_JoystickID id) {
    int s = FindSlotById(id);
    if (s < 0) return;
    log::Write("pad removed from user %d", s);
    SDL_CloseGamepad(g_slots[s].pad);
    g_slots[s] = Slot();
}

void ScanExisting() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) AddPad(ids[i]);
    SDL_free(ids);
}

}  // namespace

bool Init(const Config& cfg, const std::string& mappingsPathUtf8, RealConnectedFn probe) {
    g_cfg = cfg;
    g_probe = probe;

    // Defaults first, so the [Hints] section can override them.
    // SDL must never call XInput itself: that could load *this* proxy again and
    // would double-list Xbox pads.
    SDL_SetHint("SDL_XINPUT_ENABLED", "0");
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    SDL_SetHint("SDL_JOYSTICK_WGI", "0");
    for (const auto& h : cfg.hints) SDL_SetHint(h.first.c_str(), h.second.c_str());

    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        log::Write("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!mappingsPathUtf8.empty()) {
        int n = SDL_AddGamepadMappingsFromFile(mappingsPathUtf8.c_str());
        if (n >= 0) log::Write("loaded %d mappings from %s", n, mappingsPathUtf8.c_str());
        else log::Write("no extra mappings loaded (%s)", SDL_GetError());
    }

    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_lastPump);
    g_ready = true;
    ScanExisting();
    log::Write("SDL backend ready");
    return true;
}

void Pump() {
    if (!g_ready) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double ms = (double)(now.QuadPart - g_lastPump.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
    if (ms < kPumpIntervalMs) return;
    g_lastPump = now;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED) AddPad(ev.gdevice.which);
        else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) RemovePad(ev.gdevice.which);
    }

    ULONGLONG t = GetTickCount64();
    for (auto& s : g_slots) {
        if (s.pad && (s.low || s.high) && t - s.rumble_tick > kRumbleRefreshMs) {
            SDL_RumbleGamepad(s.pad, s.low, s.high, kRumbleDurationMs);
            s.rumble_tick = t;
        }
    }
}

bool SlotActive(int user) {
    return g_ready && user >= 0 && user < kMaxUsers && g_slots[user].pad &&
           SDL_GamepadConnected(g_slots[user].pad);
}

bool ReadSlot(int user, PadSnapshot& out) {
    if (!SlotActive(user)) return false;
    SDL_Gamepad* p = g_slots[user].pad;

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
    return true;
}

bool Rumble(int user, uint16_t low, uint16_t high) {
    if (!SlotActive(user)) return false;
    Slot& s = g_slots[user];
    s.low = low; s.high = high; s.rumble_tick = GetTickCount64();
    if (low == 0 && high == 0) SDL_RumbleGamepad(s.pad, 0, 0, 0);
    else SDL_RumbleGamepad(s.pad, low, high, kRumbleDurationMs);
    return true;
}

void StopAllRumble() {
    for (auto& s : g_slots) {
        if (s.pad) SDL_RumbleGamepad(s.pad, 0, 0, 0);
        s.low = s.high = 0;
    }
}

}}  // namespace dmc::sdlb
