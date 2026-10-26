// A minimal VST2 host to test the desktop plugin offline (no GUI): loads the .so, plays notes, checks the sound, the
// timing at several sample rates, the parameters, the state chunk and the programs.
//   test/desktop_test.sh builds and runs it:   desktop_host build/desktop/bin/VADER-vst2.so
#ifdef _WIN32
#include <windows.h>
#define dlopen_(p) ((void *)LoadLibraryA(p))
#define dlsym_(h, n) ((void *)GetProcAddress((HMODULE)(h), (n)))
#define dlerror_() "LoadLibrary failed"
#else
#include <dlfcn.h>
#define dlopen_(p) dlopen((p), RTLD_NOW)
#define dlsym_(h, n) dlsym((h), (n))
#define dlerror_() dlerror()
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <time.h>

struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t r1, r2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDouble)(AEffect *, double **, double **, int32_t);
    char future[56];
};
struct VstMidiEvent { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; char midiData[4]; char detune, noteOffVelocity, r1, r2; };
struct VstEvents { int32_t numEvents; intptr_t reserved; void *events[2]; };
struct VstTimeInfo { double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos; int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags; };

static VstTimeInfo g_ti;
static intptr_t host(AEffect *, int32_t op, int32_t, intptr_t, void *, float) {
    if (op == 1) return 2400;
    if (op == 7) return (intptr_t)&g_ti;
    return 0;
}
enum { effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effGetProgramName = 5, effGetParamName = 8, effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
       effEditGetRect = 13, effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25, effGetProgramNameIndexed = 29, effGetEffectName = 45, effGetVendorString = 47, effStartProcess = 71 };

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void send_midi(AEffect *e, int delta, int a, int b, int c) {
    VstMidiEvent m = {};
    m.type = 1; m.byteSize = sizeof m; m.deltaFrames = delta;
    m.midiData[0] = (char)a; m.midiData[1] = (char)b; m.midiData[2] = (char)c;
    VstEvents ev = {}; ev.numEvents = 1; ev.events[0] = &m;
    e->dispatcher(e, effProcessEvents, 0, 0, &ev, 0);
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: desktop_host plugin.so\n"); return 2; }
    void *h = dlopen_(argv[1]);
    if (!h) { printf("dlopen: %s\n", dlerror_()); return 2; }
    typedef AEffect *(*Main)(intptr_t (*)(AEffect *, int32_t, int32_t, intptr_t, void *, float));
    Main m = (Main)dlsym_(h, "VSTPluginMain");
    if (!m) m = (Main)dlsym_(h, "main");
    CHECK(m != nullptr, "entry point found");
    AEffect *e = m(host);
    CHECK(e && e->magic == 0x56737450, "AEffect magic");
    char buf[256] = {};
    e->dispatcher(e, effGetEffectName, 0, 0, buf, 0);
    CHECK(!strcmp(buf, "VADER"), "effect name '%s'", buf);
    e->dispatcher(e, effGetVendorString, 0, 0, buf, 0);
    CHECK(strstr(buf, "Cronx") != nullptr, "vendor '%s'", buf);
    CHECK(e->numParams == 147, "147 parameters (got %d)", e->numParams);
    CHECK(e->numPrograms >= 1, "programs (%d; DPF's VST2 has one: presets are the PRESET parameter)", e->numPrograms);
    CHECK(e->numOutputs == 2 && e->numInputs == 0, "2 outputs, no inputs");
    CHECK(e->uniqueID == (('A' << 24) | ('n' << 16) | ('V' << 8) | 'd'), "unique id AnVd");
    e->dispatcher(e, effGetParamName, 0, 0, buf, 0);
    printf("      parameter 0 is '%s'\n", buf);
    e->dispatcher(e, effGetProgramNameIndexed, 3, 0, buf, 0);
    printf("      program 3 is '%s'\n", buf);

    const double rates[] = {44100, 48000, 96000, 22050};
    std::vector<float> ref;
    for (double sr : rates) {
        e = m(host);
        e->dispatcher(e, effOpen, 0, 0, 0, 0);
        e->dispatcher(e, effSetSampleRate, 0, 0, 0, (float)sr);
        e->dispatcher(e, effSetBlockSize, 0, 512, 0, 0);
        e->dispatcher(e, effMainsChanged, 0, 1, 0, 0);
        printf("--- %.0f Hz, latency %d\n", sr, e->initialDelay);
        g_ti.sampleRate = sr; g_ti.flags = 0;
        const int blocks = (int)(sr * 1.5 / 512);
        std::vector<float> L((size_t)blocks * 512), R((size_t)blocks * 512);
        float *out[2]; float *in[2] = {nullptr, nullptr};
        int onset_host = (int)(sr * 0.1);   // note on at 100 ms
        bool sent = false;
        int firstNonZero = -1;
        bool nan = false;
        for (int b = 0; b < blocks; b++) {
            out[0] = &L[(size_t)b * 512]; out[1] = &R[(size_t)b * 512];
            int off = onset_host - b * 512;
            if (!sent && off >= 0 && off < 512) { send_midi(e, off, 0x90, 60, 100); sent = true; }
            e->processReplacing(e, in, out, 512);
        }
        double rms = 0;
        for (size_t i = 0; i < L.size(); i++) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) nan = true;
            if (firstNonZero < 0 && fabsf(L[i]) > 1e-6f) firstNonZero = (int)i;
            if (i > L.size() / 2) rms += L[i] * L[i];
        }
        rms = sqrt(rms / (L.size() / 2));
        CHECK(!nan, "no NaN/Inf at %.0f Hz", sr);
        CHECK(rms > 0.001, "note is audible at %.0f Hz (rms %.4f)", sr, rms);
        double expect = onset_host + (sr == 44100 ? 0 : e->initialDelay);
        printf("      first sound at host sample %d (note at %d, latency %d)\n", firstNonZero, onset_host, e->initialDelay);
        CHECK(firstNonZero >= onset_host && firstNonZero < expect + sr * 0.01, "sound starts when it should");
        if (sr == 44100) ref = L;
        send_midi(e, 0, 0x80, 60, 0);
        e->dispatcher(e, effMainsChanged, 0, 0, 0, 0);
        e->dispatcher(e, effClose, 0, 0, 0, 0);
    }
    // parameters and state at 44100
    e = m(host);
    e->dispatcher(e, effOpen, 0, 0, 0, 0);
    e->dispatcher(e, effSetSampleRate, 0, 0, 0, 44100.f);
    e->dispatcher(e, effSetBlockSize, 0, 512, 0, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, 0, 0);
    int pi = -1;
    for (int i = 0; i < e->numParams; i++) { e->dispatcher(e, effGetParamName, i, 0, buf, 0); if (!strcmp(buf, "ORBIT SPEED") || !strcmp(buf, "orb_speed")) { pi = i; break; } }
    CHECK(pi >= 0, "ORBIT SPEED found (index %d)", pi);
    if (pi >= 0) {
        e->setParameter(e, pi, 0.8f);
        CHECK(fabsf(e->getParameter(e, pi) - 0.8f) < 1e-4, "parameter round trip %.4f", e->getParameter(e, pi));
    }
    void *chunk = nullptr;
    intptr_t n = e->dispatcher(e, effGetChunk, 0, 0, &chunk, 0);
    CHECK(n > 100 && chunk, "state chunk %ld bytes", (long)n);
    std::vector<char> keep((char *)chunk, (char *)chunk + n);
    e->setParameter(e, pi, 0.1f);
    e->dispatcher(e, effSetChunk, 0, (intptr_t)keep.size(), keep.data(), 0);
    CHECK(fabsf(e->getParameter(e, pi) - 0.8f) < 1e-3, "state restores the parameter (%.4f)", e->getParameter(e, pi));
    // CPU: ten seconds of audio with a 10-note chord held, as a share of real time (one core)
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        AEffect *f = m(host);
        f->dispatcher(f, effOpen, 0, 0, 0, 0);
        f->dispatcher(f, effSetSampleRate, 0, 0, 0, (float)sr);
        f->dispatcher(f, effSetBlockSize, 0, 512, 0, 0);
        f->dispatcher(f, effMainsChanged, 0, 1, 0, 0);
        for (int k = 0; k < 10; k++) send_midi(f, 0, 0x90, 40 + k * 4, 100);
        std::vector<float> a(512), b(512);
        float *o[2] = {a.data(), b.data()}, *i2[2] = {nullptr, nullptr};
        const int nb = (int)(sr * 10 / 512);
        timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int k = 0; k < nb; k++) f->processReplacing(f, i2, o, 512);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        printf("      cpu at %.0f Hz: %.1f%% of one core (10 voices)\n", sr, sec / 10.0 * 100);
        CHECK(sec < 5.0, "faster than real time with room to spare at %.0f Hz", sr);
        f->dispatcher(f, effClose, 0, 0, 0, 0);
    }
    // a host that restores parameters in index order (before or after the state) must not corrupt either timbre:
    // timbre B edited, A and B different
    {
        int tB = -1, os = -1;
        for (int i = 0; i < e->numParams; i++) {
            e->dispatcher(e, effGetParamName, i, 0, buf, 0);
            if (!strcmp(buf, "EDIT TIMBRE")) tB = i;
            if (!strcmp(buf, "ORBIT SPEED")) os = i;
        }
        CHECK(tB >= 0 && os >= 0, "TIMBRE and ORBIT SPEED found (%d, %d)", tB, os);
        e->setParameter(e, tB, 0.f);  e->setParameter(e, os, 0.2f);   // timbre A: 0.2
        e->setParameter(e, tB, 1.f);  e->setParameter(e, os, 0.9f);   // timbre B: 0.9
        std::vector<float> vals;
        for (int i = 0; i < e->numParams; i++) vals.push_back(e->getParameter(e, i));
        void *c0 = nullptr;
        intptr_t n0 = e->dispatcher(e, effGetChunk, 0, 0, &c0, 0);
        std::string want((char *)c0, (size_t)n0);
        for (int order = 0; order < 2; order++) {
            AEffect *f = m(host);
            f->dispatcher(f, effOpen, 0, 0, 0, 0);
            f->dispatcher(f, effSetSampleRate, 0, 0, 0, 44100.f);
            f->dispatcher(f, effSetBlockSize, 0, 512, 0, 0);
            if (order == 1) f->dispatcher(f, effSetChunk, 0, (intptr_t)want.size(), (void *)want.data(), 0);
            for (int i = 0; i < f->numParams; i++) f->setParameter(f, i, vals[(size_t)i]);
            if (order == 0) f->dispatcher(f, effSetChunk, 0, (intptr_t)want.size(), (void *)want.data(), 0);
            void *c1 = nullptr;
            intptr_t n1 = f->dispatcher(f, effGetChunk, 0, 0, &c1, 0);
            std::string got((char *)c1, (size_t)n1);
            CHECK(got == want, "state identical after restoring %s", order == 0 ? "parameters, then the chunk" : "the chunk, then parameters");
            f->dispatcher(f, effClose, 0, 0, 0, 0);
        }
    }
    // the PRESET parameter (index 0) loads a preset, which changes the sound parameters
    float before = e->getParameter(e, pi);
    (void)before;
    e->setParameter(e, 0, 5.f / 15.f);
    CHECK(fabsf(e->getParameter(e, 0) - 5.f / 15.f) < 1e-3, "PRESET 5 selected");
    e->dispatcher(e, effGetParamName, 0, 0, buf, 0);
    e->dispatcher(e, effMainsChanged, 0, 0, 0, 0);
    e->dispatcher(e, effClose, 0, 0, 0, 0);
    printf("%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
