# Prepared extraction status

The copied native source, embedded scripts and settings UI match the previously built local source byte for byte. Source hashes are recorded in `provenance.json`.

The original local build depended on a sibling Dobby source tree and a machine-specific CMake path. This extraction uses the pinned `deps/Dobby` checkout and resolves CMake from PATH. Its Makefile also exposes a candidate payload export target.

These build-entry changes are prepared for review. No dependency was fetched and no compiler, linker, native probe, Steam session or game was run as part of this extraction. A successful build and connected test must be recorded separately before distributing newly generated binaries.

The previously installed WineForge application and its Steam connection remain independent of this directory. Neither is redirected to this extraction during preparation.
