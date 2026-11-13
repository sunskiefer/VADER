#!/usr/bin/env python3
"""VADER in Akai's plugin folder format (MPC OS 3.10.1 and later, 32-bit and 64-bit MPCs).

    python3 tools/sdk_folder.py                 -> build/sdk/L'Cronx - VST - VADER/
    python3 tools/sdk_folder.py --zip dist      -> also dist/VADER-<version>-mpc-sdk.zip

The folder (Akai's MPC Plugin SDK, docs/plugin-folder-format.md):
    plugin.json                 what the plugin is and where its two binaries are
    version.xml
    VST/vader-arm32.so          MPC X, Live, Live II, One, Key 61/37, Force
    VST/vader-arm64.so          MPC Live III, XL and later
    Plugin Skins/               the pages
    Content/Shapes, Content/Samples   VADER's own files (the wrapper and the engine look in Content first)
It is made from what build.sh built: build/skin, build/arm/vader.so, build/arm64/vader.so, shapes/, Samples/.
Standard library only."""
import json
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME, VENDOR = "VADER", "L'Cronx"
FOLDER = "%s - VST - %s" % (VENDOR, NAME)      # no character of it is one MPC strips (" # @ , ; : < > * ^ | ? \\ /)
ABOUT = "VADER by L'Cronx: an orbit-based vector synth for MPC OS, with shapes and pictures of your own"

INSTALL = """VADER {ver} by L'Cronx, for MPC OS 3.10.1 or later
====================================================

This zip holds one folder, "{folder}". It works on every standalone MPC and on the Force:
32-bit ones (MPC X, Live, Live II, One, Key 61/37, Force) and 64-bit ones (MPC Live III, XL).

HOW TO INSTALL
1. On the MPC: Preferences, Enhancements (Beta): turn on "Enable Enhancements" and "External Plugin Support".
2. On a USB stick, an SD card or the MPC's internal drive, make a folder named exactly  VST2 Plugins  at the top.
3. Copy the whole "{folder}" folder into it:
       <drive>/VST2 Plugins/{folder}/
4. VADER shows in the plugin browser under External Plugins (after a restart when it is on the internal drive).

TO UNINSTALL
Delete the "{folder}" folder from "VST2 Plugins".

YOUR OWN FILES
Shapes and pictures go in the folder's Content/Shapes, WAV samples in Content/Samples; or in a "VADER Shapes" /
"VADER Samples" folder at the top of any drive.

A modded MPC or Force on older firmware uses the other zip of this release (VADER-{ver}-mpc-armv7.zip, with install.sh).

License: PolyForm Noncommercial 1.0.0 (free to use and share, not for sale). Source: https://github.com/sunskiefer/VADER
"""


def version():
    return re.search(r'VADER_VERSION_STR "([^"]+)"', open(os.path.join(ROOT, "src", "version.h")).read()).group(1)


def main(argv):
    ver = version()
    vst = json.load(open(os.path.join(ROOT, "vst.json")))
    out = os.path.join(ROOT, "build", "sdk", FOLDER)
    shutil.rmtree(os.path.join(ROOT, "build", "sdk"), ignore_errors=True)
    os.makedirs(os.path.join(out, "VST"))
    shutil.copy(os.path.join(ROOT, "build", "arm", "vader.so"), os.path.join(out, "VST", "vader-arm32.so"))
    shutil.copy(os.path.join(ROOT, "build", "arm64", "vader.so"), os.path.join(out, "VST", "vader-arm64.so"))
    shutil.copytree(os.path.join(ROOT, "build", "skin", FOLDER, "Plugin Skins"), os.path.join(out, "Plugin Skins"))
    shutil.copytree(os.path.join(ROOT, "shapes"), os.path.join(out, "Content", "Shapes"))
    shutil.copytree(os.path.join(ROOT, "Samples"), os.path.join(out, "Content", "Samples"))
    manifest = {
        "name": NAME, "manufacturer": VENDOR, "version": ver, "format": "vst2", "kind": "instrument",
        "uid": vst["uid"], "category": "Synth", "numInputs": 0, "numOutputs": 2,
        "file32": "VST/vader-arm32.so", "file64": "VST/vader-arm64.so",
        "description": ABOUT, "author": VENDOR, "url": "https://github.com/sunskiefer/VADER",
        "license": "PolyForm-Noncommercial-1.0.0",
    }
    assert vst["name"] == NAME and vst["vendor"] == VENDOR
    with open(os.path.join(out, "plugin.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    parts = (ver.split(".") + ["0"] * 4)[:4]
    with open(os.path.join(out, "version.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<plugincontent version="1.0">\n'
                '    <version>%s</version>\n    <uiversion>0.1.0.0</uiversion>\n</plugincontent>\n' % ".".join(parts))
    print("-> %s" % out)
    if "--zip" in argv:
        dist = argv[argv.index("--zip") + 1]
        os.makedirs(dist, exist_ok=True)
        zp = os.path.join(dist, "VADER-%s-mpc-sdk.zip" % ver)
        with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
            for d, _, files in sorted(os.walk(out)):
                for fn in sorted(files):
                    p = os.path.join(d, fn)
                    z.write(p, os.path.join(FOLDER, os.path.relpath(p, out)))
            z.writestr("INSTALL.txt", INSTALL.format(ver=ver, folder=FOLDER))
            z.write(os.path.join(ROOT, "LICENSE"), "LICENSE")
        print("-> %s (%.1f MB)" % (zp, os.path.getsize(zp) / 1e6))


if __name__ == "__main__":
    main(sys.argv[1:])
