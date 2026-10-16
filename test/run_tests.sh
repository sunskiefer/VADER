#!/usr/bin/env bash
# Offline tests: the engine on its own (test/engine_test.c) and the framework's host test (the VST2 wrapper around the
# engine, as MPC drives it), both under valgrind (NOVG=1 to skip it).
set -euo pipefail
cd "$(dirname "$0")/.."
. ./build_srcs.sh
bash test/build_host.sh
CC="${CC:-clang}"
OBJS=$(cat build/host/objs.txt)
ENG=$(echo $OBJS | tr ' ' '\n' | grep -v vst2_wrap | tr '\n' ' ')
$CC -O1 -g -w -Ibuild third_party/mpc-vst-plugins/tools/host_test.c $OBJS -lm -lpthread -ldl -o build/host/host_test
$CC -O1 -g -w -std=gnu11 -Ibuild -Ithird_party/mpc-vst-plugins/wrapper $INCS test/engine_test.c $ENG -lm -lpthread -ldl -o build/host/engine_test
VG="valgrind -q --error-exitcode=9"
[ "${NOVG:-}" ] && VG=""
$VG build/host/host_test
$VG build/host/engine_test
