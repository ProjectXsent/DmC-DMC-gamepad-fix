# Design

## Findings that drive the design

From `tools/inspect_imports.py DMC-DevilMayCry.exe` (also verified from a `dumpbin /imports` dump):

| Fact | Consequence |
|---|---|
| PE32, machine `i386` | Shim must be a 32-bit DLL (`-A Win32`). |
| `XINPUT1_3.dll`: ordinals **2, 3** only | Export by ordinal; only GetState/SetState really matter. |
| `DINPUT8.dll`: `DirectInput8Create` (by name) | The engine also reads gamepads via DirectInput. **An early version of this design assumed this was keyboard/mouse only; the "prompts flip between Button N and Xbox glyphs" symptom showed otherwise.** Needs its own proxy (below). |
| `WINMM.dll`: `timeGetTime`, `timeBeginPeriod` | No joystick API use. |
| `steam_api.dll` (`SteamAPI_RestartAppIfNecessary`) | Launch via Steam; a side-by-side DLL is fine. |
| `EasyHook32.dll` bundled | The game hooks things itself — we avoid inline hooking and use plain DLL substitution. |

## Why a proxy DLL

`xinput1_3.dll` is not a KnownDLL, so the loader checks the application directory
first. Dropping our DLL there replaces the import with zero changes to the exe, no
memory patching, and trivial uninstall. It stays passive (no hooks), which is also
the least likely to upset Steam overlay or anti-tamper checks.

## Export table

`src/exports.def` exports both names and ordinals, matching the real DLL:

| Ordinal | Function | Behaviour |
|---|---|---|
| 2 | XInputGetState | routed (see below) |
| 3 | XInputSetState | routed |
| 4 | XInputGetCapabilities | routed; synthetic "gamepad" caps for SDL pads |
| 5 | XInputEnable | forwards; zeroes SDL state/rumble when disabled |
| 6 | XInputGetDSoundAudioDeviceGuids | forwards |
| 7 | XInputGetBatteryInformation | routed; SDL pads report wired/full |
| 8 | XInputGetKeystroke | forwards; SDL pads report "empty" |

The ordinal numbers come from memory of Microsoft's DLL; CI checks that 2 and 3 are
exported and the README checklist asks you to confirm against `SysWOW64\xinput1_3.dll`.
Undocumented ordinals 100–103 (GetStateEx etc.) are not exported; the game doesn't use them.

## Routing policy (`src/xinput_shim.cpp`)

- `passthrough`: always the real DLL.
- `sdl`: SDL pads only.
- `hybrid` (default): real XInput first; if that slot has no real pad, the SDL pad
  assigned to the slot answers. `prefer_sdl=true` flips the priority.

SDL pads are assigned to the first free slot **not currently occupied by a real
XInput pad** at the moment they connect. Xbox-type SDL pads are ignored
(`ignore_xbox_pads`) because real XInput already serves them; without this, an Xbox
pad would appear twice.

## DirectInput proxy (`src/dinput_proxy.cpp`)

Symptom it fixes: a physical pad is visible to the game twice, once as an XInput user
(real or via this shim) and once as a DirectInput gamepad. Input arriving from each
source switches the prompt style (Xbox glyphs vs generic "Button N"), and actions can
double-fire.

`dinput8.dll` forwards `DirectInput8Create` to the real DLL (by full system path), then
patches two vtable slots of the returned interface in place: `EnumDevices` (slot 4) and
`EnumDevicesBySemantics` (slot 9). A thunk callback drops devices whose `dwDevType`
low byte is joystick/gamepad/driving/flight/first-person (`src/di_filter.h`, unit-tested).
Keyboard, mouse and everything else are passed through. Both `IDirectInput8A` and
`IDirectInput8W` are handled.

- No full COM wrapper is needed, and the game never learns the GUIDs of hidden pads.
- Skipped `EnumDevicesBySemantics` devices are intentionally not `Release`d (ownership
  of that reference is unverified; a leak beats a double release).
- Cross-DLL guard: `xinput1_3.dll` exports `DmcInSdlCall`. If SDL ever enumerates
  DirectInput through this proxy (SDL3 normally uses `CoCreateInstance`, bypassing it),
  the filter is skipped for that thread so SDL still sees the pads.
- Disabled by `hide_dinput_pads = false` or `mode = passthrough`.
- Log: `dmc_sdl_dinput.log` (needs `log = true`) lists each enumerated device type and
  whether it was hidden, which is also the way to confirm this diagnosis on your machine.

## Mapping (`src/mapping.cpp`, unit-tested)

- Buttons are **positional** (SDL3 `SOUTH/EAST/WEST/NORTH` → A/B/X/Y).
- SDL stick Y is positive-down; XInput's is positive-up → Y is negated (with `-32768` clamped).
- Triggers 0..32767 → 0..255 (rounded). Optional rescaled threshold.
- Optional radial, rescaled deadzones. Default 0 because the game applies its own.
- `dwPacketNumber` increments whenever the mapped state changes.

## Hazards handled

| Hazard | Mitigation |
|---|---|
| Loader lock | `DllMain` only stores the handle and inits a critical section. SDL, `LoadLibrary` and file I/O run lazily on the first XInput call. |
| Loading ourselves | The real DLL is loaded by full path from the system directory. |
| SDL calling XInput (→ our proxy) | `SDL_XINPUT_ENABLED=0`, plus a per-thread re-entrancy guard that sends nested calls straight to the real DLL. |
| Thread safety | One critical section guards init and all backend state. |
| Polling cost | SDL is pumped at most once per ms regardless of how often the game polls. |
| Rumble duration | XInput rumble is open-ended; SDL's is timed. We request 60 s and refresh at 30 s while non-zero. |
| Locale | INI numbers are parsed by hand; the game may call `setlocale`. |

## Known gaps

- Rumble is not explicitly stopped at process exit (nothing safe to run during
  loader-lock teardown); drivers stop it when the device handle closes.
- The vtable layout (slots 4 and 9) and the DirectInput IIDs are from memory of the DirectInput 8 headers.
- Not yet built or exercised against the real game — see the README checklist.
