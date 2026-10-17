# VADER

**An orbit-based vector synthesizer for the Akai Force and MPC (MPC OS, VST2), by L'Cronx.**

A note plays four sound sources at once, one in each corner of a square. A point, the **orbiter**, travels around a
**shape** inside that square, and where it is decides how much of each corner you hear. Move the point and the sound
morphs. The shape can be one of fifteen built-in figures, or one you bring yourself: a drawing (SVG), a 3D wireframe
(OBJ) or a picture (PNG/JPG), traced into a line. The screen draws that line, the orbiter with its trail of dots and the envelope,
live, as glowing lines.

![VADER's main page, playing the GLASS STAR preset](docs/img/main_star.png)

## The idea

I wanted a vector synth on the Force that works like the **Beetlecrab Audio Vector**: the same way of playing, and above
all that **screen**, with the orbiter tracing its path while the sound moves between four corners. I wanted the knobs as
on the hardware: envelope on the left, mixer and arpeggiator on the right, the screen in the middle with the overtone,
filter, delay and chorus controls under it, and the second functions on an ALT layer. It is the same layout, with the
parts moved around.

Then I wanted to go further than the hardware: **load any image, vector drawing or 3D shape and let it drive the
oscillators.** The shape becomes the orbiter's path, and it can also be played as sound itself, the way vector graphics
are drawn with audio on an oscilloscope.

The look: every knob is a **line**, a thin track with a thicker glowing blue line on top that grows with the value. No
panel boxes, so my own background picture shows through. There is a strip across the top of every page, like on my
BAZZLINE panels, showing the name and value of the last control touched. All the text is big, and so are the dropdowns.

VADER is its own engine, written for the Force's CPU. It is not a port and contains no Beetlecrab code, firmware or
artwork; it is not affiliated with Beetlecrab Audio.

**Inspired by and based on:**
- [Beetlecrab Audio Vector](https://beetlecrab.audio/vector/): its orbiter and suborbit, its four corners of two overtone
  generators with a blend stage and filter each, its corner matrix, and firmware 3.0's two timbres.
- Vector synthesis as Sequential's Prophet VS started it: mixing four sources from a point in a square.
- Oscilloscope vector graphics: [Vector Synthesis](https://github.com/macumbista/vectorsynthesis) by Derek Holzer
  (Lissajous figures, 2D and 3D shapes drawn with audio). This is behind the SHAPE SOUND and the 3D shapes.
- [OSS Enterprise](https://plugins4free.com/plugin/3435) by Ocean Swift Synthesis: a hybrid vector synth with XY
  movement.
- The MPC plugin framework [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins), which makes native MPC OS plugins
  possible.

## What's in it

- **Four corners, each two OVERTONE GENerators**: 32 additive spectra (sine to saw, pulse widths, vowels, organ
  drawbars, bells, primes...), swept smoothly by one knob, band-limited so nothing aliases.
- **BLEND per corner**: MIX, AM, PM, AM+PM, phase distortion, RING, FOLD.
- **A filter per corner**: LP12, LP24, BP, HP or a ladder, placed after the blend or on either generator (the corner
  matrix), plus MASTER RESONANCE.
- **The orbiter**: 15 built-in shapes (CIRCLE, FIGURE 8, STAR, SQUARE, TRIANGLE, two Lissajous, two roses, SPIRAL,
  HEART, ZIGZAG, SPIRO, and the 3D CUBE and ICOSA) or your own USER shape. It has speed, size, rotation, 3D spin and
  tilt, and a SUBORBIT circling the path point. It can sync to MPC's tempo, restart on each note, and grow or shrink
  with the envelope.
- **Your own shapes**: SVG, OBJ, PNG, JPG from the plugin's `Shapes` folder, or a `VADER Shapes` folder on your SD
  card or USB drive. Seven examples come with it.
- **SHAPE SOUND**: the shape itself played as audio, x on the left, y on the right.
- **Two timbres**: A and B, layered or split across the keyboard, each a complete sound. Unison up to 8 with detune,
  glide, vibrato, tremolo, 8 or 16 voices.
- **Arpeggiator** with six directions, six rates synced to MPC, 1 to 4 octaves, latch, and a 16-step pattern
  (ON, ACCENT, TIE, OFF).
- **Effects**: drive, chorus, ping-pong delay (synced, or free with TAP), reverb.
- **16 presets** in MPC's PRESET menu, from INIT ORBIT to SHAPE SCOPE, and **999 slots for your own** (MY PRESETS).
- **The IMAGE page**: a picture plays the sound. Four MODs read the picture under the orbiter (BRIGHT on a spectrogram
  colour scale, HUE, COLOUR, DEPTH as a landscape) or the outline (TURN, HEADING) and move CUTOFF, RESO, OCTAVE, PITCH,
  GEN 1, GEN 2, BLEND or LEVEL.
- **Six pages**: MAIN, CORNERS, ORBIT, IMAGE, ARP, FX. Q-Links are mapped on every page.

## The screen

MPC draws a plugin's screen itself, from pictures and the plugin's values; a plugin cannot draw on the Force's display
directly. So VADER's screen is built from pieces MPC can switch:
- the **built-in shapes** as ready-drawn, glowing pictures: 12 size steps and fine turn steps over each shape's own
  symmetry (a STAR every 12 degrees), and CUBE and ICOSA as spinning frames, so they stay crisp and move in small steps;
- **your own 2D shapes** drawn by VADER itself into its skin folder when loaded (an experiment: MPC may show them only
  after the plugin window is opened again);
- the **orbit line**: the orbiter's whole path over one orbit as a faint thin line, lit up as a **light trail** where the
  orbiter has just passed, on its own layer of tiles;
- a grid of **32 × 16 fine tiles** for the rest: the path point, the orbiter (a round dot moving
  in 3 px steps) and shapes without pictures. Each tile holds a straight piece of line at one of 24 angles, so a line
  stays straight across tiles;
- a light **glass** layer on top (fine scanlines, darker edges), like a CRT;
- the **envelope**, drawn as 32 strokes that follow ATTACK, DECAY, SUSTAIN and RELEASE;
- **four corner bars**, showing how much A, B, C and D you hear.

The square of the corners is drawn square, at its true proportions.

The engine works out what each piece shows, 25 times a second, and only tells MPC about the pieces that changed.
The shape is drawn in dim blue, the suborbit's trail in bright cyan, the path point in amber and the orbiter in cyan.
**MOTION** off freezes the screen, which saves MPC's screen CPU in a heavy project.

| | |
|---|---|
| ![a loaded picture on the screen](docs/img/main_user_shape.png) | ![the CORNERS page](docs/img/corners.png) |
| ![the ORBIT page](docs/img/orbit.png) | ![the IMAGE page](docs/img/image.png) |

## Install

1. Download `VADER-<version>-mpc-armv7.zip` from [Releases](https://github.com/sunskiefer/VADER/releases).
2. Unzip it and follow `INSTALL.md` inside. It needs root SSH access to the device (MockbaMod, for example); the
   installer stops MPC, adds VADER to the plugin list and starts MPC again.
3. Load it on a track: Plugin → VST → **VADER**.
4. Put your own shapes in `L'Cronx - VST - VADER/Shapes/` inside the Synths folder you installed to, or in a
   folder called `VADER Shapes` at the top of your SD card or USB drive.

MPC OS 3.x. The skin uses the MPC OS 3 format.

## Use

Read the **[user guide](docs/USER_GUIDE.md)**. It covers every page, every control and every Q-Link, with recipes and
tips on making shapes.

The quick version: play a chord, then turn **ORBIT SPEED** and **ORBIT SIZE** and watch the orbiter. Pick a SHAPE.
Set CORNER SELECT to one corner and turn OVERTONE GEN 1 and 2, BLEND and the filter to change what that corner sounds
like. Press **ALT** for the second function of each knob.

## Build

```
pip install ziglang pillow numpy
./build.sh                 # -> build/package/L'Cronx - VST - VADER/
test/run_tests.sh          # the engine and the VST2 wrapper, under valgrind (NOVG=1 to skip it)
test/arm_sim.sh            # the engine test as ARM code under qemu (needs qemu-user)
```

The parameters live in one place, `tools/gen_params.py`. The pages are in `tools/vader_skin.py`, and
`tools/preview.py` draws them with the engine really playing (the pictures above). The engine is in `src/`.

## Status

**0.4.2 has not been tested on a device yet.** Offline it passes the framework's VST2 host test and VADER's own engine
test (every preset, shape, blend mode and filter, the arpeggiator on MPC's clock, every example shape file, the state
round trip, the screen updates) on x86 and as ARM code, and the release zip passes the catalog check. Two things only a
Force can show:
- **CPU**: the worst case (16 voices, all four corners with ladder filters, every effect) should be measured with the
  framework's `tools/bench.sh`. The default is 8 voices.
- **The screen**: the MAIN page has about 1330 parts, and the shape pictures take about 260 MB once MPC has loaded them
  (more steps for size, turn and spin). If MPC is slow to open VADER or runs short, the steps can be made coarser.
- **Your shape's pictures**: whether MPC shows the pictures VADER writes when a file is loaded (an experiment).

## License

VADER is MIT-licensed (see [LICENSE](LICENSE)). Bundled: [mpc-vst-plugins](third_party/mpc-vst-plugins) (MIT),
[stb_image](third_party/stb) (public domain / MIT), Chakra Petch (SIL Open Font License, `art/fonts`).
