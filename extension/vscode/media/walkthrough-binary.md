# vectra CLI

Vectra ships as a single C++ binary (`vectra` / `vectra.exe`). The
extension is a thin wrapper — every action shells out to the CLI.

## Where to put it

- macOS / Linux: anywhere on `$PATH` (e.g. `/usr/local/bin/vectra`)
- Windows: anywhere on `%PATH%` (e.g. `%LOCALAPPDATA%\Programs\Vectra\vectra.exe`)
- Or set the absolute path in the `vectra.binary` setting and skip
  PATH altogether.

## Build from source

```sh
git clone --recurse-submodules https://github.com/HenryBuilds/Vectra
cd Vectra
cmake --preset release
cmake --build --preset release
```

The resulting binary lives in `build/release/bin/`. See the repo's
README for platform-specific GPU presets.
