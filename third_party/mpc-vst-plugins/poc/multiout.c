/* Minimal VST2 mono synth PoC: saw -> resonant lowpass -> amp env. Tests MIDI into a plugin. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process)(AEffect*, float**, float**, int32_t);
    void (*setParameter)(AEffect*, int32_t, float);
    float (*getParameter)(AEffect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect*, float**, float**, int32_t);
    void (*processDoubleReplacing)(AEffect*, double**, double**, int32_t);
    char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[2]; } VstEvents;

enum { effOpen=0, effClose=1, effGetParamLabel=6, effGetParamDisplay=7, effGetParamName=8,
       effSetSampleRate=10, effProcessEvents=25, effCanBeAutomated=26, effGetPlugCategory=35,
       effGetEffectName=45, effGetVendorString=47, effGetProductString=48, effGetVendorVersion=49,
       effCanDo=51, effGetVstVersion=58 };


/* Multi-output spike: 8 outputs (stereo pair + 6 mono), one sine per output at a distinct pitch while any note is held.
 * Question it answers: does MPC's plugin host expose >2 outputs of a VST2 instrument (pluginList numOutputs="8")? */
#define NOUT 8
static float sr = 44100.0f, ph[NOUT];
static void lg(const char *f, ...) { FILE *fp = fopen("/tmp/multiout.log", "a"); if (!fp) return; va_list a; va_start(a, f); vfprintf(fp, f, a); va_end(a); fclose(fp); }
static unsigned seen[8];
static int held;
static const float hz[NOUT] = { 220, 330, 440, 523.25f, 659.25f, 784, 880, 1046.5f };
static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    (void)e; (void)v;
    if (op >= 0 && op < 256 && !(seen[op >> 5] & (1u << (op & 31)))) { seen[op >> 5] |= 1u << (op & 31); lg("opcode %d idx %d v %ld o %f\n", op, idx, (long)v, (double)o); }
    switch (op) {
    case effGetPlugCategory: return 2;
    case effGetEffectName: case effGetProductString: strcpy(p, "MPC MultiOut Spike"); return 1;
    case effGetVendorString: strcpy(p, "sd88me"); return 1;
    case effGetVendorVersion: return 1000;
    case effGetVstVersion: return 2400;
    case effSetSampleRate: sr = o; return 1;
    case 33: /* effGetOutputProperties */ {
        struct { char label[64]; int32_t flags, arr; char shortLabel[8]; char future[48]; } *pp = p;
        memset(pp, 0, sizeof *pp);
        snprintf(pp->label, 64, idx < 2 ? "Main %c" : "Track %d", idx < 2 ? "LR"[idx] : idx - 1);
        snprintf(pp->shortLabel, 8, "O%d", idx + 1);
        pp->flags = 1 | (idx < 2 ? 2 : 0); /* kVstPinIsActive | kVstPinIsStereo (pair) */
        pp->arr = idx < 2 ? 1 : 0;         /* kSpeakerArrStereo / mono-ish */
        return 1;
    }
    case effProcessEvents: {
        VstEvents *ev = p;
        for (int i = 0; i < ev->numEvents; i++)
            if (ev->events[i]->type == 1) {
                unsigned char *m = ((VstMidiEvent *)ev->events[i])->midiData;
                if ((m[0] & 0xf0) == 0x90 && m[2]) held++;
                else if ((m[0] & 0xf0) == 0x80 || (m[0] & 0xf0) == 0x90) { if (held > 0) held--; }
            }
        return 1;
    }
    case effCanDo: return (!strcmp(p, "receiveVstEvents") || !strcmp(p, "receiveVstMidiEvent")) ? 1 : -1;
    case effOpen: case effClose: return 1;
    default: return 0;
    }
}
static void setParameter(AEffect *e, int32_t i, float v) { (void)e; (void)i; (void)v; }
static float getParameter(AEffect *e, int32_t i) { (void)e; (void)i; return 0; }
static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    (void)e; (void)in;
    static int once;
    if (!once++) { lg("processReplacing n=%d in=%p out=%p\n", n, (void *)in, (void *)out); for (int c = 0; c < NOUT; c++) lg("  out[%d]=%p\n", c, (void *)out[c]); }
    for (int32_t i = 0; i < n; i++)
        for (int c = 0; c < NOUT; c++) {
            if (!out[c]) continue;
            ph[c] += hz[c] / sr; if (ph[c] >= 1) ph[c] -= 1;
            out[c][i] = held > 0 ? 0.25f * sinf(6.2831853f * ph[c]) : 0;
        }
}
static AEffect fx;
__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    memset(&fx, 0, sizeof fx);
    fx.magic = 0x56737450;
    fx.dispatcher = dispatcher; fx.setParameter = setParameter; fx.getParameter = getParameter;
    fx.processReplacing = processReplacing;
    fx.numParams = 1; fx.numInputs = 0; fx.numOutputs = NOUT;
    fx.flags = (1 << 4) | (1 << 8);
    fx.uniqueID = 0x4d754f75; /* 'MuOu' */
    fx.version = 1000;
    fx.user = (void *)m;
    return &fx;
}
