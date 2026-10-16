/* Minimal VST2 mono synth PoC: saw -> resonant lowpass -> amp env. Tests MIDI into a plugin. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/syscall.h>

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



/* Shared-engine spike: does every instance of this plugin in MPC live in one process and one copy of this library's statics?
 * Each instance takes an id from a process-wide counter, plays a sine at 220*(id+1) Hz while a note is held (stereo, both
 * channels), and logs (id, pid, tid, time) for its first calls to /tmp/shared.log. */
static volatile int mu;
#define LOCK() while (__sync_lock_test_and_set(&mu, 1)) {}
#define UNLOCK() __sync_lock_release(&mu)
static int g_next, g_live;
static void lg(const char *f, ...) { LOCK(); FILE *fp = fopen("/tmp/shared.log", "a"); if (fp) { va_list a; va_start(a, f); vfprintf(fp, f, a); va_end(a); fclose(fp); } UNLOCK(); }
typedef struct { int id, held, calls; float ph, sr; } inst_t;
static inst_t *mk(void) { inst_t *i = calloc(1, sizeof *i); LOCK(); i->id = g_next++; g_live++; UNLOCK(); i->sr = 44100; return i; }
static long long now_us(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec * 1000000LL + t.tv_usec; }

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    inst_t *i = e->object; (void)v; (void)idx;
    switch (op) {
    case effOpen: lg("open id=%d pid=%d tid=%ld live=%d\n", i->id, (int)getpid(), (long)syscall(SYS_gettid), g_live); return 1;
    case effClose: LOCK(); g_live--; UNLOCK(); lg("close id=%d live=%d\n", i->id, g_live); free(i); e->object = NULL; return 1;
    case effGetPlugCategory: return 2;
    case effGetEffectName: case effGetProductString: strcpy(p, "MPC SharedEngine Spike"); return 1;
    case effGetVendorString: strcpy(p, "sd88me"); return 1;
    case effGetVendorVersion: return 1000;
    case effGetVstVersion: return 2400;
    case effSetSampleRate: i->sr = o; return 1;
    case effProcessEvents: {
        VstEvents *ev = p;
        for (int k = 0; k < ev->numEvents; k++)
            if (ev->events[k]->type == 1) {
                unsigned char *m = ((VstMidiEvent *)ev->events[k])->midiData;
                if ((m[0] & 0xf0) == 0x90 && m[2]) i->held++;
                else if ((m[0] & 0xf0) == 0x80 || (m[0] & 0xf0) == 0x90) { if (i->held > 0) i->held--; }
            }
        return 1;
    }
    case effCanDo: return (!strcmp(p, "receiveVstEvents") || !strcmp(p, "receiveVstMidiEvent")) ? 1 : -1;
    default: return 0;
    }
}
static void setParameter(AEffect *e, int32_t i, float v) { (void)e; (void)i; (void)v; }
static float getParameter(AEffect *e, int32_t i) { (void)e; (void)i; return 0; }
static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    inst_t *i = e->object; (void)in;
    if (i->calls < 6 || i->calls == 2000) lg("proc id=%d call=%d n=%d tid=%ld t=%lld us live=%d\n", i->id, i->calls, n, (long)syscall(SYS_gettid), now_us(), g_live);
    i->calls++;
    float hz = 220.0f * (float)(i->id + 1);
    for (int32_t k = 0; k < n; k++) {
        i->ph += hz / i->sr; if (i->ph >= 1) i->ph -= 1;
        float x = i->held > 0 ? 0.25f * sinf(6.2831853f * i->ph) : 0;
        for (int c = 0; c < 8; c++) out[c][k] = c < 2 ? x : 0;
    }
}
__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback m) {
    AEffect *fx = calloc(1, sizeof *fx);
    fx->magic = 0x56737450;
    fx->dispatcher = dispatcher; fx->setParameter = setParameter; fx->getParameter = getParameter;
    fx->processReplacing = processReplacing;
    fx->numParams = 1; fx->numInputs = 0; fx->numOutputs = 8;
    fx->flags = (1 << 4) | (1 << 8);
    fx->uniqueID = 0x4d754f75;
    fx->version = 1000;
    fx->user = (void *)m;
    fx->object = mk();
    return fx;
}
