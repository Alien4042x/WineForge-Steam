# WineForge-Steam

Native macOS Steam integration for WineForge. This project contains the Steam client hook, the Metal overlay shim, per-game settings UI and the compatibility-tool entry script.

> **Status: Experimental.** Bugs, incomplete behavior and compatibility issues may occur. Steam client updates may require a new profile or further fixes.

The hook and shim build separately from Wine. A compatible host application provides installation, game environments and the authenticated local settings service.

This is an unofficial integration based on [NotProton](https://github.com/NotProtonNot/NotProton). It is not endorsed by Valve.

## Project boundaries

| Project | Responsibility |
| --- | --- |
| [WineForge](https://github.com/Alien4042x/WineForge) | Wine loader bridge, lsteamclient and Windows steam.exe helper |
| WineForge-Steam | Native Steam hook, Metal overlay shim, supported-client profiles and native build |

The native hook runs in the ARM64 Steam client. Its Intel slice is inert. The Wine side supports x86_64 and 32-bit WoW64 games. The overlay shim contains ARM64 and x86_64 slices.

## Build

Requirements are macOS 15 or later, Xcode Command Line Tools, CMake, Make and Python 3. Node.js is needed only for the JavaScript checks.

Fetch the exact pinned Dobby dependency, then build both native libraries.

```sh
make deps
make
```

Build only the overlay shim with:

```sh
make overlay
```

Outputs are written to `build/`. The local build uses ad-hoc signing. Building does not install a hook, change Steam.app or start Steam or a game.

Create a candidate integration payload with:

```sh
make payload
```

The exporter records file hashes and preserves licenses. It refuses an existing output directory and changed reference source files. A new payload still requires connected validation before it can be treated as ready for game launches.

## Host integration

The binaries require a compatible host application that implements the compatibility-tool entry and [settings protocol](SETTINGS_CONTRACT_V1.md). This source project contains the native components and build tools. It does not contain the host application's installer or settings service.

The host verifies the exact supported client hash, preserves the original Steam files and installs the owned connection. Installation changes Steam.app and replaces its Valve signature with a local ad-hoc signature. Anti-cheat compatibility is not established by the connection tests.

Game environments live under `NotProton/<AppID>` in the selected WineForge storage folder. Each environment contains a private `drive_c/WineForge/NativeSteam` directory for Steam client DLLs. Inside Windows that private directory is `C:\WineForge\NativeSteam`. The environment name remains NotProton.

## Validation status

The retained reference uses native client `625715ad`. Its complete SHA-256 is the profile directory name under `profiles/`.

On 2026-10-14 the original integration passed all 16 client anchors and the structure-layout check. A real native session registered all 9 hooks. A separate 32.128-second control verified the 64-bit and 32-bit Wine APIs, private client DLLs, actual mapped libraries, arguments and exit-code forwarding. Its test processes were cleaned up.

That control did not render a game. It does not establish overlay behavior, graphics performance or compatibility with every Steam game.

This repository is a source extraction of that reference. The portable Makefile and payload exporter prepared for this repository have not been used for a new build yet. Previous binary results do not automatically validate future binaries. See [the reference evidence](docs/validation/client-625715ad.json).

Only exact validated client versions are supported. A Steam update can require a new profile and connected verification.

## Sources and licenses

The [NotProton source](https://github.com/NotProtonNot/NotProton) is pinned to commit [05a27874798ffbffb8e64a37595bde6f16d5320a](https://github.com/NotProtonNot/NotProton/commit/05a27874798ffbffb8e64a37595bde6f16d5320a). Dobby is pinned to `5dfc8546954ce3b3198132ab13fddb89ee92cdd7`.

Original copyright notices and license files are retained. WineForge adaptations carry `WineForge-Internal` markers. See [third-party notices](THIRD_PARTY.md) and [source provenance](provenance.json).

This repository contains no Valve Steam client binaries, Steam accounts, installed games, user settings or bottle data.
