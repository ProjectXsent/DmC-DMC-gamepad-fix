# data/

`gamecontrollerdb.txt` is **not committed**. It is the community mapping database from
<https://github.com/mdqinc/SDL_GameControllerDB> (zlib licensed). SDL3 already contains
built-in mappings for common pads, so this file is optional; it adds coverage for
obscure and generic controllers.

CI downloads it when building a release. To add it locally:

```powershell
Invoke-WebRequest https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/master/gamecontrollerdb.txt -OutFile data/gamecontrollerdb.txt
```

You can also append your own `SDL_GAMECONTROLLERCONFIG`-style lines to the installed copy.
