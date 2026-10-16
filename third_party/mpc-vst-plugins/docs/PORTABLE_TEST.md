# Testing the portable layout on a device

The portable layout puts a plugin in one folder (`<Vendor> - VST - <Name>/`: skin, `plugin-meta.xml`, the `.so`, its data) inside a
Synths folder. It passed on a Force with Dexed on 2026-09-29 (`docs/NOTES.md`) and is now the only layout; `install.sh` / `uninstall.sh` are the
portable installers. This is the checklist to repeat for a port with data (ROMs, banks) or after changing the installer. Record results in `docs/NOTES.md` with the date.

Offline first: `python3 tools/test_catalog.py` runs the installer scripts against a copy of `MPC.settings` (under dash and awk). To imitate the
device's userland, put BusyBox applets first on the path and run the installer tests without Python:
`busybox --install -s /tmp/bb && INSTALLER_TEST_PATH=/tmp/bb python3 -m unittest tools.test_catalog.InstallerTest`
(passed with BusyBox 1.36.1, 2026-09-29).

The device is shared with a live setup: save projects, and ask before anything that restarts MPC. Every step that changes
`MPC.settings` makes a backup next to it (`MPC.settings.bak-<plugin>-<date>`).

## 0. Prepare (about 5 minutes)
1. Copy the settings file to your computer as an extra backup:
   `scp root@<device-ip>:/media/az01-internal/Settings/*/MPC.settings ~/MPC.settings.backup`
2. Note what is installed now: `ssh root@<device-ip> "ls /sdcard/vst /sdcard/Synths"`.
3. Check whether the storage allows running code (a "noexec" mount would stop plugins loading):
   `ssh root@<device-ip> "mount | grep -E 'sdcard|/media'"`. Write down the lines; `noexec` in the options is a fail for that path.

## 1. Build a test zip (about 10 minutes)
Use a plugin with no big data files, ideally one already installed the old way so the upgrade path is tested too.
```
git checkout main                                 # mpc-vst-plugins
<your port>/build.sh                               # as usual
tools/release.py --so build/x.so --skin "build/skin/<vendor> - VST - <Name>" --entry build/pluginlist-entry.xml \
    --version <X.Y.Z> --repo owner/name --license <SPDX> --user-data <folder for user files, if any> -o dist
tools/catalog_check.py dist/<zip> --catalog        # must print OK
unzip -l dist/<zip> | grep -E 'portable/|install.sh|uninstall.sh'
```

## 2. Install (about 5 minutes)
```
unzip dist/<zip> && scp -r <Name>-<version> root@<device-ip>:/tmp/
ssh root@<device-ip>
sh /tmp/<Name>-<version>/install.sh          # answer y; MPC stops and restarts
```
Expect: "Installing...", possibly "removed the old copy /sdcard/vst/..." if it was installed the old way, then "Done. Settings backup: ...".
Then on the device shell:
```
ls -la "/sdcard/Synths/<Vendor> - VST - <Name>/"       # the .so, plugin-meta.xml, version.xml, Plugin Skins
grep -c 'uid="<uid>"' /media/az01-internal/Settings/*/MPC.settings    # must print 1
grep 'file=.*<so name>' /media/az01-internal/Settings/*/MPC.settings   # file= is the /sdcard/Synths/... path
```

## 3. Use it in MPC (10 minutes)
- [ ] The plugin is in the list (only once, not twice) and can be added to a track.
- [ ] It sounds/works, the screen skin shows, Q-Links move parameters.
- [ ] Save the project, reload it: the plugin and its settings come back.
- [ ] Anything that reads data next to the plugin (banks, ROMs, kits) finds it.

## 4. Upgrade and uninstall (10 minutes)
- [ ] Put a file in the user-data folder (`<folder>/roms/test.rom`), run `install.sh` again: still one entry, plugin still works, your file is still there.
- [ ] `sh /tmp/<Name>-<version>/uninstall.sh`: the plugin is gone from the list, a project that used it opens without it, your file is kept.

## 5. Optional: another location
Only if `mount` showed no `noexec` for it: `install.sh -t /media/<id>/Synths`. If the plugin loads from there, removable-media installs work.

## If something goes wrong
Stop MPC (`systemctl stop acvs`), copy the newest `MPC.settings.bak-*` over `MPC.settings` (or your `~/MPC.settings.backup`), delete the plugin folder,
and start MPC (`systemctl start acvs`).

## What to send back
Pass or fail for each checkbox, the `mount` lines from step 0, the installer's output, and the plugin's `<PLUGIN .../>` line from `MPC.settings`.

## If it passes
Record it in `docs/NOTES.md`, and add the device and firmware to the port's `tested.json`.
