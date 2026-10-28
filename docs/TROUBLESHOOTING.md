# Troubleshooting

**Game ignores my pad.** Set `log = true`, play briefly, read `dmc_sdl.log`:

- no `loaded` / `SDL backend ready` line → the DLL isn't being loaded (is it in the
  same folder as `DMC-DevilMayCry.exe`? 32-bit build?). Run `xinput_harness.exe` from that folder.
- `ignoring Xbox-type pad` → expected; that pad is served by real XInput. Set
  `ignore_xbox_pads = false` only if real XInput doesn't see it.
- no pad line at all → SDL doesn't recognise the device. Add a mapping line to
  `gamecontrollerdb.txt` (generate one with SDL's controller mapping tools).

**Prompts keep switching between generic "Button N" and Xbox glyphs.** The game is reading
the same pad through DirectInput *and* XInput. Keep `hide_dinput_pads = true`, set
`log = true`, start the game and read `dmc_sdl.log`:

- `DirectInput hook (MinHook): installed` and then `DirectInput device type=0x... -> hidden`
  for your pad → working. If prompts still flip, send the log.
- `DirectInput hook (MinHook): MH_ERROR_...` → the detour couldn't be installed (usually
  because something else already hooks `DirectInput8Create`, e.g. an overlay or another
  mod). The pad then stays visible to the game's DirectInput; send the log.
- `WARNING: hooked a dinput8.dll outside the system folder` → there is a `dinput8.dll` in the
  game folder (left by an older version of this project, or ReShade/another tool). If it is
  ours from an older version, delete it (and rename `dinput8.dll.dmc-backup` back to
  `dinput8.dll` if one exists).
- the pad shows as `-> visible` → its DirectInput type isn't joystick/gamepad class; send
  the type value and we can extend the filter.
- no `DirectInput device` lines at all → the game never enumerated DirectInput through the
  hooked function; send the log.

**Buttons are wrong.** Positional mapping is default. Try `swap_ab` / `swap_xy`, or fix
the pad's mapping in `gamecontrollerdb.txt`.

**Double inputs / pad appears twice.** Leave `ignore_xbox_pads = true`. If using Steam
Input, either let Steam present an Xbox pad (works untouched) or disable Steam Input for
the game and use this shim, not both.

**Rumble stuck or weak.** Adjust `rumble_scale`. DualShock/DualSense rumble behaviour can be
tuned with SDL hints in `[Hints]`.

**Rule out the shim.** Set `mode = passthrough` or run `install.ps1 -Uninstall`.

**Report a bug.** Include `dmc_sdl.log`, the pad model/connection type, and the
`tools/inspect_imports.py` output for your exe.
