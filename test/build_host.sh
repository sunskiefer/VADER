#!/usr/bin/env bash
# Host (x86) build of the plugin for the offline tests. Usage: test/build_host.sh [extra cflags]
set -euo pipefail
cd "$(dirname "$0")/.."
. ./build_srcs.sh
MPC_VST=third_party/mpc-vst-plugins
python3 tools/gen_params.py >/dev/null
python3 tools/gen_web.py >/dev/null
python3 $MPC_VST/tools/gen_vst.py vst.json --params-h >/dev/null
mkdir -p build/host
CC="${CC:-clang}"
FL="-O1 -g -w -std=gnu11 -Ibuild -I$MPC_VST/wrapper $INCS $*"
OBJS=""
for f in $PLUGIN_SRCS src/wrapper/vst2_wrap.c; do o="build/host/$(echo $f | tr / _).o"; $CC -c $FL $f -o $o; OBJS="$OBJS $o"; done
# 0.8: the ZYNTH voices (C, own include paths) and the Mutable Instruments engines (C++)
for f in $ZYN_SRCS; do o="build/host/$(echo $f | tr / _).o"; $CC -c -O1 -g -w -std=gnu11 $ZINCS $* $f -o $o; OBJS="$OBJS $o"; done
CXXC="${CXX:-clang++}"
for f in $CXX_SRCS; do o="build/host/$(echo $f | tr / _).o"; $CXXC -c -O1 -g -w $CXXFLAGS_MI $CXXINCS $f -o $o; OBJS="$OBJS $o"; done
echo "$OBJS" > build/host/objs.txt
