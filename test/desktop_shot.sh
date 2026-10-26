#!/usr/bin/env bash
# Screenshot of the desktop plugin's window under a virtual display (Mesa software GL).
# Usage: [XD="mousemove 600 130 click 1"] test/desktop_shot.sh out.png [page 0-6] [width height]   (XD: extra xdotool commands)
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/test/shot.png}"; PAGE="${2:-0}"; W="${3:-1280}"; H="${4:-668}"
SO="${SO:-build/desktop/bin/VADER-vst2.so}"
mkdir -p build/test
g++ -std=c++17 -O1 -o build/test/desktop_gui test/desktop_gui.cpp -ldl -lX11
export HOME="$(mktemp -d)" LIBGL_ALWAYS_SOFTWARE=1
xvfb-run -a -s "-screen 0 1600x1000x24 +extension GLX" bash -c '
  build/test/desktop_gui "'"$SO"'" 14 '"$W"' '"$H"' > build/test/gui.log 2>&1 &
  PID=$!
  sleep 2.5
  if [ "'"$PAGE"'" != "0" ]; then
    X=$(( ('"$PAGE"' * '"$W"' / 7) + '"$W"' / 14 ))
    xdotool mousemove $X 15 click 1
    sleep 1.2
  fi
  if [ -n "${XD:-}" ]; then sleep 0.5; eval "xdotool $XD"; sleep 0.8; fi
  sleep 1
  import -window root build/test/full.png
  wait $PID || true
'
convert build/test/full.png -crop "${W}x${H}+0+0" +repage "$OUT"
echo "$OUT"
