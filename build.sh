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

TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -std=gnu11"
OBJS=""
for f in $PLUGIN_SRCS src/wrapper/vst2_wrap.c; do
  o="build/arm/$(echo "$f" | tr / _).o"
  $ZIG cc $TGT $COMMON -Ibuild -I"$MPC_VST/wrapper" $INCS -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
$ZIG cc $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections -Wl,-s \
  $OBJS -lm -ldl -lpthread -o build/arm/vader.so
PKG="build/package/$NAME"
rm -rf build/package && mkdir -p "$PKG"
cp -r "build/skin/$NAME/." "$PKG/"
cp build/arm/vader.so "$PKG/"
mkdir -p "$PKG/Shapes" && cp shapes/* "$PKG/Shapes/"
echo "-> $PKG"
