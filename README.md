# ClaudeBawtLatest

Windows x64 research client for an authorized Realm of the Mad God private-server and reverse-engineering environment. This snapshot targets the 2026-08-17 IL2CPP build.

## Build

Requirements:

- Windows 10 or newer
- Visual Studio 2022/2026 with the **Desktop development with C++** workload
- MSVC x64 build tools and the Windows SDK

From a Developer Command Prompt, run:

```bat
build.bat
```

The release output is written to `dist\ClaudeBawt.dll`, with runtime assets copied beside it. A debug build can be produced with:

```bat
build.bat Debug
```

## Repository layout

- `src/` — client and feature source
- `assets/interactive-map/` — map textures and attribution
- `vendor/imgui/` — minimal Dear ImGui sources used by this project
- `vendor/minhook/` — minimal MinHook sources used by this project
- `vendor/font/` — bundled UI font
- `build.bat` — self-contained MSVC build script

## Current build notes

- Offsets and native patch sites are build-specific and collected in `src/il2cpp.h` plus feature-local constants.
- The 2026-08-17 safety pass corrected the object-effects layout and projectile-property path.
- `UnityThread.Update()` is mapped at `GA+0x144C280` for noclip gating.
- Ambiguous hooks remain disabled instead of falling back to unverified addresses.
- Generated IL2CPP dumps, game binaries, logs, local configuration, and compiled outputs are intentionally excluded from Git.

## Third-party software

This repository includes source from:

- [Dear ImGui](https://github.com/ocornut/imgui), under its MIT license in `vendor/imgui/LICENSE.txt`.
- [MinHook](https://github.com/TsudaKageyu/minhook), under its license in `vendor/minhook/LICENSE.txt`.

Interactive-map attribution is retained in `assets/interactive-map/ATTRIBUTION.txt`.

## Scope

Use only in environments you own or are explicitly authorized to test. No game binaries, metadata files, credentials, or private configuration are included.
