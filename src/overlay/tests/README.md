# Pipeline ownership regressions

These tests come from `overlay-shim/tests` in NotProton commit
`279167917dc6d0a84fb0305b0c88b11603c4bf92`. The original GPL-3.0 license and
notices apply.

Run `make overlay-tests` to check pipeline ownership, retained and unretained
command-buffer completion/discard, and repeated rebuilds with compilation
failure fallback on arm64 and x86_64. The tests use mock Metal objects without
a GPU or Steam.
