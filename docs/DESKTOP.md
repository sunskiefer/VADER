# VADER for Windows and Linux (VST3 + VST2)

The same VADER engine as on the Force, as a desktop plugin, with the orbit screen (glowing shape, orbit line, light
trail, the two balls, corner bars) drawn in the plugin's own window.

## Install

| | VST3 | VST2 |
|---|---|---|
| **Windows** (64-bit) | copy the folder `VADER.vst3` to `C:\Program Files\Common Files\VST3\` | copy `VADER-vst2.dll` to your VST2 folder (for example `C:\Program Files\VSTPlugins\`) |
| **Linux** (x86_64) | copy the folder `VADER.vst3` to `~/.vst3/` | copy `VADER-vst2.so` to `~/.vst/` |

Then rescan plugins in your host. VADER is an instrument (no audio input, stereo out, MIDI in).

## Shapes folder

- Windows: `%APPDATA%\VADER\Shapes`
- Linux: `~/.local/share/VADER/Shapes` (`$XDG_DATA_HOME` is honoured)

The example shapes are put there the first time the plugin runs (once; the file `.examples_installed` marks it).
Add your own pictures (PNG, JPG), SVG or OBJ files and pick them with the SHAPE field.

## The window

Resizable (it starts at 1280 x 668). The tabs on top are the pages of the Force's screen: MAIN, CORNERS, ORBIT,
IMAGE, ARP, FX, and SCREEN, which is the orbit view full size. Drag a control to change it, use the mouse wheel for
fine steps, click twice quickly to reset it. Lists (preset, shape, modes) open as a popup; Esc closes it. The page
layouts and the parameters come from the same sources as the Force build, so they never drift apart. See the
[user guide](USER_GUIDE.md) for what every control does.

## In the host

- 147 automatable parameters (the global settings first, then the sound, then five momentary buttons).
- Presets: the host's program list holds the 16 factory presets; the full sound is saved with the project.
- Any host sample rate works: the engine runs at 44.1 kHz and the plugin resamples (Kaiser windowed sinc, error
  better than -94 dB on the tones tested) and reports its latency. At exactly 44.1 kHz nothing is resampled.
- MIDI timing is sample-accurate, and the arpeggiator follows the host's tempo and position.
- CPU: about 5 to 6 % of one core with 10 voices.

## Build

```
desktop/build.sh linux        # build/desktop/bin/        needs cmake, g++, python3, X11 + OpenGL dev packages
desktop/build.sh windows      # build/desktop-win/bin/    cross-compile, needs g++-mingw-w64-x86-64-posix
desktop/build.sh package 0.7.0   # dist/*.zip
test/desktop_test.sh          # loads the VST2 in a small host and plays it
```

No Steinberg SDK is needed: DPF provides the VST2 and VST3 interfaces from clean-room headers.

## What was tested

- Linux: Steinberg's VST3 validator (47 of 47 checks), the VST2 host test, screenshots of every page under Xvfb.
- Windows: cross-compiled; the VST2 DLL host test and screenshots of every page ran under Wine. **Not yet tried on
  real Windows and not in a real DAW**, and the VST3 on Windows was not run. Please report what you find.
- `.github/workflows/desktop.yml` builds both; it has not run yet.

## Limits

- 64-bit x86 only. No macOS build.
- The host sees one program (DPF's VST2 limit); use the PRESET parameter or the PRESET field in the window.
- Loading a shape through a file dialog is not in the window yet: put files in the Shapes folder.
- The companion web screen and the QR code are Force-only; here the window is the screen.

Third-party code: see [THIRD_PARTY.md](../desktop/THIRD_PARTY.md). VST is a trademark of Steinberg Media Technologies GmbH.
