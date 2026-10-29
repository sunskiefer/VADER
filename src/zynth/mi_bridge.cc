// LXRXXL ZYNTH -- the Mutable Instruments engines (MIT, third_party/mutable), behind a small C interface:
//   MACRO  Braids' macro oscillator (run at 88.2 kHz and brought down to 44.1 kHz, pitched for its 96 kHz tables)
//   WAVE   Plaits' wavetable engine (Plaits set to 44.1 kHz), with a warp and a scan envelope added here
//   PHYS   Plaits' string voice (Karplus-Strong with dispersion) and modal voice (a bank of resonators)
// Braids and Plaits are by Emilie Gillet (Mutable Instruments), MIT license: third_party/mutable/LICENSE.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <new>

#include "braids/macro_oscillator.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/physical_modelling/string_voice.h"
#include "plaits/dsp/physical_modelling/modal_voice.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/dsp/limiter.h"

extern "C" {
#include "eng.h"
}

// The C++ runtime pieces the engines' classes refer to (no libstdc++ is linked).
extern "C" void __cxa_pure_virtual() { abort(); }
void operator delete(void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }

namespace {

// the shapes MACRO offers: Braids' list without its drums (KICK, CYMBAL, SNARE) and the hidden "?" shape
const braids::MacroOscillatorShape MACRO_SHAPES[44] = {
    braids::MACRO_OSC_SHAPE_CSAW, braids::MACRO_OSC_SHAPE_MORPH, braids::MACRO_OSC_SHAPE_SAW_SQUARE,
    braids::MACRO_OSC_SHAPE_SINE_TRIANGLE, braids::MACRO_OSC_SHAPE_BUZZ, braids::MACRO_OSC_SHAPE_SQUARE_SUB,
    braids::MACRO_OSC_SHAPE_SAW_SUB, braids::MACRO_OSC_SHAPE_SQUARE_SYNC, braids::MACRO_OSC_SHAPE_SAW_SYNC,
    braids::MACRO_OSC_SHAPE_TRIPLE_SAW, braids::MACRO_OSC_SHAPE_TRIPLE_SQUARE, braids::MACRO_OSC_SHAPE_TRIPLE_TRIANGLE,
    braids::MACRO_OSC_SHAPE_TRIPLE_SINE, braids::MACRO_OSC_SHAPE_TRIPLE_RING_MOD, braids::MACRO_OSC_SHAPE_SAW_SWARM,
    braids::MACRO_OSC_SHAPE_SAW_COMB, braids::MACRO_OSC_SHAPE_TOY, braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_LP,
    braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_PK, braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_BP,
    braids::MACRO_OSC_SHAPE_DIGITAL_FILTER_HP, braids::MACRO_OSC_SHAPE_VOSIM, braids::MACRO_OSC_SHAPE_VOWEL,
    braids::MACRO_OSC_SHAPE_VOWEL_FOF, braids::MACRO_OSC_SHAPE_HARMONICS, braids::MACRO_OSC_SHAPE_FM,
    braids::MACRO_OSC_SHAPE_FEEDBACK_FM, braids::MACRO_OSC_SHAPE_CHAOTIC_FEEDBACK_FM, braids::MACRO_OSC_SHAPE_PLUCKED,
    braids::MACRO_OSC_SHAPE_BOWED, braids::MACRO_OSC_SHAPE_BLOWN, braids::MACRO_OSC_SHAPE_FLUTED,
    braids::MACRO_OSC_SHAPE_STRUCK_BELL, braids::MACRO_OSC_SHAPE_STRUCK_DRUM, braids::MACRO_OSC_SHAPE_WAVETABLES,
    braids::MACRO_OSC_SHAPE_WAVE_MAP, braids::MACRO_OSC_SHAPE_WAVE_LINE, braids::MACRO_OSC_SHAPE_WAVE_PARAPHONIC,
    braids::MACRO_OSC_SHAPE_FILTERED_NOISE, braids::MACRO_OSC_SHAPE_TWIN_PEAKS_NOISE,
    braids::MACRO_OSC_SHAPE_CLOCKED_NOISE, braids::MACRO_OSC_SHAPE_GRANULAR_CLOUD,
    braids::MACRO_OSC_SHAPE_PARTICLE_NOISE, braids::MACRO_OSC_SHAPE_DIGITAL_MODULATION,
};

// Braids' tables are made for 96 kHz; it runs here at 88.2 kHz: ask for the note this much higher
const float kBraidsPitchOffset = 1.4676f;   // = 12 log2(96000 / 88200) semitones
const int kBraidsBlock = 16;   // even, as some of Braids' shapes render two samples at a time

// a half-band decimator: 88.2 -> 44.1 kHz (an 11-tap symmetric FIR)
struct Halfband {
  float z[11];
  void Init() { memset(z, 0, sizeof z); }
  float Process(float a, float b) {   // two input samples, one output
    for (int i = 10; i >= 2; --i) z[i] = z[i - 2];
    z[1] = a;
    z[0] = b;
    return 0.5f * z[5] + 0.30406f * (z[4] + z[6]) - 0.06764f * (z[2] + z[8]) + 0.01359f * (z[0] + z[10]);
  }
};

struct Mi {
  // MACRO
  braids::MacroOscillator macro;
  int16_t bbuf[kBraidsBlock];
  uint8_t sync[kBraidsBlock];
  Halfband hb;
  float fifo[kBraidsBlock];
  int fifoLen, fifoPos;
  int shape;
  float menv;
  int mstage;              // the shape envelope: 1 attack, 2 decay
  float mhold, mholdPh, mdrift, mdriftT, sigA, sigB, tilt;
  uint32_t mrnd;
  // WAVE
  plaits::WavetableEngine wt;
  char wtRam[256 * sizeof(void *) + 64];   // the wave map: 4 banks x 64 waves (Plaits allocates 64 and writes 256)
  float scan;
  uint32_t ringPh;
  float wdrift, wdriftT, wtilt;
  uint32_t wrnd;
  int wtWarm;   // a fresh wavetable engine plays a few silent frames first (its differentiator starts with a jump)
  // PHYS
  plaits::StringVoice str;
  char strRam[(1024 + 256) * sizeof(float) + 64];
  plaits::ModalVoice modal;
  int physModel;
  stmlib::Limiter lim;   // as Plaits does for its string and modal engines
  float b1[3], b2[3];      // BODY: three resonances
  float strike, pstrike, nz;
  uint32_t prnd;
  float comb[2048];
  int cw;
  float temp[32], aux[32], o[32];
};

void reset_macro(Mi *m) {
  m->macro.Init();
  m->shape = -1;
  m->hb.Init();
  m->fifoLen = m->fifoPos = 0;
  m->menv = 0.f;
  m->mstage = 2;
  m->mhold = m->mholdPh = m->mdrift = m->mdriftT = m->tilt = 0.f;
  m->mrnd = 0x1B873593u;
  m->sigA = m->sigB = 0.f;
  memset(m->sync, 0, sizeof m->sync);
}

void reset_wave(Mi *m) {
  stmlib::BufferAllocator a(m->wtRam, sizeof m->wtRam);
  m->wt.Init(&a);
  m->wt.LoadUserData(NULL);
  m->scan = 0.f;
  m->ringPh = 0;
  m->wdrift = m->wdriftT = m->wtilt = 0.f;
  m->wrnd = 0x68E31DA4u;
  m->wtWarm = 1;
}

void reset_phys(Mi *m) {
  stmlib::BufferAllocator a(m->strRam, sizeof m->strRam);
  m->str.Init(&a);
  m->modal.Init();
  m->physModel = -1;
  m->lim.Init();
  memset(m->b1, 0, sizeof m->b1);
  memset(m->b2, 0, sizeof m->b2);
  m->strike = m->pstrike = m->nz = 0.f;
  m->prnd = 0x4F1BBCDCu;
  memset(m->comb, 0, sizeof m->comb);
  m->cw = 0;
}

inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
inline int zone_of(float v, int n) { int i = (int)(clamp01(v) * (float)n); return i >= n ? n - 1 : i; }
inline uint32_t mrand(uint32_t *s) { uint32_t x = *s ? *s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }
inline float mbrand(uint32_t *s) { return (float)(mrand(s) >> 8) * (2.f / 16777216.f) - 1.f; }

// BRIGHT (0.5 neutral): below, a low-pass closing; above, a high shelf lifting (a one-pole split)
inline float tilt_eq(float *z, float x, float v) {
  if (v > 0.49f && v < 0.51f) return x;
  if (v < 0.5f) {
    const float a = 1.f - expf(-6.2831853f * 300.f * exp2f(v * 2.f * 6.f) / 44100.f);
    *z += a * (x - *z);
    return *z;
  }
  const float a = 1.f - expf(-6.2831853f * 1500.f / 44100.f);
  *z += a * (x - *z);
  return x + (x - *z) * (v - 0.5f) * 3.f;
}

void render_macro(Mi *m, const EngIn *in, float *out, int n) {
  const int shape = zone_of(in->p[0], 44);
  if (shape != m->shape) { m->macro.set_shape(MACRO_SHAPES[shape]); m->shape = shape; }
  // the shape's envelope: TIMBRE ENV and COLOR ENV push from where TIMBRE and COLOR stand, ENV DECAY long or short
  const float dec = expf(-(float)n / ((0.01f + 2.5f * in->p[5] * in->p[5]) * 44100.f));
  const float atk = (float)n / ((0.002f + 1.5f * in->p[6] * in->p[6]) * 44100.f);
  if (in->trig) {
    m->macro.Strike();
    if (in->p[6] > 0.002f) { m->mstage = 1; m->menv = 0.f; } else { m->mstage = 2; m->menv = 1.f; }
    m->sigA = mbrand(&m->mrnd); m->sigB = mbrand(&m->mrnd);   // SIGNATURE: each note's own small flaws
  }
  if (m->mstage == 1) { m->menv += atk; if (m->menv >= 1.f) { m->menv = 1.f; m->mstage = 2; } }
  const float t = clamp01(in->p[1] + in->p[3] * m->menv);
  const float c = clamp01(in->p[2] + in->p[4] * m->menv);
  if (m->mstage == 2) m->menv *= dec;
  // DRIFT: a slow random walk of the pitch
  if (in->p[9] > 0.001f) {
    m->mdriftT -= (float)n;
    if (m->mdriftT <= 0.f) { m->mdriftT = 44100.f * 0.3f; m->mdrift += 0.5f * (mbrand(&m->mrnd) - m->mdrift); }
  }
  float note = in->note + kBraidsPitchOffset + in->p[9] * 0.3f * m->mdrift;
  if (note < 0.f) note = 0.f;
  if (note > 127.f) note = 127.f;
  m->macro.set_pitch((int16_t)(note * 128.f));
  m->macro.set_parameters((int16_t)(t * 32767.f), (int16_t)(c * 32767.f));
  for (int k = 0; k < n; ++k) {
    if (m->fifoPos >= m->fifoLen) {
      m->macro.Render(m->sync, m->bbuf, kBraidsBlock);
      for (int i = 0; i < kBraidsBlock / 2; ++i)
        m->fifo[i] = m->hb.Process(m->bbuf[2 * i] * (1.f / 32768.f), m->bbuf[2 * i + 1] * (1.f / 32768.f));
      m->fifoLen = kBraidsBlock / 2;
      m->fifoPos = 0;
    }
    out[k] = m->fifo[m->fifoPos++] * 1.1f;
  }
  // after Braids: SIGNATURE, CRUSH, DOWNSAMPLE, BRIGHT
  const float sig = in->p[10], crush = in->p[7], ds = in->p[8], br = in->p[11];
  const float lv = exp2f(15.f - crush * 13.f), hz = 44100.f * exp2f(-ds * 6.f);
  for (int k = 0; k < n; ++k) {
    float x = out[k];
    if (sig > 0.001f) x = x + sig * 0.25f * (m->sigA * x * x + m->sigB * 0.1f);
    if (crush > 0.005f) x = floorf(x * lv + 0.5f) / lv;
    if (ds > 0.005f) {
      m->mholdPh += hz / 44100.f;
      if (m->mholdPh >= 1.f) { m->mholdPh -= floorf(m->mholdPh); m->mhold = x; }
      x = m->mhold;
    }
    out[k] = tilt_eq(&m->tilt, x, br);
  }
}

void render_wave(Mi *m, const EngIn *in, float *out, int n) {
  plaits::EngineParameters p;
  bool enveloped = false;
  if (in->trig) m->scan = 1.f;
  if (in->p[8] > 0.001f) {   // DRIFT: the position wanders
    m->wdriftT -= (float)n;
    if (m->wdriftT <= 0.f) { m->wdriftT = 44100.f * 0.4f; m->wdrift += 0.4f * (mbrand(&m->wrnd) - m->wdrift); }
  }
  p.trigger = in->trig ? 1 : 0;   // (unused by the wavetable)
  p.note = in->note;
  p.harmonics = clamp01(in->p[0]) * 0.5f;   // the four banks (Plaits mirrors them past the middle)
  p.timbre = clamp01(in->p[1] + in->p[5] * m->scan * 0.6f + in->p[8] * 0.15f * m->wdrift);
  p.morph = clamp01(in->p[2] + in->p[7] * m->scan * 0.6f);
  p.accent = in->accent;
  const float scanEnv = m->scan;
  m->scan *= expf(-(float)n / ((0.03f + 3.f * in->p[6] * in->p[6]) * 44100.f));
  if (m->wtWarm) {
    for (int w = 0; w < 6; ++w) { m->wt.Render(p, m->o, m->aux, 8, &enveloped); }
    m->wtWarm = 0;
  }
  memset(m->aux, 0, sizeof(float) * (size_t)n);
  m->wt.Render(p, out, m->aux, (size_t)n, &enveloped);
  for (int k = 0; k < n; ++k) out[k] *= 1.4f;
  // the warp, after the wavetable
  const int type = zone_of(in->p[3], 4);
  const float a = clamp01(in->p[4] + in->p[10] * scanEnv);
  const float br = in->p[11];
  if (a < 0.001f) {
    if (br < 0.49f || br > 0.51f) for (int k = 0; k < n; ++k) out[k] = tilt_eq(&m->wtilt, out[k], br);
    return;
  }
  const uint32_t ringInc = (uint32_t)(in->freq * (0.5f + 7.5f * in->p[9]) / 44100.f * 4294967296.f);
  for (int k = 0; k < n; ++k) {
    float x = out[k];
    switch (type) {
      case 0: x = sinf(1.5707963f * x * (1.f + a * 6.f)); break;                           // FOLD
      case 1: { const float g = 1.f + a * 8.f; x = tanhf(g * x + a * 0.6f) - tanhf(a * 0.6f); x /= tanhf(g) > 0.1f ? tanhf(g) : 1.f; break; }   // SHAPE
      case 2: { const float lv = exp2f(8.f - a * 6.5f); x = floorf(x * lv + 0.5f) / lv; break; }   // CRUSH
      default:                                                                              // RING
        m->ringPh += ringInc;
        x *= 1.f - a + a * sinf(6.2831853f * (float)m->ringPh * (1.f / 4294967296.f));
        break;
    }
    out[k] = tilt_eq(&m->wtilt, x, br);
  }
}

void render_phys(Mi *m, const EngIn *in, float *out, int n) {
  const int model = zone_of(in->p[0], 2);
  if (model != m->physModel) { m->physModel = model; }
  if (in->trig) { m->strike = 1.f; m->pstrike = 1.f; }
  const float bend = in->p[8] * in->p[8] * 12.f * m->pstrike;      // PITCH STRIKE: a bend down into the note
  m->pstrike *= expf(-(float)n / (0.05f * 44100.f));
  float freq = in->freq * exp2f(bend / 12.f);
  if (freq > 15000.f) freq = 15000.f;
  const float f0 = freq / 44100.f;
  const bool sustain = in->p[4] > 0.05f && in->gate;
  const float strength = clamp01(in->p[5]) * (0.6f + 0.4f * in->vel) + 0.25f * in->accent;
  // MUTE: let go and the body is damped (by MUTE)
  const float damp = in->gate ? clamp01(in->p[3]) : clamp01(in->p[3] + in->p[9] * (1.f - in->p[3]));
  memset(m->o, 0, sizeof(float) * (size_t)n);
  memset(m->aux, 0, sizeof(float) * (size_t)n);
  if (model == 0)
    m->str.Render(sustain, in->trig != 0, clamp01(strength), f0, clamp01(in->p[1]), clamp01(in->p[2]),
                  damp, m->temp, m->o, m->aux, (size_t)n);
  else
    m->modal.Render(sustain, in->trig != 0, clamp01(strength), f0, clamp01(in->p[1]), clamp01(in->p[2]),
                    damp, m->temp, m->o, m->aux, (size_t)n);
  // the exciter is heard a little with the body (Plaits' aux), more as EXCITER goes up
  const float ex = 0.15f + 0.35f * clamp01(in->p[4]);
  const float body = in->p[6], noise = in->p[7], symp = in->p[10], drive = in->p[11];
  static const float BF[3] = {110.f, 240.f, 520.f};
  const float sdec = expf(-1.f / (0.02f * 44100.f));
  int clen = (int)(44100.f / (freq * 2.f));
  if (clen < 8) clen = 8;
  if (clen > 2047) clen = 2047;
  for (int k = 0; k < n; ++k) {
    float x = m->o[k] + ex * m->aux[k];
    if (noise > 0.001f && m->strike > 0.0005f) { x += noise * 0.6f * m->strike * mbrand(&m->prnd); m->strike *= sdec; }
    if (body > 0.001f) {   // BODY: a wooden box's three resonances under the string
      float b = 0.f;
      for (int j = 0; j < 3; ++j) {
        const float g = tanf(3.14159265f * BF[j] / 44100.f), kk = 1.f / 4.f;
        const float hp = (x - (kk + g) * m->b1[j] - m->b2[j]) / (1.f + kk * g + g * g);
        const float bp = g * hp + m->b1[j];
        const float lp = g * bp + m->b2[j];
        m->b1[j] = g * hp + bp;
        m->b2[j] = g * bp + lp;
        b += bp * kk;
      }
      x += body * 2.5f * b;
    }
    if (symp > 0.001f) {   // SYMPATHETIC: a string an octave up ringing along
      const int r = (m->cw - clen) & 2047;
      const float y = x + symp * 0.85f * m->comb[r];
      m->comb[m->cw] = y;
      m->cw = (m->cw + 1) & 2047;
      x = x + symp * 0.5f * m->comb[r];
    }
    if (drive > 0.001f) x = tanhf(x * (1.f + drive * 6.f)) / (1.f + drive * 2.f) * (1.f + drive);
    out[k] = x;
  }
  m->lim.Process(3.5f, out, (size_t)n);
}

}  // namespace

extern "C" void *mi_new(void) {
  void *mem = malloc(sizeof(Mi));
  if (!mem) return NULL;
  memset(mem, 0, sizeof(Mi));
  Mi *m = new (mem) Mi();
  reset_macro(m);
  reset_wave(m);
  reset_phys(m);
  return m;
}

extern "C" void mi_free(void *p) { free(p); }

extern "C" void mi_reset(void *p, int type) {
  Mi *m = static_cast<Mi *>(p);
  if (type == ENG_MACRO) reset_macro(m);
  else if (type == ENG_WAVE) reset_wave(m);
  else if (type == ENG_PHYS) reset_phys(m);
}

extern "C" void mi_render(void *p, int type, const EngIn *in, float *out, int n) {
  Mi *m = static_cast<Mi *>(p);
  if (n > 32) n = 32;
  switch (type) {
    case ENG_MACRO: render_macro(m, in, out, n); break;
    case ENG_WAVE: render_wave(m, in, out, n); break;
    case ENG_PHYS: render_phys(m, in, out, n); break;
    default: memset(out, 0, sizeof(float) * (size_t)n); break;
  }
}
