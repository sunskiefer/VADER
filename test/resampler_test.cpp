// The desktop plugin's sample-rate converter on its own: a sine in at 44100 Hz, out at the host's rate; how clean is it?
// (best-fit sine removed; what is left is the converter's error). Usage: build/test/resampler_test
#include "../desktop/Resampler.hpp"
#include <stdio.h>
int main() {
    int fails = 0;
    const double rates[] = {48000, 88200, 96000, 192000, 32000, 22050};
    const double freqs[] = {1000, 8000, 15000, 18000};
    for (double sr : rates) {
        VaderResampler r;
        r.reset(sr);
        for (double f : freqs) {
            if (f > sr * 0.45) continue;
            r.reset(sr);
            const int N = (int)(sr * 0.5);
            std::vector<double> out;
            std::vector<float> l(128), rr(128);
            int64_t in_pos = 0;
            for (int j = 0; j < N; j++) {
                while (r.produced() < r.need_for_next()) {
                    for (int i = 0; i < 128; i++) { l[i] = rr[i] = (float)sin(2 * 3.14159265358979 * f * (double)(in_pos + i) / 44100.0); }
                    in_pos += 128;
                    r.push(l.data(), rr.data(), 128);
                }
                float a, b;
                r.pull(&a, &b);
                out.push_back(a);
            }
            // the output sample j is the input's value at time j*ratio - radius (input samples): fit a sine of frequency f
            const int lat = (int)r.latency();
            // least squares fit of A cos + B sin (normal equations: exact whatever the number of cycles)
            double scc = 0, sss = 0, scs = 0, sxc = 0, sxs = 0;
            for (int j = lat + 2000; j < N; j++) {
                const double t = (double)j / sr, w = 2 * 3.14159265358979 * f * t;
                const double cc = cos(w), sn = sin(w);
                scc += cc * cc; sss += sn * sn; scs += cc * sn; sxc += out[(size_t)j] * cc; sxs += out[(size_t)j] * sn;
            }
            const double det = scc * sss - scs * scs;
            const double A = (sxc * sss - sxs * scs) / det, B = (sxs * scc - sxc * scs) / det;
            double err = 0, sig = 0;
            for (int j = lat + 2000; j < N; j++) {
                const double t = (double)j / sr, w = 2 * 3.14159265358979 * f * t;
                const double fit = A * cos(w) + B * sin(w);
                err += (out[(size_t)j] - fit) * (out[(size_t)j] - fit);
                sig += fit * fit;
            }
            const double snr = 10 * log10(sig / (err + 1e-30));
            const double amp = sqrt(A * A + B * B);
            printf("%s %7.0f Hz -> %6.0f: tone %6.0f Hz, level %.4f, error %.1f dB below\n", snr > 80 && fabs(amp - 1) < 0.01 ? "ok:  " : "FAIL:", 44100.0, sr, f, amp, snr);
            if (!(snr > 80 && fabs(amp - 1) < 0.01)) fails++;
        }
    }
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
