/* LXRXXL ZYNTH -- small DSP helpers shared by the engines written here. */
#pragma once
#include <math.h>
#include <stdint.h>
#include "eng.h"

#ifndef SR
#define SR 44100.0f
#endif
#define PH_ONE 4294967296.0f              /* a 32-bit phase: one cycle */
#define TWO_PI_F 6.283185307f

#define SIN_BITS 11
#define SIN_SIZE (1 << SIN_BITS)
extern float ENG_SIN[SIN_SIZE + 1];       /* one sine cycle (+1 guard) */
extern float ENG_HANN[257];               /* a Hann window 0..1 (+1 guard) */

static inline float clampe(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline uint32_t ph_inc(float hz)
{
    if (hz < 0.f) hz = 0.f;
    if (hz > 20000.f) hz = 20000.f;
    return (uint32_t)(hz * (PH_ONE / SR));
}

/* the sine at a 32-bit phase */
static inline float esin(uint32_t ph)
{
    const uint32_t i = ph >> (32 - SIN_BITS);
    const float fr = (float)(ph & ((1u << (32 - SIN_BITS)) - 1)) * (1.f / (float)(1u << (32 - SIN_BITS)));
    return ENG_SIN[i] + fr * (ENG_SIN[i + 1] - ENG_SIN[i]);
}

/* the sine of a phase in cycles (any real number) */
static inline float esinf(float cycles)
{
    return esin((uint32_t)(int64_t)(cycles * PH_ONE));
}

static inline float ph_unit(uint32_t ph) { return (float)ph * (1.f / PH_ONE); }

/* polyBLEP: the band-limited step's correction near a jump, t and dt in cycles */
static inline float blep(float t, float dt)
{
    if (dt <= 0.f) return 0.f;
    if (t < dt) { const float x = t / dt; return x + x - x * x - 1.f; }
    if (t > 1.f - dt) { const float x = (t - 1.f) / dt; return x * x + x + x + 1.f; }
    return 0.f;
}

static inline float saw_blep(float t, float dt) { return 2.f * t - 1.f - blep(t, dt); }

static inline float sq_blep(float t, float dt, float pw)
{
    float t2 = t + 1.f - pw;
    float v = t < pw ? 1.f : -1.f;
    if (t2 >= 1.f) t2 -= 1.f;
    return v + blep(t, dt) - blep(t2, dt);
}

static inline uint32_t xrand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
static inline float frand(uint32_t *s) { return (float)(xrand(s) >> 8) * (1.f / 16777216.f); }       /* 0..1 */
static inline float brand(uint32_t *s) { return frand(s) * 2.f - 1.f; }                            /* -1..1 */

static inline float etanh(float x)
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

/* a stepped slider: option 0..n-1 */
static inline int zone(float v, int n)
{
    int i = (int)(clampe(v, 0, 1) * (float)n);
    return i >= n ? n - 1 : i;
}

static inline float semis(float s) { return exp2f(s * (1.f / 12.f)); }

/* a two-pole state-variable filter (Chamberlin, trapezoidal): band-pass / low-pass */
typedef struct { float g, k; } SvfCo;
static inline SvfCo svf_co(float hz, float q)
{
    SvfCo c;
    if (hz > SR * 0.45f) hz = SR * 0.45f;
    if (hz < 10.f) hz = 10.f;
    c.g = tanf(3.14159265f * hz / SR);
    c.k = 1.f / (q < 0.3f ? 0.3f : q);
    return c;
}
/* one sample; returns the band-pass, *lp the low-pass */
static inline float svf_bp(SvfCo c, float *s1, float *s2, float x, float *lp)
{
    const float hp = (x - (c.k + c.g) * *s1 - *s2) / (1.f + c.k * c.g + c.g * c.g);
    const float bp = c.g * hp + *s1;
    const float l = c.g * bp + *s2;
    *s1 = c.g * hp + bp;
    *s2 = c.g * bp + l;
    if (lp) *lp = l;
    return bp;
}
