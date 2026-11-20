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
$CC -O1 -g -w -Ibuild third_party/mpc-vst-plugins/tools/host_test.c $OBJS -lm -lpthread -ldl -lstdc++ -o build/host/host_test
$CC -O1 -g -w -std=gnu11 -Ibuild -Ithird_party/mpc-vst-plugins/wrapper $INCS test/engine_test.c $ENG -lm -lpthread -ldl -lstdc++ -o build/host/engine_test
$CC -O1 -g -w -std=gnu11 -Ibuild -Ithird_party/mpc-vst-plugins/wrapper $INCS test/corner_env_test.c $OBJS -lm -lpthread -ldl -lstdc++ -o build/host/corner_env_test
VG="valgrind -q --error-exitcode=9"
export VADER_SLOW=1   # the engine test skips its timing check under valgrind
[ "${NOVG:-}" ] && VG="" && unset VADER_SLOW
$VG build/host/host_test
$VG build/host/engine_test
build/host/corner_env_test
# 0.10.0: the host's load-time nudge of parameter 0 must not touch the corners
$CC -O1 -g -w -Ibuild test/nudge_test.c $OBJS -lm -lpthread -ldl -lstdc++ -o build/host/nudge_test
$VG build/host/nudge_test
# 0.10.7: MY PRESETS come back with the file they were saved with (a picture, a shape file, or none)
$CC -O1 -g -w -std=gnu11 -Ibuild -Ithird_party/mpc-vst-plugins/wrapper $INCS test/preset_file_test.c $ENG -lm -lpthread -ldl -lstdc++ -o build/host/preset_file_test
$VG build/host/preset_file_test
$CC -O1 -g -w -std=gnu11 -Ibuild -Ithird_party/mpc-vst-plugins/wrapper $INCS test/skin_dump.c $ENG -lm -lpthread -ldl -lstdc++ -o build/host/skin_dump
python3 test/web_test.py
python3 test/mirror_test.py
