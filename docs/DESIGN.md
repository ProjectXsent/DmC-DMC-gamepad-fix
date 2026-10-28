# Design

## Findings that drive the design

From `tools/inspect_imports.py DMC-DevilMayCry.exe` (also verified from a `dumpbin /imports` dump):

| Fact | Consequence |
|---|---|
| PE32, machine `i386` | Shim must be a 32-bit DLL (`-A Win32`). |
| `XINPUT1_3.dll`: ordinals **2, 3** only | Export by ordinal; only GetState/SetState really matter. |
| `DINPUT8.dll`: `DirectInput8Create` (by name, static import) | The engine also reads gamepads via DirectInput. **An early version of this design assumed this was keyboard/mouse only; the "prompts flip between Button N and Xbox glyphs" symptom showed otherwise.** Needs the DirectInput hiding described below. |
| `WINMM.dll`: `timeGetTime`, `timeBeginPeriod` | No joystick API use. |
| `steam_api.dll` (`SteamAPI_RestartAppIfNecessary`) | Launch via Steam; a side-by-side DLL is fine. |
| `EasyHook32.dll` bundled | The game hooks things itself, which is a collision risk for our one inline hook (`DirectInput8Create`); the XInput side itself stays plain DLL substitution. |

## Why a proxy DLL

`xinput1_3.dll` is not a KnownDLL, so the loader checks the application directory
first. Dropping our DLL there replaces the import with zero changes to the exe, no
memory patching, and trivial uninstall. The XInput side stays passive (no hooks).

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

## Hiding DirectInput gamepads

Symptom it fixes: a physical pad is visible to the game twice, once as an XInput user
(real or via this shim) and once as a DirectInput gamepad. Input arriving from each
source switches the prompt style (Xbox glyphs vs generic "Button N"), and actions can
double-fire.

Two layers, deliberately separate:

1. **Interception** (`src/di_minhook.cpp`) — getting control when the game calls
   `DirectInput8Create`, using a MinHook inline detour inside the XInput proxy.
2. **Hiding** (`src/di_hide.cpp`) — patching the returned `IDirectInput8A/W` vtable in
   place: `EnumDevices` (slot 4) and `EnumDevicesBySemantics` (slot 9) get thunk
   callbacks that drop devices whose `dwDevType` low byte is joystick/gamepad/driving/
   flight/first-person (`src/di_filter.h`, unit-tested). Keyboard, mouse and everything
   else pass through; the game never learns the GUIDs of hidden pads.

### Interception: MinHook detour

The XInput proxy inline-detours `DirectInput8Create` in the genuine `dinput8.dll`.

- **Timing.** The exe imports `DINPUT8.dll` in its normal import table (not delay-loaded;
  confirmed with `inspect_imports.py`), so the module is already mapped when our `DllMain`
  runs and `MH_CreateHookApi` (which only calls `GetModuleHandle`, never `LoadLibrary`)
  succeeds before any game code executes. If the module were not mapped, the install is
  retried at the first XInput call.
- **Detour.** Runs on the game's thread: calls the original, then (if enabled) vtable-patches
  the result. It reads `dmc_sdl.ini` on demand via `ShimConfig()`, so the config is
  available even if DirectInput is created before any XInput call.
- **Why MinHook is only used for this one function.** The hiding is the vtable patch;
  replacing it with inline hooks on the method bodies would add risk without adding coverage.
- **Risks to be aware of:**
  - It is the one place we do real work under the loader lock: `MH_EnableHook` briefly
    suspends other threads while patching. At process start that is normally harmless.
  - The game bundles `EasyHook32.dll`; if it (or an overlay such as Steam's or ReShade's)
    also detours `DirectInput8Create`, MinHook may refuse the target
    (`MH_ERROR_UNSUPPORTED_FUNCTION`) or hooks may chain unexpectedly. The status is written
    to `dmc_sdl.log` ("DirectInput hook (MinHook): ..."). If the hook cannot be installed,
    DirectInput pads stay visible to the game (the duplicate-pad symptom returns).
  - If a `dinput8.dll` exists in the game folder (left by an older version of this project,
    ReShade, ...), that module is the one found by name and gets hooked; the log warns.
  - The detour stays installed for the life of the process (the proxy is never unloaded).

### Details

- Skipped `EnumDevicesBySemantics` devices are intentionally not `Release`d (ownership
  of that reference is unverified; a leak beats a double release).
- Guard: if SDL itself ever enumerates DirectInput through the patched interface (SDL3
  normally uses `CoCreateInstance`, bypassing it), the filter is skipped for that thread
  so SDL still sees the pads (`ShimInSdlCall`).
- Disabled by `hide_dinput_pads = false` or `mode = passthrough`, or at build time with
  `-DDMC_USE_MINHOOK=OFF` (which also drops the MinHook dependency).

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

## Other games and 64-bit

Nothing in the logic is 32-bit specific: struct layouts are pinned with `static_assert`,
and the calling convention differences vanish on x64 (so `.def` exports are undecorated).
What varies per game, and is therefore a build option, is:

| Varies | Option / mechanism |
|---|---|
| Bitness | Pick the `win32` or `win64` preset; it must match the exe (`inspect_imports.py` shows it). |
| XInput DLL name | `DMC_XINPUT_DLL` (default `xinput1_3`; `win64` preset uses `xinput1_4`). Used for the output name, the generated `.def`, and the real DLL we forward to (`src/build_config.h`). |
| Imported by ordinal? | `DMC_EXPORT_ORDINALS`. Names are always exported. |

The real DLL is loaded from `GetSystemDirectoryW`, which is `System32` for a 64-bit
process and `SysWOW64` for a 32-bit one, so the right genuine DLL is chosen automatically.

What this approach cannot do:

- Games that don't load XInput from their own folder (absolute `System32` paths, or
  `LOAD_LIBRARY_SEARCH_SYSTEM32`-style loading) will never load the proxy.
- Games that don't use XInput at all (Windows.Gaming.Input, RawInput, SDL, or their own HID).
- Anti-cheat or integrity checks that reject side-loaded DLLs. Single-player only.
- `xinput1_4` has extra exports beyond 1_3 (e.g. `XInputGetBaseBusInformation`,
  `XInputGetCapabilitiesEx`); they are not provided, so software that calls them
  directly will fail on the proxy.

## Known gaps

- Rumble is not explicitly stopped at process exit (nothing safe to run during
  loader-lock teardown); drivers stop it when the device handle closes.
- The vtable layout (slots 4 and 9), the DirectInput IIDs and the MinHook API details (v1.3.3 file layout, `MH_CreateHookApi` never loading modules) are from memory.
- Not yet built or exercised against the real game — see the README checklist.
