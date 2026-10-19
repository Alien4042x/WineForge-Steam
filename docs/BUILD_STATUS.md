# Build and validation status

The initial source extraction preserved the previously built local native
source, embedded scripts and settings UI. Its source snapshot is recorded in
`provenance.json`. No compiler or application test was run during that initial
extraction.

The standalone build uses the pinned `deps/Dobby` checkout and CMake from PATH.
The focused overlay target builds independently of Dobby and the native hook.
The payload exporter records exact resource and source hashes. Exporting a
candidate does not install it or mark it ready for games.

## Overlay update, 2026-10-19

The universal overlay shim has been built. Its ownership, completion, discard
and rebuild regressions passed on arm64 and x86_64 without a GPU or Steam.
The old implementation reproduced the leaked variant reference.

A subsequent Backrooms run with the matching WineForge launcher update mapped
both the rebuilt shim and native Steam renderer. The maintainer confirmed the
visible overlay. The scoped result and exact binary identities are in
[overlay validation](validation/overlay-20261019.json). Fullscreen cursor and
Dock behavior remain a separate investigation.

The exporter was also exercised with isolated non-executable build fixtures.
Resource/source hashes and new overlay provenance matched. Existing outputs,
changed source and missing build files were rejected. This checks the export
contract, not a fresh complete native-hook build.

Fresh candidate exports keep connected and game readiness false until their
exact binaries are tested. The historical extraction result and the scoped
game result do not automatically validate another build or client.
