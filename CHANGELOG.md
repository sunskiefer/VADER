# Changelog

## Unreleased: 0.8.0 (Force and desktop, one release)

Built for the Force (ARM) and the desktop plugins from the same sources. **Not yet tested on a device.**

- **MODE page** (27 modes, three layers of eight knobs each, named live): PATH, CHASE, RECAMAN, METEOR RAIN, KEPLER,
  ROSETTE, FIGURE 8, LORENZ, ROSSLER, THOMAS, AIZAWA, PENDULUM, BOUNCE, RIPPLE, MAGNET, BOIDS, PARTICLE WAVE, OPTIMIZE,
  TRANSFER ORBIT, GRAVITY WELLS, SERPENTINE DRAGON, PARAMECIA, MEDUSA, FLATWORMS, ETHEREAL RAY, CLUMP SWARM, THUNDER.
- **35 maths shapes** beside the old ones (N-gon, butterfly, superformula, Lissajous, rose, limacon, cardioid,
  spirograph, spirals, torus knot, Mobius, harmonograph, pi Lissajous, epicycles, pulse, ADSR, polyhedra, Hilbert, Koch,
  Fourier, squeeze, de Broglie, Cotes, Poinsot, atomic, mandala, stacked spirograph, times table, toy gears), each
  with four shape numbers.
- **VOICES page**: each corner can be VADER's own generators or one of 14 voices from LXRXXL ZYNTH (the ZYNTH voices
  and tables carry their own licences, see third_party and src/zynth).
- **SAMPLES and WINDOW pages**: one WAV per corner (any rate 8-192 kHz, 8/16/24/32-bit or float, up to 30 s), played
  one-shot or looped over 1-4 bars of the tempo, rooted on C2; start, end and length controls for the selected corner.
  54 example samples ("vader 1" to "vader 54") and six sample presets.
- **PLAY page**: SELF PLAY (the orbiter plays notes from a scale), DRONE chords, POLY/DUAL/MONO, RANDOMIZE with UNDO,
  VOICE LIMIT and a CPU readout.
- **TRAVEL page**: speed LFO and curves, stop at corners, part of the path, soft turn, stretch/skew/morph/centre,
  Warp pad with reset, Focus (steer by hand), aftertouch to suborbit, MIDI CC 20/21 centre point, sound balls and
  kaleidoscope copies, suborbit direction/shape.
- **ARP**: octave per step, octave walk, humanize, chain. **FX**: the delay is now the ZYNTH delay.
- **Per corner**: GEN 2 ratio (musical or prime) and a limiter.
- **Screens**: show the real orbiter, extra balls, kaleidoscope copies and the path start/end markers.
- Desktop: the left/right arrow keys turn the pages.
- Not in this release: the freehand drawing (dropped), the modulator plugin (a later version). See docs/USER_GUIDE.md.

## Unreleased: desktop plugins (planned 0.7.0)

Desktop only; the Force plugin is unchanged. Tested on Linux, and on Windows under Wine only (not on real Windows).

- **VADER as a VST3 and a VST2 plugin for Windows and Linux.** The same engine, with the orbit screen (shape glow,
  orbit line, light trail, balls, corner bars) drawn in the plugin's own resizable window, and all six pages of the
  Force's panel plus a full-size SCREEN page. 147 automatable parameters, 16 presets, sample-accurate MIDI, the
  arpeggiator on the host's tempo, any host sample rate (the engine stays at 44.1 kHz behind a high-quality
  resampler, with its latency reported). Shapes folder: `%APPDATA%\VADER\Shapes` or `~/.local/share/VADER/Shapes`.
  See [docs/DESKTOP.md](docs/DESKTOP.md). Built with `desktop/build.sh`; no Steinberg SDK needed.
- The engine publishes the corner weights and the IM meters in its live view (the Force build is unaffected).

## 0.6.1 (2026-10-25)

Tested on an Akai Force.

- **The MAIN page's middle is a switch**: it starts on the VADER logo; tap it and it swaps to the QR code of the
  companion screen, tap again and it is the logo. The last choice is remembered (it is not part of a preset). The QR
  code is now neon blue dots on black, with blue corner markers and a bolder VADER mark, so it blends with the panel.
- The MOTION button is gone from MAIN (the companion screen's motion stays on).
- The companion screen draws the shape bigger: the square now fills more of a long, short phone screen.

## 0.6.0 (2026-10-24)

Not yet tested on a device.

- **The companion screen.** VADER's screen is now drawn on your phone, tablet or laptop: open the address shown on
  the ORBIT page (or scan the QR code on MAIN with the phone's camera) on the same Wi-Fi, and the page draws the shape,
  the orbit line, the orbiter with its light trail and the two balls, smoothly, with a FULLSCREEN button; on a phone it
  draws in landscape whichever way the phone is held. "Add to Home Screen" gives it an icon that opens it full screen
  like an app. Apple devices can also type `vader.local`. Several devices can watch at once. The Force sends 30 small
  messages a second and draws nothing itself: the sound gets the whole machine.
- **LOAD SHAPE on the page**: send a picture (PNG, JPG), an SVG or an OBJ from the phone to the Force; it lands in the
  Shapes folder and plays at once.
- **The MAIN page, redone**: three columns of bigger controls (bigger names, values and lines), and in the middle,
  instead of the screen, the QR code of the companion screen around the VADER mark, in the panel's colours. The tile
  screen (the 3000 pieces of 0.4.x) and its 288 MB of pictures are gone: the plugin loads faster and MPC redraws
  nothing while you play. MOTION now only switches the companion screen's motion.
- The live screen of 0.5.x (the overlay plane) stays in the code as an experiment, off unless a file `overlay.on` is in
  the plugin's folder.
- Fixed: a hang at start when the parameter list grew past the lookup table.

## 0.5.3 (2026-10-23)

- No tearing on the live screen: each frame is copied into the plane's buffer right after the display's vertical
  blank (a plain wait, MPC is not involved), and only the rows that changed. 0.5.2 copied at any moment, so a spinning
  shape showed two turns at once and seemed to fall apart.

## 0.5.2 (2026-10-23)

- The live screen no longer calls the display for every frame: its plane is set once when it is shown, and each frame
  is drawn in ordinary memory and copied into the plane's buffer in one pass. MPC's own screen updates never wait on
  it (0.5.0's per-frame plane swaps waited for the display's refresh, and MPC's drawing queued behind them: laggy
  readouts while the sound stayed in time). Thread priority as in 0.5.0 (0.5.1's low priority was worse).
- Looking for the screen box on the display: MPC's picture is mapped for one look and released at once (a mapping
  kept pinned MPC's buffer), and the whole-picture search runs at most twice a second (it reads a lot of display
  memory: leaving the MAIN page lagged while it ran ten times a second); otherwise only the box's last place is
  checked, eight pixels.

## 0.5.1 (2026-10-23)

- The live screen's thread runs at a low priority: MPC's own screen and touch come first on the core they share (the
  audio has cores of its own), so a busy moment drops a frame of the live screen instead of making MPC lag. 25 frames a
  second, a lighter bloom.

## 0.5.0 (2026-10-23)

Not yet tested on a device: this build is the try.

- **The live screen**: VADER draws its screen itself, straight onto a hardware overlay plane of the display, so MPC
  redraws nothing while it moves. The plugin runs inside MPC, and uses the display handle MPC already holds (no change
  to the firmware, no patch, no service: the same install as always). The drawing: the shape as an anti-aliased glowing
  line (3D shapes shaded by depth), the orbit line faint, the orbiter's light trail as a smooth fading curve, the two
  balls as soft-edged spheres with a glow, a loaded picture dimly under its shape, a bloom over the lights, scanlines
  and a vignette; 30 frames a second, slower by itself when the device is slower. The tile screen rests (empty) while
  it is up and carries on as before when it isn't.
- Where the screen box is on the display, and whether it is on screen at all, VADER reads from the picture MPC shows:
  two marker pixels in the skin's glass layer at the box's corners. The live screen hides itself on the other pages,
  when the SHAPE list is open over it, and whenever the markers can't be seen.
- Everything it finds and does goes to `overlay.log` in the plugin's folder (the display's planes and formats, the
  plane it took, where the box is, frame times, any error). A file `overlay.off` in the plugin's folder keeps the live
  screen off. Screen captures through MPC Remote don't show it (the display chip composes it, not MPC).
- With two VADER instances in one project, the first one up has the live screen; the other keeps the tile screen.

## 0.4.5 (2026-10-23)

Not yet tested on a device.

- **No more crackle with MOTION on.** 0.4.4 asked MPC to redraw 35 to 47 screen pieces every 40 ms (about 1000 a
  second; MPC redraws each one itself, which is the CPU you saw): the orbit line was redrawn as its start phase
  drifted, and the light trail re-levelled every tile along it each frame. Now the drawing is the same (the dotted
  orbit line, the light trail, the two balls at 25 frames a second) but:
  - the orbit line is worked out once, when the shape, its picture frame, ORBIT SIZE, ROTATE, SUBORBIT SIZE or the
    speed ratio changes, and then left alone;
  - the light trail is the last 14 tiles the orbiter crossed (the newest 6 bright, the rest softer): only the tile it
    enters and the one that falls off the end change;
  - the corner bars and the IMAGE meters report about 8 times a second, in steps of 5;
  - and a hard ceiling: at most 10 screen pieces a poll (250 a second), the balls first, then the trail, then the bars,
    meters and envelope, then the shape's and the orbit line's tiles in turn. A new shape or size spreads its redraw
    over a second or two instead of a burst.
  In steady play that is 4 to 8 pieces a poll (100 to 200 a second), the level of 0.4.1.
- A fresh instance, and one whose state was just restored, report only the pieces that show something (not every one
  of the 1500 screen parameters).
- A preset change no longer re-sends the whole screen.

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

- Suborbit shape MOBIUS: swings across the path with a half turn per lap (twisted ribbon).
- Kaleidoscope: every second copy is now mirrored (not only rotated), in the sound and on the screens.
- IMAGE page: new sources LINE COLOUR and LINE WAVE (colour along the line, for spirographs).
- FOURIER shape: new LOADED option rebuilds your loaded shape from circles.
- Desktop: MIDI CC 22 / CC 23 turn to the previous / next page.
- MOBIUS suborbit now follows the path's 3D Frenet frame (twists in depth on 3D shapes, flat on 2D ones).
