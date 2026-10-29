#!/usr/bin/env bash
# Offline test of the 0.8 modes and maths shapes (plain C, no plugin host)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/test
gcc -O1 -std=gnu11 -Isrc -o build/test/modes_test test/modes_test.c src/modes.c src/paths.c src/shape.c -lm
build/test/modes_test
