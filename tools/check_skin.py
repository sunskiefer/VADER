#!/usr/bin/env python3
"""mpc-vst-plugins' skin_check.py for VADER: every finding, except a popup list covering the page while it is open
(the lists are drawn last, over the page, and close on a pick, as the framework's own popups do).

    python3 tools/check_skin.py "<plugin folder>"     (exit 1 on any other finding)"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "third_party", "mpc-vst-plugins", "tools"))
import skin_check  # noqa: E402


def main(folder):
    params = json.load(open(os.path.join(ROOT, "params.json")))["params"]
    popups = {p["popup_of"] for p in params if p.get("popup_of")}
    opts = {k: next(p for p in params if p["key"] == k).get("options", []) for k in popups}
    listed = {"%s %s" % (k, o) for k in popups for o in opts[k]} | {"%s field" % k for k in popups}
    out = []
    for f in skin_check.check(folder):
        if " TOUCH " in f:
            body = f.split(" TOUCH ", 1)[1].rsplit(" overlap", 1)[0]
            if any(body.startswith(n + " and ") or body.endswith(" and " + n) for n in listed):
                continue
        out.append(f)
    for f in out:
        print(f)
    print("check_skin: %d finding(s)" % len(out))
    return 1 if out else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
