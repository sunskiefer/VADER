#!/usr/bin/env bash
# Offline test of the desktop VST2 build: loads it in a minimal host and plays it. Usage: test/desktop_test.sh [plugin.so]
set -euo pipefail
cd "$(dirname "$0")/.."
SO="${1:-build/desktop/bin/VADER-vst2.so}"
mkdir -p build/test
g++ -std=c++17 -O1 -o build/test/desktop_host test/desktop_host.cpp -ldl
HOME="$(mktemp -d)" XDG_DATA_HOME="" build/test/desktop_host "$SO"
