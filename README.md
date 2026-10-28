# dmc-sdl-controller

SDL3-based controller support for **DmC: Devil May Cry** (2013, PC). A drop-in
`xinput1_3.dll` proxy that lets the game see PlayStation, Switch Pro and generic
controllers as if they were Xbox pads — with rumble and hotplug — without patching
the game executable.

## How it works

The game (`DMC-DevilMayCry.exe`, 32-bit) imports exactly two XInput functions from
`XINPUT1_3.dll`, **by ordinal** (2 = `XInputGetState`, 3 = `XInputSetState`).
Windows searches the application folder before System32, so a DLL named
`xinput1_3.dll` placed next to the exe is loaded instead. It:

1. Forwards to the real system `xinput1_3.dll` first, so genuine Xbox pads behave
   exactly as before (**hybrid mode**).
2. Where no real pad is present, answers from an SDL3 gamepad, translating its
   state to `XINPUT_STATE` and its rumble requests to `SDL_RumbleGamepad`.

The game *also* reads gamepads through DirectInput (`DirectInput8Create` from
`DINPUT8.dll`), which makes a physical pad show up twice and causes button prompts to
flip between generic "Button N" labels and Xbox glyphs. The XInput proxy therefore also
detours `DirectInput8Create` **in-process with [MinHook](https://github.com/TsudaKageyu/minhook)**
and hides gamepad-class devices from the game's DirectInput (keyboard and mouse are
untouched), so only the XInput path remains. No separate `dinput8.dll` is involved.

Details: [docs/DESIGN.md](docs/DESIGN.md).

## Install

1. Download the release zip (or build it; see below).
2. Run `install.ps1` (auto-detects the Steam install), or copy `xinput1_3.dll`,
   `dmc_sdl.ini` and `gamecontrollerdb.txt` next to `DMC-DevilMayCry.exe` yourself.
   That is all; there is nothing else to install.
3. Launch the game through Steam as usual. Uninstall: `install.ps1 -Uninstall`.

Configuration: [docs/CONFIG.md](docs/CONFIG.md). Problems: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Build

Requires CMake ≥ 3.21 and Visual Studio 2022 (C++ workload). **The proxy's bitness must match the game's.**
DmC: Devil May Cry is 32-bit, so use the `win32` preset for it.

```powershell
cmake --preset win32
cmake --build --preset win32-release
ctest --preset win32-release
python tools/inspect_imports.py --exports build/win32/Release/xinput1_3.dll --expect-ordinals 2,3
```

### 64-bit games

The same code builds for x64; only the bitness and the proxy's file name change.

```powershell
cmake --preset win64            # builds xinput1_4.dll (x64)
cmake --build --preset win64-release
```

1. Run `python tools/inspect_imports.py Game.exe`. It reports the bitness and which XInput
   DLL the game imports: 64-bit titles usually import `xinput1_4.dll`, but some use
   `xinput1_3.dll` or `xinput9_1_0.dll`.
2. Build with that name: `cmake --preset win64 -DDMC_XINPUT_DLL=xinput1_3` (or `xinput9_1_0`).
3. Install with `install.ps1 -GameDir <folder> -ExeName Game.exe [-XInputDll xinput1_3.dll]`.
   It detects 32/64-bit from the exe and picks the matching build.

Names are always exported. Ordinals (2–8) are only exported for `xinput1_3` by default;
if a game imports another XInput DLL **by ordinal** (the inspector shows this), build with
`-DDMC_EXPORT_ORDINALS=ON` after confirming the numbering with
`inspect_imports.py --exports C:\Windows\System32\xinput1_4.dll`.

This only works for games that load XInput from their own folder via the normal DLL
search. See [docs/DESIGN.md](docs/DESIGN.md#other-games-and-64-bit) for what it can't cover.
The DirectInput part is specific to what *this* game does; for other games, set
`hide_dinput_pads = false` unless they show the same duplicate-pad symptom.

SDL3 and MinHook are fetched and linked statically (pinned in `cmake/FetchSDL.cmake` / `cmake/FetchMinHook.cmake`).
Unit tests alone build anywhere: `cmake --preset tests-only && cmake --build --preset tests-only && ctest --preset tests-only`.

## Verification checklist

Things the scaffold could not confirm without a Windows machine:

- [ ] `tools/inspect_imports.py --exports C:\Windows\SysWOW64\xinput1_3.dll` shows
      ordinals 2/3 = `XInputGetState`/`XInputSetState` (the `.def` assumes the usual layout).
- [ ] The project configures and builds for both `win32` and `win64` (SDL3 tag in `cmake/FetchSDL.cmake`, trimmed SDL options).
- [ ] `xinput_harness.exe` prints pad state for a DualSense / Switch Pro / generic pad.
- [ ] In-game: menus navigate, movement/camera/triggers feel right, rumble works.
- [ ] With `log = true`, `dmc_sdl.log` shows `DirectInput hook (MinHook): installed`, then your pad as `-> hidden`, and prompts no longer flip.
- [ ] MinHook release tag in `cmake/FetchMinHook.cmake` (`v1.3.3`) fetches and compiles (it can't be verified without network).
- [ ] Hotplug: unplug/replug mid-game; Steam overlay on and off.

## Limitations

- On-screen button prompts stay Xbox-style (they are baked into the game's UI).
- Single-player only; XInput users 0–3 are exposed.
- Motion, touchpad, LEDs and adaptive triggers are not translated.
- Only gamepad-class DirectInput devices (joystick, gamepad, driving, flight, first-person) are hidden.
- Steam Input users: Steam's virtual Xbox pad already works through real XInput and is left alone.

## Layout

```
src/        shim sources (mapping/config are platform-independent)
tests/      unit tests, Python tool tests, 32-bit ordinal harness
tools/      inspect_imports.py, install.ps1
config/     default dmc_sdl.ini
docs/       DESIGN, CONFIG, TROUBLESHOOTING
```

No game files or assets are included or required in this repository. zlib licensed; third-party notices in [THIRD_PARTY.md](THIRD_PARTY.md).
