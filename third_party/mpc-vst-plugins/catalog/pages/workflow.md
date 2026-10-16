---
title: Release workflow
nav: Workflow
order: 30
summary: The path from a working build to a tested, checksummed release that people can install and the catalog can list.
---

Develop on your PC first, test on the device once, and let a workflow package the zip that people will actually install. The order matters because the device is somebody's live setup.

## The loop
1. **Build** with `tools/build_port.sh`. It produces the `.so`, the skin and the plugin-list entry.
2. **Host test** with `tools/test_port.sh`. It must print `PASSED`.
3. **Preview the skin** with `tools/studio.py preview`, and look at every page.
4. **Measure CPU** on a device: `tools/bench.sh build/x.so <device-ip> -j | tee build/bench.txt`. It must **PASS**, or **WARN** with a note in your release notes. A plugin gets about 2.9 ms per audio block and shares it with everything else in the project.
5. **Package** the zip (below).
6. **Smoke test the zip** on a device with its own `install.sh`: load the plugin on a track, play it, turn every page and Q-Link, save and reload a project, then run `uninstall.sh`.
7. **Publish** the release on GitHub.

## Package the zip
```
tools/release.py --so build/x.so --skin "build/skin/<vendor> - VST - <Name>" \
    --entry build/pluginlist-entry.xml --version 1.2.0 --bench build/bench.txt \
    --about "One line about the plugin." \
    --repo you/your-plugin-repo --license MIT -o dist
tools/catalog_check.py dist/Name-1.2.0-mpc-armv7.zip --catalog
```

You get `Name-1.2.0-mpc-armv7.zip`: the plugin, its skin, `install.sh`, `uninstall.sh`, a generated `INSTALL.md`, checksums and a manifest (`mpc-plugin.json`) the catalog reads. `catalog_check.py` runs the same checks the catalog does, so a zip that passes here will be listed.

## Or let GitHub build it
Your repo can call this repo's reusable workflow, which builds, tests, packages and attaches the zip to a **draft** release:

```
jobs:
  vst:
    uses: sd88me/mpc-vst-plugins/.github/workflows/vst-release.yml@<commit>
    permissions: { contents: write }
    with:
      tag: my-plugin-vst-v${{ inputs.version }}
      version: ${{ inputs.version }}
      tools_ref: <commit>
      vst_dir: vst
      build: vst/build.sh
      license: MIT
      about: One line about the plugin.
```

Pin the same commit in both places. The steps that need a device, CPU and the smoke test, stay with you: run them on the draft's own zip, then publish it, so the zip you tested is the zip people get. Publishing the draft creates the tag.

## Versions
Use `X.Y.Z`.

| Bump | When |
|---|---|
| Z | Fixes |
| Y | New parameters or pages |
| X | Parameter positions change (this breaks saved projects, so avoid it) |

Keep the plugin `uid` and the `.so` name fixed forever. Projects find the plugin by uid, and the installer replaces the entry with the same uid or file name.

## Say where you tested
Add a `tested.json` at the root of your repo's default branch, and the catalog shows it on the release:

```
[ { "version": "1.2.0", "device": "MPC Live II", "firmware": "3.6.0", "date": "2026-09-29" } ]
```

## After the release
The catalog finds new releases by itself every night. To list a plugin for the first time, follow [Add your plugin](add.html). If a release fails the checks, it is left out, the previous version stays listed, and an issue is opened on the [catalog repository](https://github.com/sd88me/mpc-vst-plugins/issues) saying why.
