# dmc-sdl-controller

SDL3-based controller support for **DmC: Devil May Cry** (2013, PC). A drop-in
`xinput1_3.dll` proxy that lets the game see PlayStation, Switch Pro and generic
controllers as if they were Xbox pads — with rumble and hotplug — without patching
the game executable.

> **Status: initial scaffold.** The portable logic (button/axis mapping, config
> parsing, PE inspection tool) is unit-tested. The Windows DLL has **not yet been
> built or run against the game**; see [Verification checklist](#verification-checklist).

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
flip between generic "Button N" labels and Xbox glyphs. A second proxy, `dinput8.dll`,
hides gamepad-class devices from the game's DirectInput (keyboard and mouse are
untouched) so only the XInput path remains.

Details: [docs/DESIGN.md](docs/DESIGN.md).

## Install

1. Download the release zip (or build it; see below).
2. Run `install.ps1` (auto-detects the Steam install), or copy `xinput1_3.dll`, `dinput8.dll`,
   `dmc_sdl.ini` and `gamecontrollerdb.txt` next to `DMC-DevilMayCry.exe` yourself.
   **Install both DLLs**; without `dinput8.dll` the prompts can flip between generic and Xbox.
3. Launch the game through Steam as usual. Uninstall: `install.ps1 -Uninstall`.

Configuration: [docs/CONFIG.md](docs/CONFIG.md). Problems: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Build

Requires CMake ≥ 3.21 and Visual Studio 2022 (C++ workload). **The DLL must be 32-bit.**

```powershell
cmake --preset win32
cmake --build --preset win32-release
ctest --preset win32-release
python tools/inspect_imports.py --exports build/win32/Release/xinput1_3.dll --expect-ordinals 2,3
python tools/inspect_imports.py --exports build/win32/Release/dinput8.dll --expect-names DirectInput8Create
```

SDL3 is fetched and linked statically (pinned in `cmake/FetchSDL.cmake`).
Unit tests alone build anywhere: `cmake --preset tests-only && cmake --build --preset tests-only && ctest --preset tests-only`.

## Verification checklist

Things the scaffold could not confirm without a Windows machine:

- [ ] `tools/inspect_imports.py --exports C:\Windows\SysWOW64\xinput1_3.dll` shows
      ordinals 2/3 = `XInputGetState`/`XInputSetState` (the `.def` assumes the usual layout).
- [ ] The project configures and builds (SDL3 tag in `cmake/FetchSDL.cmake`, trimmed SDL options).
- [ ] `xinput_harness.exe` prints pad state for a DualSense / Switch Pro / generic pad.
- [ ] In-game: menus navigate, movement/camera/triggers feel right, rumble works.
- [ ] With `log = true`, `dmc_sdl_dinput.log` lists your pad as `-> hidden` and prompts no longer flip.
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

No game files or assets are included or required in this repository. zlib licensed.
