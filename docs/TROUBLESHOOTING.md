# Troubleshooting

**Game ignores my pad.** Set `log = true`, play briefly, read `dmc_sdl.log`:

- no `loaded` / `SDL backend ready` line → the DLL isn't being loaded (is it in the
  same folder as `DMC-DevilMayCry.exe`? 32-bit build?). Run `xinput_harness.exe` from that folder.
- `ignoring Xbox-type pad` → expected; that pad is served by real XInput. Set
  `ignore_xbox_pads = false` only if real XInput doesn't see it.
- no pad line at all → SDL doesn't recognise the device. Add a mapping line to
  `gamecontrollerdb.txt` (generate one with SDL's controller mapping tools).

**Prompts keep switching between generic "Button N" and Xbox glyphs.** The game is reading
the same pad through DirectInput *and* XInput. Install `dinput8.dll` alongside
`xinput1_3.dll` (both next to the exe) and keep `hide_dinput_pads = true`. To confirm:
set `log = true`, start the game, then read `dmc_sdl_dinput.log`:

- `DirectInput device type=0x... -> hidden` for your pad → the proxy is working; if prompts
  still flip, send both logs.
- no `dinput8 proxy starting` line → `dinput8.dll` isn't being loaded (wrong folder, or a
  64-bit build?).
- the pad shows as `-> visible` → its DirectInput type isn't joystick/gamepad class; send
  the type value and we can extend the filter.
- other overlays/wrappers that also ship a `dinput8.dll` (ReShade, some mod loaders)
  conflict with this one. Chain them by renaming theirs and tell us which one it is.

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
