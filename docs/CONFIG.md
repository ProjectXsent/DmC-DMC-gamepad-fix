# Configuration reference (`dmc_sdl.ini`)

The file sits next to `xinput1_3.dll`. Missing file or keys fall back to defaults.
Booleans accept `true/false`, `1/0`, `yes/no`, `on/off`. Unknown keys are ignored.
Changes take effect on the next launch.

## [General]

| Key | Default | Meaning |
|---|---|---|
| `mode` | `hybrid` | `hybrid`, `sdl`, or `passthrough` (see DESIGN.md). |
| `prefer_sdl` | `false` | In hybrid, SDL pad wins over a real XInput pad in the same slot. |
| `ignore_xbox_pads` | `true` | Skip Xbox-type pads in SDL (real XInput serves them). |
| `hide_dinput_pads` | `true` | Hides gamepad-class devices from the game's DirectInput (MinHook detour inside the XInput proxy) so the pad isn't seen twice. Ignored when `mode = passthrough`. |
| `log` | `false` | Write `dmc_sdl.log` next to the DLL (DirectInput hook status, devices, mapping, errors). |
| `mappings_file` | `gamecontrollerdb.txt` | Extra SDL mappings; relative to the DLL's folder. |

## [Input]

| Key | Default | Meaning |
|---|---|---|
| `swap_ab` / `swap_xy` | `false` | Swap face buttons (label-matching on Nintendo layouts). |
| `left_deadzone` / `right_deadzone` | `0` | Radial deadzone, 0–99 %, output rescaled. |
| `trigger_threshold` | `0` | Trigger deadzone, 0–99 %, output rescaled. |
| `rumble_scale` | `100` | Rumble strength %, 0–200 (clamped to hardware max). |

## [Hints]

Raw SDL hints, `NAME = VALUE`, applied after the shim's defaults and before SDL starts.
Do **not** set `SDL_XINPUT_ENABLED=1`; it can make SDL load this proxy recursively and
duplicates Xbox pads.
