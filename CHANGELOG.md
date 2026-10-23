# Changelog

## 0.4.4 (2026-10-23)

Not yet tested on a device.

- **The orbit line is drawn as many small dots** and always closes on itself: its loops are counted as a whole number
  per orbit, so it has no loose ends. Sharp points in it are the orbiter really turning back.

## 0.4.3 (2026-10-23)

Not yet tested on a device.

- **Two sharp balls**: the path point and the orbiter are each one sharp round dot with no glow (the orbiter 32 px, the
  path point 22 px), on layers of their own over the screen's glass. Each ball's colour shows its size, in 6 steps:
  the path point's follows ORBIT SIZE (yellow, amber, orange, red, rose, pink), the orbiter's SUBORBIT SIZE (blue, sky,
  cyan, aqua, green, lime). The orbiter passes behind the path point on the upper half of its circle.
- The balls always stay whole inside the screen: near the top or bottom edge they stop at the edge instead of
  reaching over the panel.
- **An unbroken orbit line**: every piece of the screen's lines now runs from a fixed place on a tile's edge to
  another, and the next tile starts at that same place, so the orbit line, the light trail and drawn shapes join up
  with no breaks or steps. The orbit line is faint and thin, and has a second layer of its own where it crosses itself
  or the shape, so crossings don't leave gaps. It stays still while nothing changes.
- Screen reports are budgeted (at most 48 tiles a poll, the moving ones first), so a big redraw no longer comes as one
  burst.
- Wrapper: mpc-vst-plugins #486 (no UpdateDisplay for a plain host write, so Q-Links move parameters on the automation
  screen), #487 (at most one UpdateDisplay per 50 ms), #488 (params.lock: parameters keep their index; the new ball
  parameters are appended).

## 0.4.2 (2026-10-17)

- **The orbit line and the light trail**: the screen draws the orbiter's whole path over one orbit (with its suborbit
  loops) as a faint thin line, and the part the orbiter has just passed lights up: bright and thick right behind it,
  then a softer tail, then the faint line. It is worked out from the orbit and suborbit speeds, so it follows the real
  curve and turns, grows, spins and tilts with the shape. It has its own layer, so it no longer cuts the shape's line.
- **ROTATE, 3D SPIN, 3D TILT and ORBIT ENV on the MAIN page** (the SHAPE section). With ALT on, that section shows
  VIBRATO / TREMOLO, and the ORBIT section DETUNE, GLIDE, VELOCITY and VOLUME. New Q-Link set MAIN 3.

## 0.4.1 (2026-10-17)

- PNG and JPG outlines are smoothed (the traced pixels made a staircase), so a loaded picture draws as a clean line.
- The page previews show your shape as MPC shows it once it has read the pictures VADER writes.

## 0.4.0 (2026-10-17)

Not yet tested on a device.

- **IMAGE page**: a picture plays the sound. A PNG or JPG loaded as the shape is also the IMAGE. Four MODs, each FROM a
  source (BRIGHT on a spectrogram colour scale, HUE, COLOUR, DEPTH as a landscape slope, TURN of the outline, HEADING
  of the outline) TO a destination (CUTOFF, RESO, OCTAVE in whole octaves, PITCH in semitones, GEN 1, GEN 2, BLEND,
  LEVEL), with AMOUNT (-100..+100), SMOOTH and a live NOW meter. Per timbre; saved with projects and MY PRESETS.
- **Finer screen pictures**: 12 size steps for the built-in shapes and fine turn steps over each shape's symmetry (a
  STAR every 12 degrees); CUBE and ICOSA ready-drawn while they spin. Drawn as solid glowing lines again.
- **25 screen updates a second** (was 10): the orbiter and the trail move more smoothly.
- **Your own shape on the screen (experiment)**: loading a 2D shape writes its pictures into the plugin's skin folder;
  close and reopen the VADER window to see whether MPC shows them.
- Fixed: PNG and JPG outlines were traced wrongly (a picture could load as a tiny square); they now follow the shape.
- Fixed: the fader strip was taller than MPC allows.

## 0.3.2 (2026-10-17)

- **No more clicks when you turn ORBIT SIZE**: ORBIT SIZE, SUBORBIT SIZE and ROTATE now glide to their new value
  (about 40 ms) instead of jumping in steps, in every patch.
- The cloud of dots is gone; the suborbit's trail line is back (bright cyan line pieces behind the orbiter).
- The orbiter stays a round dot.

## 0.3.1 (2026-10-16)

- The orbiter's trail line is gone (it was drawn in coarse pieces and looked ragged); the trail is the fading cloud of
  dots behind the orbiter.
- The orbiter is one round dot that moves in 2 px steps and is never cut at a tile's edge.

## 0.3.0 (2026-10-16)

Not yet tested on a device.

- **Stipple screen**: a layer of fine dots under the lines. They cloud around the shape, follow the orbiter as a
  fading trail and gather at each corner as loud as that corner is heard. The shape and the trail are drawn as chains
  of dots. The dot layer is 16 x 8 tiles whose density blends smoothly into its neighbours (no blocks).
- **Black boxes, white text**: every field, list and button is black; names and captions are white.
- **Bigger text**: no small help text on the pages; labels never drop below 17 px (short names on MAIN:
  GEN 1, GEN 2, BLEND, CUTOFF, RESO, DLY TIME, FEEDBACK, CH SPEED, CH DEPTH, CH COLOUR).
- Lighter glass over the screen, for more contrast.

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
