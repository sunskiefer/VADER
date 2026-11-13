#!/usr/bin/env python3
"""Make an Akai SDK-format plugin folder zip (one folder for every MPC, MPC OS 3.10.1+) from our per-build release zips.

    tools/sdk_zip.py OUT.zip RELEASE-armv7.zip [RELEASE-aarch64.zip]

Our releases are one zip per build, each with portable/<Manufacturer> - VST - <Name>/ holding the .so at the folder root and a
plugin.json naming it (file32 or file64). Akai's SDK ships one folder for both (docs: the SDK's plugin-folder-format.md):

    <Manufacturer> - VST - <Name>/
      plugin.json          file32: <slug>-arm32.so, file64: <slug>-arm64.so
      <slug>-arm32.so, <slug>-arm64.so
      Plugin Skins/, version.xml, Presets/, ... (as in the release)

The binaries stay at the folder root, not in the SDK's VST/ subfolder: the SDK allows any path inside the folder (plugin.json gives
it; VST/ is a convention), and our plugins find their own folder as the directory of their .so (wrapper/plugin_dir.h, the Plugin
Manager's themes), so a .so in VST/ would look for its presets, ROMs and skins in VST/.

This writes that zip, whose only top-level entry is the folder. Every other file must be the same in both builds (else the
plugin needs per-build files and stays per-build: the tool says which files and stops). mpc-plugin.json (our manifest, which the
Plugin Manager reads to recognise an installed plugin) is kept, with "arch": "universal" and each build's max_glibc under "builds".
plugin-meta.xml (the MPC OS 3.9.1 plugin-list entry, which names the .so as it was) is left out: the SDK zip is for MPC OS
3.10.1 and later; 3.9.1 installs the per-build zips. A Gen1-only or Gen2-only release gives a folder with just that binary. Modes and dates are kept. Standard library only.
A release made from an Akai SDK build (tools/sdk_release.py: binaries at distinct paths in a subfolder, VST/) keeps those paths.
Our Plugin Manager and installers keep installing the per-build zips; this is the download for copying by hand and for SDK tools.
"""
import json
import os
import sys
import zipfile

ARCH = {"armv7": ("file32", "arm32"), "aarch64": ("file64", "arm64")}
SKIP = {"plugin.json", "mpc-plugin.json", "plugin-meta.xml"}   # plugin-meta.xml: MPC OS 3.9.1 registration, naming the old .so name


def read_release(path):
    """(folder, {relative path: ZipInfo}, zip, plugin.json, mpc-plugin.json) of one release zip."""
    z = zipfile.ZipFile(path)
    pjs = [n for n in z.namelist() if n.count("/") == 3 and n.split("/")[1] == "portable" and n.endswith("/plugin.json")]
    if len(pjs) != 1:
        raise SystemExit("%s: expected one portable/<folder>/plugin.json, found %d" % (path, len(pjs)))
    pre = pjs[0][: -len("plugin.json")]
    files = {}
    for i in z.infolist():
        if i.filename.startswith(pre) and not i.is_dir():
            rel = i.filename[len(pre):]
            if rel.startswith("/") or ".." in rel.split("/"):
                raise SystemExit("unsafe name: " + i.filename)
            files[rel] = i
    man = json.loads(z.read(pre + "mpc-plugin.json")) if "mpc-plugin.json" in files else {}
    return pre.split("/")[2], files, z, json.loads(z.read(pjs[0])), man


def sdk_zip(out, zips):
    rels = [read_release(p) for p in zips]
    by_arch = {}
    for p, r in zip(zips, rels):
        pj = r[3]
        keys = [k for k in ("file32", "file64") if k in pj]
        arch = (r[4].get("arch") or {"file32": "armv7", "file64": "aarch64"}.get(keys[0] if len(keys) == 1 else "", ""))
        if arch not in ARCH or len(keys) != 1 or keys[0] != ARCH[arch][0]:
            raise SystemExit("%s: can't tell its build (plugin.json %s, manifest arch %r)" % (p, keys, r[4].get("arch")))
        if arch in by_arch:
            raise SystemExit("two %s zips" % arch)
        by_arch[arch] = r
    folders = {r[0] for r in rels}
    if len(folders) != 1:
        raise SystemExit("the zips hold different plugin folders: %s" % sorted(folders))
    folder = folders.pop()
    pjs = [{k: v for k, v in r[3].items() if k not in ("file32", "file64")} for r in rels]
    if any(x != pjs[0] for x in pjs):
        raise SystemExit("plugin.json differs between the builds (beyond file32/file64)")

    sos = {a: r[3][ARCH[a][0]] for a, r in by_arch.items()}
    slug = os.path.splitext(os.path.basename(sorted(sos.values())[0]))[0]
    common = None
    for a, r in by_arch.items():
        rest = {k: i for k, i in r[1].items() if k not in SKIP and k != sos[a]}
        if common is None:
            common = (r, rest)
            continue
        r0, rest0 = common
        diff = sorted(k for k in set(rest) | set(rest0) if k not in rest or k not in rest0 or r[2].read(r[1][k]) != r0[2].read(r0[1][k]))
        if diff:
            raise SystemExit("files differ between the builds, so this plugin can't be one folder: %s%s" % (", ".join(diff[:8]), " ..." if len(diff) > 8 else ""))

    pj = dict(pjs[0])
    pj.setdefault("format", "vst2")   # Akai's plugin.json schema requires it (SDK febd2ca, 2026-11-07); older releases lack it
    sdk_layout = len(set(sos.values())) == len(sos) and all("/" in v for v in sos.values())
    for a in by_arch:   # a release made from an SDK build (tools/sdk_release.py) keeps its own paths (VST/<slug>-arm32.so ...)
        pj[ARCH[a][0]] = sos[a] if sdk_layout else "%s-%s.so" % (slug, ARCH[a][1])   # ours: at the folder root, see the docstring
    mans = {a: r[4] for a, r in by_arch.items() if r[4]}
    man = None
    if mans:
        man = dict(next(iter(mans.values())))
        man["arch"] = "universal"
        man.pop("max_glibc", None)
        man["builds"] = {a: {"max_glibc": m.get("max_glibc", "")} for a, m in sorted(mans.items())}

    r0, rest0 = common
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zo:
        def put(rel, data, like):
            zi = zipfile.ZipInfo(folder + "/" + rel, like.date_time)
            zi.external_attr, zi.create_system, zi.compress_type = like.external_attr, like.create_system, zipfile.ZIP_DEFLATED
            zo.writestr(zi, data)
        anyinfo = r0[1]["plugin.json"]
        put("plugin.json", json.dumps(pj, indent=2) + "\n", anyinfo)
        if man is not None:
            put("mpc-plugin.json", json.dumps(man, indent=2) + "\n", anyinfo)
        for a, r in sorted(by_arch.items()):
            put(pj[ARCH[a][0]], r[2].read(r[1][sos[a]]), r[1][sos[a]])
        for k in sorted(rest0):
            put(k, r0[2].read(r0[1][k]), r0[1][k])
    return folder, sorted(by_arch)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    folder, arches = sdk_zip(sys.argv[1], sys.argv[2:])
    print("%s: %s (%s)" % (sys.argv[1], folder, " + ".join(arches)))
