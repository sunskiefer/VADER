/* VADER desktop: the engine runs at 44100 Hz (its effects' delay lines and constants are in samples, so a different rate
 * would change the sound); this brings its output to the host's rate.
 *
 * A windowed-sinc (Kaiser, beta 9) interpolator, 64 taps wide (more when the host rate is below 44100, so the cut-off
 * follows the lower Nyquist). The engine's oscillators stop at 19 kHz, so the filter's pass band (to 19 kHz) and its stop
 * band (from 25 kHz at 48000 Hz) are what the engine's own band-limiting needs. At exactly 44100 Hz nothing is done.
 *
 * The engine's clock is "now": output sample j is built from the engine's samples around j * ratio - taps/2, so the
 * output lags by taps/2 samples (reported to the host as latency), and a MIDI event for host frame f is applied at
 * engine sample ceil(f * ratio): it stays sample accurate, only later by that constant delay. Audio thread only. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <vector>

static const double PI_ = 3.14159265358979323846;

class VaderResampler {
public:
    static constexpr int HALF = 32;           /* taps on each side of the centre, in units of the (stretched) kernel */
    static constexpr int RING = 8192;         /* engine samples kept: more than 2 * HALF * stretch for any rate >= 8 kHz */
    static constexpr int QUANT = 256;         /* kernel table points per unit */

    VaderResampler() { build_table(); build_poly(); reset(44100.0); }

    void reset(double hostRate) {
        host_ = hostRate;
        ratio_ = 44100.0 / hostRate;                      /* engine samples per output sample */
        stretch_ = ratio_ > 1.0 ? ratio_ : 1.0;
        radius_ = (int)ceil(HALF * stretch_);
        bypass_ = fabs(hostRate - 44100.0) < 0.5;
        memset(l_, 0, sizeof l_);
        memset(r_, 0, sizeof r_);
        produced_ = 0;
        pos_ = 0.0;
    }
    bool bypass() const { return bypass_; }
    double ratio() const { return ratio_; }
    /* the delay in host frames */
    uint32_t latency() const { return bypass_ ? 0u : (uint32_t)ceil(radius_ / ratio_); }

    /* engine samples produced so far, and the number the next output sample needs produced */
    int64_t produced() const { return produced_; }
    int64_t need_for_next() const { return (int64_t)floor(pos_) + 1; }
    /* where output sample (current + k) sits on the engine's clock */
    double engine_time(int64_t k) const { return pos_ + (double)k * ratio_; }
    void push(const float *l, const float *r, int n) {
        for (int i = 0; i < n; i++) {
            const int64_t at = (produced_ + i) & (RING - 1);
            l_[at] = l[i];
            r_[at] = r[i];
        }
        produced_ += n;
    }
    /* one output sample; the engine must have produced need_for_next() samples */
    void pull(float *outl, float *outr) {
        if (stretch_ == 1.0) { pull_fast(outl, outr); return; }
        const double t = pos_ - (double)radius_;          /* the centre, on the engine's clock */
        const int64_t i0 = (int64_t)floor(t);
        double sl = 0, sr = 0, sw = 0;
        const double inv = 1.0 / stretch_;
        for (int64_t i = i0 - radius_ + 1; i <= i0 + radius_; i++) {
            const double u = ((double)i - t) * inv;       /* in kernel units */
            const double a = fabs(u) * QUANT;
            if (a >= HALF * QUANT) continue;
            const int k = (int)a;
            const double f = a - k;
            const double w = tab_[k] + (tab_[k + 1] - tab_[k]) * f;
            if (i < 0) { sw += w; continue; }             /* before the first sample: silence */
            const int64_t at = i & (RING - 1);
            sl += w * l_[at];
            sr += w * r_[at];
            sw += w;
        }
        const double g = sw > 1e-9 ? 1.0 / sw : 1.0;
        *outl = (float)(sl * g);
        *outr = (float)(sr * g);
        pos_ += ratio_;
    }

private:
    static constexpr int PHASES = 512;        /* the fast path's sub-sample positions (linear between them) */
    static constexpr int TAPS = 2 * HALF;

    /* host rate >= 44100: 64 taps from a polyphase table, each row summing to 1 */
    void pull_fast(float *outl, float *outr) {
        const double t = pos_ - (double)radius_;
        const int64_t i0 = (int64_t)floor(t);
        const float ph = (float)((t - (double)i0) * PHASES);
        const int pi = (int)ph;
        const float a = ph - (float)pi;
        const float *r0 = &poly_[(size_t)pi * TAPS], *r1 = r0 + TAPS;
        float xl[TAPS], xr[TAPS];
        const int64_t first = i0 - HALF + 1;
        const int s = (int)(first & (RING - 1));
        if (s + TAPS <= RING) {
            memcpy(xl, &l_[s], sizeof xl);
            memcpy(xr, &r_[s], sizeof xr);
        } else {
            const int n1 = RING - s;
            memcpy(xl, &l_[s], sizeof(float) * (size_t)n1);
            memcpy(xl + n1, &l_[0], sizeof(float) * (size_t)(TAPS - n1));
            memcpy(xr, &r_[s], sizeof(float) * (size_t)n1);
            memcpy(xr + n1, &r_[0], sizeof(float) * (size_t)(TAPS - n1));
        }
        float sl = 0, sr = 0;
        for (int k = 0; k < TAPS; k++) {
            const float w = r0[k] + (r1[k] - r0[k]) * a;
            sl += w * xl[k];
            sr += w * xr[k];
        }
        *outl = sl;
        *outr = sr;
        pos_ += ratio_;
    }
    void build_poly() {
        poly_.assign((size_t)(PHASES + 1) * TAPS, 0.f);
        for (int p = 0; p <= PHASES; p++) {
            const double frac = (double)p / PHASES;
            double row[TAPS], sum = 0;
            for (int k = 0; k < TAPS; k++) {
                const double d = (double)(k - HALF + 1) - frac;   /* tap position minus the centre, in samples */
                const double a = fabs(d) * QUANT;
                double w = 0;
                if (a < HALF * QUANT) {
                    const int q = (int)a;
                    w = tab_[(size_t)q] + (tab_[(size_t)q + 1] - tab_[(size_t)q]) * (a - q);
                }
                row[k] = w;
                sum += w;
            }
            for (int k = 0; k < TAPS; k++) poly_[(size_t)p * TAPS + (size_t)k] = (float)(row[k] / sum);
        }
    }
    void build_table() {
        tab_.assign((size_t)(HALF * QUANT + 2), 0.0);
        const double beta = 9.0;
        auto i0 = [](double x) {
            double s = 1, t = 1;
            for (int k = 1; k < 40; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
            return s;
        };
        const double d = i0(beta);
        for (int k = 0; k <= HALF * QUANT; k++) {
            const double u = (double)k / QUANT;
            const double s = u < 1e-9 ? 1.0 : sin(PI_ * u) / (PI_ * u);
            const double r = u / HALF;
            const double w = r >= 1 ? 0.0 : i0(beta * sqrt(1 - r * r)) / d;
            tab_[(size_t)k] = s * w;
        }
    }
    double host_ = 44100, ratio_ = 1, stretch_ = 1, pos_ = 0;
    int radius_ = HALF;
    bool bypass_ = true;
    int64_t produced_ = 0;
    float l_[RING], r_[RING];
    std::vector<double> tab_;
    std::vector<float> poly_;
};
