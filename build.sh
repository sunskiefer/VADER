#!/usr/bin/env bash
# Build VADER for MPC OS (armv7-a hard-float, glibc <= 2.31 so it also loads on MPC OS 2.x).
#   ./build.sh                     (MPC_VST=../mpc-vst-plugins to use another framework checkout)
# Needs: python3 + Pillow and numpy, a host gcc (for shadow_art), and Zig (pip install ziglang). No Docker.
# Output: build/package/L'Cronx - VST - VADER/  (skin, vader.so and the Shapes folder)
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
NAME="L'Cronx - VST - VADER"
. ./build_srcs.sh
python3 tools/gen_params.py
python3 tools/gen_web.py
python3 tools/vader_skin.py --layout

mkdir -p build/tools build/arm
[ -x build/tools/shadow_art ] || gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -x c -o build/tools/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
rm -rf build/skin
SHADOW_ART="$PWD/build/tools/shadow_art" \
  python3 "$MPC_VST/tools/gen_vst.py" vst.json 2>&1 | grep -v -e "TOUCH" -e "EDGE" -e "MODULE_DIR" -e "QLINK" || true
[ -f build/params.h ] && [ -d "build/skin/$NAME/Plugin Skins" ]
gcc -O1 -w -std=gnu11 -Isrc -o build/tools/shape_dump tools/shape_dump.c src/shape.c -lm   # the built-in shapes, for their pictures
python3 tools/vader_skin.py "build/skin/$NAME/Plugin Skins"      # every page, from tools/vader_skin.py
python3 tools/check_skin.py "build/skin/$NAME"                    # touch boxes, edges, Q-Links, as MPC loads it (open lists excepted)

COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -std=gnu11"
[ "${VADER_NOSCREEN:-}" = 1 ] && COMMON="$COMMON -DNOSCREEN=1"   # a test build with no screen at all
CXXCOMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w $CXXFLAGS_MI"
# one binary per CPU: build_so <out dir> <target flags>
build_so() {
  local D="$1" TGT="$2" OBJS="" f o
  mkdir -p "$D"
  for f in $PLUGIN_SRCS src/wrapper/vst2_wrap.c; do
    o="$D/$(echo "$f" | tr / _).o"
    $ZIG cc $TGT $COMMON -Ibuild -I"$MPC_VST/wrapper" $INCS -c "$f" -o "$o"
    OBJS="$OBJS $o"
  done
  for f in $ZYN_SRCS; do   # the ZYNTH voices (own include paths)
    o="$D/$(echo "$f" | tr / _).o"
    $ZIG cc $TGT $COMMON $ZINCS -c "$f" -o "$o"
    OBJS="$OBJS $o"
  done
  for f in $CXX_SRCS; do   # the Mutable Instruments engines
    o="$D/$(echo "$f" | tr / _).o"
    $ZIG c++ $TGT $CXXCOMMON $CXXINCS -c "$f" -o "$o"
    OBJS="$OBJS $o"
  done
  printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > "$D/exports.map"
  $ZIG cc $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script="$D/exports.map" -Wl,--gc-sections -Wl,-s \
    $OBJS -lm -ldl -lpthread -o "$D/vader.so"
}
# 32-bit: MPC X, Live, Live II, One, Key 61/37, Force
build_so build/arm "-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
# 0.10.0, 64-bit: MPC Live III, XL and later (MPC OS 3.10.1, Akai's plugin folder format)
build_so build/arm64 "-target aarch64-linux-gnu.2.31 -mcpu=generic -ffp-contract=off"
PKG="build/package/$NAME"
rm -rf build/package && mkdir -p "$PKG"
cp -r "build/skin/$NAME/." "$PKG/"
cp build/arm/vader.so "$PKG/"
mkdir -p "$PKG/Shapes" && cp shapes/* "$PKG/Shapes/"
mkdir -p "$PKG/Samples" && cp Samples/* "$PKG/Samples/"   # 0.8: example samples (vader 1..54)
echo "-> $PKG"
# 0.10.0: the same plugin in Akai's folder format (MPC OS 3.10.1: copy the folder into "VST2 Plugins" on a drive)
python3 tools/sdk_folder.py
