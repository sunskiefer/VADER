# Optional: button remap (hwremap)

Not part of any plugin release. `hwremap-patch.sh` is a standalone script you run on the device yourself, if you want it. The installer app lists it in its read-only "Advanced: device patches" step, next to the 16-pad drum layout and the drive exec patch.

> **The installer is untested on a device.** The shim itself was tried by its author on an MPC Live (Hakai, MPC 3.9.1) and a Force (stock firmware 3.9.0). This script, which installs that shim the same way the other patches here install themselves (`status`, a typed word, a backup, `uninstall`), has only been run offline. Save the project before you run it: it stops and starts MPC.

## What it does
The devices have no setting for "this button opens that screen". `hwremap.so` is an `LD_PRELOAD` library that rewrites the controller's button stream, so a press can become other buttons, a pad, or a touchscreen tap. To MPC it looks like you did it yourself. Holding **Menu** and tapping a pad opens the icon in that cell of the Mode Menu, which is how a button opens a screen.

The library and the two example maps are inside the one script. Full behaviour, the config language and the button notes are in the upstream README (vendored notes in `VENDORED.md`): https://github.com/mmiroshnikov/akai_standalone_remap

| Device | What the script changes | Default map |
|---|---|---|
| Hakai (a launcher `/usr/bin/az01-launch-MPC`) | `/usr/lib/hwremap.so`, and that launcher's `LD_PRELOAD` lines. The root filesystem is remounted writable for a moment. | Pad Bank A–D and `+` / `−` open Mode Menu cells. Shift keeps the original button. |
| systemd (`acvs` or `inmusic-mpc`; a stock Force) | `/data/hwremap/hwremap.so`, and a drop-in that adds it to the service's `LD_PRELOAD`, keeping whatever was already there. | Mixer twice = Master, Menu twice = a Main Mode tap, Knobs short / long / double as in `configs/force.conf`. |

The map is written to `/sdcard/hwremap.conf` only when that file is not already there. Editing it applies on the next button press; MPC does not restart for a config change. `--layout mpc-live` or `--layout force` picks the map on a device that is not the one it was written for.

## Use
```
scp tools/mpc_patch/hwremap/hwremap-patch.sh root@<device-ip>:/tmp/
ssh root@<device-ip>
sh /tmp/hwremap-patch.sh status                 # changes nothing
sh /tmp/hwremap-patch.sh install                # asks you to type PATCH; MPC restarts
sh /tmp/hwremap-patch.sh uninstall              # asks you to type REMOVE; MPC restarts
```
`install --confirmed` skips the typed question (the installer app will use that later; it is not wired up yet). A backup of the launcher or the service environment goes to `/data/mpc-vst-plugins/backups`.

To turn the remap off without uninstalling, empty `/sdcard/hwremap.conf`. With no rules the shim passes every button through.

## Uninstall by hand
While the patch is installed it keeps the original launcher at `/data/hwremap/az01-launch-MPC.orig`. If the script is gone: stop MPC, remount `/` writable, copy that file back over `/usr/bin/az01-launch-MPC`, delete `/usr/lib/hwremap.so`, remount read-only, start MPC.

Force: remove `/etc/systemd/system/acvs.service.d/hwremap.conf` (or the `inmusic-mpc` one), `systemctl daemon-reload`, restart MPC, delete `/data/hwremap/hwremap.so`.

## Credit
`hwremap.c` and the two configs are https://github.com/mmiroshnikov/akai_standalone_remap at commit `9d2aa57a570b0c4788f88b04ca242bb82176c380`, MIT, unchanged. The device build here uses `arm32v7/gcc:11-bullseye` (glibc 2.31; the library needs glibc 2.17). See `VENDORED.md`.

## Tests
`python3 tools/test_hwremap_patch.py` (a scratch root, shims for `systemctl` and `pidof`: both install styles, the typed words, rollback, a config you edited, a preload that is not a plain path). `./build.sh test` runs the library's own host tests under ASan. Neither has been run on a device.
