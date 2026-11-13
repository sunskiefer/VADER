#!/usr/bin/env bash
# The engine test built for ARM (32-bit and, since 0.10.0, 64-bit) with the plugin's own target flags (static musl, so it runs under qemu without an
# ARM glibc) and run under qemu: checks the ARM code generation. Needs qemu-user (qemu-arm or qemu-arm-static) and Zig.
set -euo pipefail
cd "$(dirname "$0")/.."
. ./build_srcs.sh
ZIG="${ZIG:-python3 -m ziglang}"
MPC_VST=third_party/mpc-vst-plugins
python3 tools/gen_params.py >/dev/null
python3 tools/gen_web.py >/dev/null
python3 $MPC_VST/tools/gen_vst.py vst.json --params-h >/dev/null
FL="-O2 -w -std=gnu11 -Ibuild -I$MPC_VST/wrapper $INCS"
# sim <dir> <target flags> <qemu names...>: the engine test as that CPU's code, run under qemu
sim() {
  local D="$1" TGT="$2"; shift 2
  mkdir -p "$D"
  local OBJS="" f o
  for f in $PLUGIN_SRCS test/engine_test.c; do o="$D/$(echo $f | tr / _).o"; $ZIG cc $TGT $FL -c $f -o $o; OBJS="$OBJS $o"; done
  # 0.8: the ZYNTH voices (C) and the Mutable Instruments engines (C++)
  for f in $ZYN_SRCS; do o="$D/$(echo $f | tr / _).o"; $ZIG cc $TGT -O2 -w -std=gnu11 $ZINCS -c $f -o $o; OBJS="$OBJS $o"; done
  for f in $CXX_SRCS; do o="$D/$(echo $f | tr / _).o"; $ZIG c++ $TGT -O2 -w $CXXFLAGS_MI $CXXINCS -c $f -o $o; OBJS="$OBJS $o"; done
  $ZIG c++ $TGT -static $OBJS -lm -o "$D/engine_test"
  local QEMU=""
  for q in "$@"; do QEMU=$(command -v "$q" || true); [ -n "$QEMU" ] && break; done
  [ -n "$QEMU" ] || { echo "arm_sim: none of $* found"; exit 1; }
  VADER_SLOW=1 $QEMU "$D/engine_test"   # emulated: the engine test's timing check is skipped
  if [ "${SERVERS:-}" = 1 ]; then   # the companion screen's server and PLUG, served by this CPU's code
    local E=$(echo $OBJS | tr ' ' '\n' | grep -v engine_test | tr '\n' ' ')
    $ZIG cc $TGT $FL -c test/skin_dump.c -o "$D/skin_dump.o"
    $ZIG c++ $TGT -static "$D/skin_dump.o" $E -lm -o "$D/skin_dump"
    VADER_SKIN_DUMP="$QEMU $D/skin_dump" python3 test/web_test.py
    VADER_SKIN_DUMP="$QEMU $D/skin_dump" python3 test/mirror_test.py
  fi
}
sim build/armsim "-target arm-linux-musleabihf -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off" qemu-arm qemu-arm-static
# 0.10.0: the 64-bit MPCs (Live III, XL)
SERVERS=1 sim build/arm64sim "-target aarch64-linux-musl -mcpu=generic -ffp-contract=off" qemu-aarch64 qemu-aarch64-static
