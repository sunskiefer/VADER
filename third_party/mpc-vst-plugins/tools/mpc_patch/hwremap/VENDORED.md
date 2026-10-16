# Vendored from akai_standalone_remap

Source: https://github.com/mmiroshnikov/akai_standalone_remap
Commit: `9d2aa57a570b0c4788f88b04ca242bb82176c380` (message: `LICENSE: MIT`)
Licence: MIT (`src/LICENSE`). Copyright (c) 2026 Misha Miroshnikov.

Copied as-is: `src/hwremap.c`, `src/test_hwremap.c`, `configs/mpc-live.conf`, `configs/force.conf`.

Local change, build only: the device library is compiled with `arm32v7/gcc:11-bullseye` (glibc 2.31, the same image the plugins here use) instead of that repo's `arm32v7/gcc:12`. No source lines were edited.
