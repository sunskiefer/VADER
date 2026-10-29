# The source lists, shared by build.sh and the test scripts.
PLUGIN_SRCS="src/tables.c src/shape.c src/paths.c src/modes.c src/shapefile.c src/screen.c src/fx.c src/pictures.c src/overlay.c src/qr.c src/web.c src/engine.c"
INCS="-Isrc -Isrc/zynth -Ithird_party/stb"

# 0.8: the voices from LXRXXL ZYNTH (src/zynth, third_party/mutable, third_party/lxr02open): C files with their own include paths,
# and the Mutable Instruments engines (C++, MIT) for MACRO and WAVE
M=third_party/mutable
ZYN_SRCS="src/zynth/engines.c src/zynth/eng_osc.c src/zynth/eng_fm.c src/zynth/eng_more.c src/zynth/eng_new.c third_party/lxr02open/Core/DSPAudio/wavetable.c"
ZINCS="-Isrc/zynth -Isrc/zynth/shim -Ithird_party/lxr02open -Ithird_party/lxr02open/Core -Ithird_party/lxr02open/Core/DSPAudio"
CXX_SRCS="src/zynth/mi_bridge.cc $M/braids/macro_oscillator.cc $M/braids/digital_oscillator.cc $M/braids/analog_oscillator.cc $M/braids/resources.cc $M/plaits/dsp/engine/wavetable_engine.cc $M/plaits/dsp/physical_modelling/modal_voice.cc $M/plaits/dsp/physical_modelling/resonator.cc $M/plaits/dsp/physical_modelling/string.cc $M/plaits/dsp/physical_modelling/string_voice.cc $M/plaits/resources.cc $M/stmlib/dsp/units.cc $M/stmlib/utils/random.cc"
CXXINCS="-Isrc/zynth -I$M"
CXXFLAGS_MI="-std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -DTEST"   # TEST: stmlib's portable C paths (no Cortex-M asm)
