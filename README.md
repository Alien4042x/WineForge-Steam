# WineForge-Steam

Native macOS Steam integration for WineForge. This project contains the Steam client hook, the Metal overlay shim, per-game settings UI and the compatibility-tool entry script.

> **Status: Experimental.** Bugs, incomplete behavior and compatibility issues may occur. Client compatibility is checked automatically. Steam updates that change the required interfaces can still require a profile or hook fix.

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

The shim includes NotProton's pipeline ownership fix
[`f5a871a`](https://github.com/NotProtonNot/NotProton/commit/f5a871a329bba165cf41b3f5e7b105b3c0cab7ba),
with WineForge's process/backend and late-install gates retained. Run its
ownership, completion, discard and rebuild regression tests without Steam or a
GPU using `make overlay-tests`. The test target runs arm64 and x86_64 controls.

Outputs are written to `build/`. The local build uses ad-hoc signing. Building does not install a hook, change Steam.app or start Steam or a game.

Create a candidate integration payload with:

```sh
make payload
```

The exporter records file hashes and preserves licenses. It refuses an existing output directory and changed reference source files. A new payload still requires connected validation before it can be treated as ready for game launches.

## Host integration

The binaries require a compatible host application that implements the compatibility-tool entry and [settings protocol](SETTINGS_CONTRACT_V1.md). This source project contains the native components and build tools. It does not contain the host application's installer or settings service.

The host preserves file hashes as exact installation identities. With the
automatic compatibility policy it checks the current client using the bundled
`wineforge-client-check` before installing, rather than requiring its hash to
match the reference client. The checker maps the client as non-executable data,
resolves all 16 required functions and validates the compatibility-manager
layout. Missing, ambiguous or incompatible results reject installation.
Installation ownership, file integrity and live hook readiness remain separate
checks. The host application's selected installation mode determines whether
Steam.app is modified. Anti-cheat compatibility is not established by these checks.

The matching host update must accept Steam's trusted
`steamloader.dylib:gameoverlayrenderer.dylib` request, retain approved 64-bit
loader ordering and insert the packaged shim first. It also stages the official
install-script helpers under the actual client `legacycompat` directory with
ownership and rollback. These host-side fixes are maintained in the launcher.
Updating this repository alone does not update an installed launcher.

Game environments live under `NotProton/<AppID>` in the selected WineForge storage folder. Each environment contains a private `drive_c/WineForge/NativeSteam` directory for Steam client DLLs. Inside Windows that private directory is `C:\WineForge\NativeSteam`. The environment name remains NotProton.

## Validation status

The retained reference uses native client `625715ad`. Its complete SHA-256 is the profile directory name under `profiles/`.

On 2026-10-14 the original integration passed all 16 client anchors and the structure-layout check. A real native session registered all 9 hooks. A separate 32.128-second control verified the 64-bit and 32-bit Wine APIs, private client DLLs, actual mapped libraries, arguments and exit-code forwarding. Its test processes were cleaned up.

That control did not render a game. It does not establish overlay behavior, graphics performance or compatibility with every Steam game.

The historical extraction and checker reference are retained in
[client validation](docs/validation/client-625715ad.json).

On 2026-10-19 the rebuilt overlay shim passed ownership tests on arm64 and
x86_64. A maintainer then confirmed a visible Steam overlay in Backrooms:
Escape Together using the matching WineForge launcher update. A successful
process mapping read independently confirmed the shim, Steam renderer and
Core driver in the Shipping process. See the scoped
[overlay validation record](docs/validation/overlay-20261019.json).

This game result does not establish fullscreen cursor/Dock behavior or validate
every game. Fresh candidate exports keep connected and game readiness false
until their exact binaries are tested.

The profile's reference client hash records provenance. Compatible client
updates do not require a new hash allowlist entry. The automatic check does not
transfer historical connected-client or game acceptance to a new client.

Build and test the passive checker separately, without fetching Dobby or
building/installing a Steam hook:

```sh
make client-check client-check-tests
build/wineforge-client-check --json /path/to/steamclient.dylib profiles/625715ada315323351ada75a077142c15d4b742be2db9e63ec354828082ab194/client-profile.json
```

Run the settings panel and migration tests without Steam or Wine:

```sh
make settings-tests
```

This target generates the embedded native component required by the JavaScript
tests. It does not fetch Dobby or install a hook.

## Sources and licenses

The [NotProton source](https://github.com/NotProtonNot/NotProton) is pinned to commit [05a27874798ffbffb8e64a37595bde6f16d5320a](https://github.com/NotProtonNot/NotProton/commit/05a27874798ffbffb8e64a37595bde6f16d5320a). Dobby is pinned to `5dfc8546954ce3b3198132ab13fddb89ee92cdd7`.

Original copyright notices and license files are retained. WineForge adaptations carry `WineForge-Internal` markers. See [third-party notices](THIRD_PARTY.md) and [source provenance](provenance.json).

This repository contains no Valve Steam client binaries, Steam accounts, installed games, user settings or bottle data.
