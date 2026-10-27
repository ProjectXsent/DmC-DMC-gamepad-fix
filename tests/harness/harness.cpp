// 32-bit smoke-test harness. Loads ./xinput1_3.dll by full path and calls
// XInputGetState / XInputSetState BY ORDINAL (2 and 3) -- exactly how the game
// imports them -- then prints pad state.  Usage:  xinput_harness [--rumble] [--seconds N]
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma pack(push, 4)
struct Gamepad { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct State   { DWORD packet; Gamepad pad; };
struct Vib     { WORD left, right; };
#pragma pack(pop)

typedef DWORD (WINAPI *GetStateFn)(DWORD, State*);
typedef DWORD (WINAPI *SetStateFn)(DWORD, Vib*);

int main(int argc, char** argv) {
    bool rumble = false; int seconds = 30;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--rumble")) rumble = true;
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
    }

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = 0;
    lstrcatW(path, L"xinput1_3.dll");

    HMODULE mod = LoadLibraryW(path);
    if (!mod) { printf("LoadLibrary failed (%lu): %ls\n", GetLastError(), path); return 1; }
    GetStateFn getState = (GetStateFn)GetProcAddress(mod, MAKEINTRESOURCEA(2));
    SetStateFn setState = (SetStateFn)GetProcAddress(mod, MAKEINTRESOURCEA(3));
    if (!getState || !setState) { printf("ordinal 2/3 not exported!\n"); return 2; }
    printf("Loaded %ls; polling for %d s. Press buttons... (Ctrl+C to quit)\n", path, seconds);

    DWORD end = GetTickCount() + (DWORD)seconds * 1000;
    DWORD lastPacket[4] = {};
    bool connected[4] = {};
    while ((long)(end - GetTickCount()) > 0) {
        for (DWORD u = 0; u < 4; ++u) {
            State s = {};
            DWORD r = getState(u, &s);
            bool now = (r == 0);
            if (now != connected[u]) { printf("user %lu %s\n", u, now ? "CONNECTED" : "disconnected"); connected[u] = now; }
            if (now && s.packet != lastPacket[u]) {
                lastPacket[u] = s.packet;
                printf("user %lu pkt=%lu btn=%04X LT=%3u RT=%3u L=(%6d,%6d) R=(%6d,%6d)\n", u, s.packet,
                       s.pad.buttons, s.pad.lt, s.pad.rt, s.pad.lx, s.pad.ly, s.pad.rx, s.pad.ry);
                if (rumble) { Vib v = { (WORD)(s.pad.lt * 257), (WORD)(s.pad.rt * 257) }; setState(u, &v); }
            }
        }
        Sleep(8);
    }
    for (DWORD u = 0; u < 4; ++u) { Vib v = {0, 0}; setState(u, &v); }
    return 0;
}
