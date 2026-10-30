#!/usr/bin/env bash
# Build VADER as VST2 + VST3 for the desktop.
#   desktop/build.sh linux     -> build/desktop/bin/    (VADER-vst2.so, VADER.vst3)
#   desktop/build.sh windows   -> build/desktop-win/bin (VADER-vst2.dll, VADER.vst3), cross-compiled with mingw-w64 (posix threads)
#   desktop/build.sh package [version]   -> dist/VADER-<version>-desktop-<os>.zip for whichever builds exist
# Needs: cmake, a C/C++ compiler, python3, and on Linux the X11/OpenGL dev packages (libx11-dev libxext-dev libxrandr-dev
# libxcursor-dev libxinerama-dev libgl1-mesa-dev). Windows: g++-mingw-w64-x86-64-posix.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/gen_params.py >/dev/null
python3 tools/gen_desktop.py
case "${1:-linux}" in
  linux)
    cmake -S desktop -B build/desktop -DCMAKE_BUILD_TYPE=Release
    cmake --build build/desktop -j"$(nproc)" ;;
  windows)
    cmake -S desktop -B build/desktop-win -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/desktop/toolchain-mingw64.cmake"
    cmake --build build/desktop-win -j"$(nproc)" ;;
  package)
    V="${2:-0.7.0}"; mkdir -p dist
    for t in linux:build/desktop:linux-x86_64 windows:build/desktop-win:windows-x86_64; do
      IFS=: read -r _ dir tag <<<"$t"
      [ -d "$dir/bin" ] || continue
      S="build/pkg/VADER-$V-desktop-$tag"; rm -rf "$S"; mkdir -p "$S"
      cp -r "$dir"/bin/VADER* "$S/"
      # the example samples: inside the VST3 bundle (found automatically) and as a Samples folder beside the VST2 file
      for b in "$S"/VADER.vst3; do [ -d "$b/Contents" ] && mkdir -p "$b/Contents/Resources" && cp -r Samples "$b/Contents/Resources/Samples"; done
      cp -r Samples "$S/Samples"
      cp docs/DESKTOP.md "$S/README.md"; cp LICENSE "$S/LICENSE"; cp desktop/THIRD_PARTY.md "$S/THIRD_PARTY.md"
      (cd build/pkg && rm -f "../../dist/VADER-$V-desktop-$tag.zip" && python3 -m zipfile -c "../../dist/VADER-$V-desktop-$tag.zip" "VADER-$V-desktop-$tag")
      echo "dist/VADER-$V-desktop-$tag.zip"
    done ;;
  *) echo "usage: $0 linux|windows|package [version]"; exit 1 ;;
esac
