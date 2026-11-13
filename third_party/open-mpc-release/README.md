# open-mpc release tools (vendored)

`release.py`, `catalog_check.py`, `sdk_zip.py` and what they import, from
[open-mpc/mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins) `tools/` at commit
`52c1fb072966d7de77e97dd9eec0544548b042dc` (2026-11-13), MIT (see LICENSE). They make VADER's release zips as the
catalog reads them: `-mpc-armv7.zip` (Gen1 and Force), `-mpc-aarch64.zip` (Gen2) and `-mpc-sdk.zip` (Akai's one-folder
format, MPC OS 3.10.1).

One change, in `release.py`: the plugin's name and skin folder are shell-escaped when they go into `install.sh` and
`uninstall.sh`, because `L'Cronx` has an apostrophe (without it `sh -n install.sh` fails). `.github/workflows/build.yml`
runs `sh -n` on both scripts of both zips.

The skin and wrapper tools VADER builds with are the older copy in `third_party/mpc-vst-plugins`.
