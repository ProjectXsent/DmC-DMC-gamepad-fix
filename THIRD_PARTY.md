# Third-party software

Fetched at build time (not committed to this repository):

| Component | Used for | License |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Gamepad input, rumble, hotplug (linked statically) | zlib |
| [MinHook](https://github.com/TsudaKageyu/minhook) (incl. Hacker Disassembler Engine) | In-process detour of `DirectInput8Create` | BSD-2-Clause |
| [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | Optional community controller mappings, bundled in release zips | zlib |

Release binaries statically include SDL3 and MinHook; their license texts apply to those parts.
