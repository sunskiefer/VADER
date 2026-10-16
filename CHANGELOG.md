# Changelog

## 0.2.1 (2026-10-16)

- Packed under **L'Cronx**: the plugin folder is now `L'Cronx - VST - VADER` and MPC lists the maker as L'Cronx.
  If you installed 0.2.0, run its `uninstall.sh` first (copy out `User Presets` if you saved any), then install 0.2.1.
- The installer handles a ' in the folder name.

## 0.2.0 (2026-10-16)

Not yet tested on a device.

- **The screen, sharper**: the built-in 2D shapes are ready-drawn glowing pictures (following ORBIT SIZE and ROTATE in
  steps). The tiles are finer (32 × 16) and hold straight pieces at 24 angles, so lines stay straight; the orbiter and
  the path point move in small steps. A glass layer (scanlines, darker edges) on top. The square is drawn at its true
  proportions.
- **Black** background behind everything.
- **MY PRESETS** on the ORBIT page: 999 slots of your own (SLOT, SAVE, LOAD), files in `User Presets/`.
- **Shapes from the card**: a `VADER Shapes` folder on the SD card or a USB drive is read too.
- Fixed: notes kept sounding forever after changing preset. A preset change now stops every note, and a note-off always
  finds its note.
- Fixed: loading a shape file could fail with "can't open the file".

## 0.1.0 (2026-10-16)

First release: not yet tested on a device.

- Four corners, each two overtone generators (32 additive spectra, band-limited), a blend (MIX, AM, PM, AM+PM, PD, RING,
  FOLD) and a filter (LP12, LP24, BP, HP, LADDER) after the blend or on either generator.
- The orbiter: 15 built-in shapes (two in 3D), a USER shape from SVG, OBJ, PNG or JPG files, the suborbit, rotation,
  3D spin and tilt, tempo sync, retrigger, the envelope on the orbit's size. SHAPE SOUND plays the shape as audio.
- Two timbres, layered or split; 8 or 16 voices; unison up to 8, glide, vibrato, tremolo.
- Arpeggiator with a 16-step pattern, synced to MPC.
- Drive, chorus, ping-pong delay (TAP), reverb, master resonance.
- 16 presets in MPC's PRESET menu.
- Five pages (MAIN, CORNERS, ORBIT, ARP, FX) with line controls, a strip showing the last control touched, and a live
  line screen: the shape, the orbiter's trail and the envelope.
