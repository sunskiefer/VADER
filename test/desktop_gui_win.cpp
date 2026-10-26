// Opens the desktop plugin's editor in a bare X11 window (for screenshots under xvfb) while playing a held chord, so the
// orbit screen moves. Usage: desktop_gui plugin.so seconds [width height]
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

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
struct ERect { short top, left, bottom, right; };
static intptr_t host(AEffect *, int32_t op, int32_t, intptr_t, void *, float) { return op == 1 ? 2400 : 0; }
static void midi(AEffect *e, int a, int b, int c) {
    VstMidiEvent m = {};
    m.type = 1; m.byteSize = sizeof m;
    m.midiData[0] = (char)a; m.midiData[1] = (char)b; m.midiData[2] = (char)c;
    VstEvents ev = {}; ev.numEvents = 1; ev.events[0] = &m;
    e->dispatcher(e, 25, 0, 0, &ev, 0);
}
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM a, LPARAM l) { return DefWindowProcA(h, m, a, l); }
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary failed\n"); return 2; }
    typedef AEffect *(*Main)(intptr_t (*)(AEffect *, int32_t, int32_t, intptr_t, void *, float));
    Main m = (Main)GetProcAddress(h, "VSTPluginMain");
    AEffect *e = m(host);
    e->dispatcher(e, 0, 0, 0, 0, 0);
    e->dispatcher(e, 10, 0, 0, 0, 48000.f);
    e->dispatcher(e, 11, 0, 512, 0, 0);
    e->dispatcher(e, 12, 0, 1, 0, 0);
    ERect *r = nullptr;
    e->dispatcher(e, 13, 0, 0, &r, 0);
    int w = r ? r->right - r->left : 1280, hh = r ? r->bottom - r->top : 668;
    WNDCLASSA wc = {}; wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "vhost";
    RegisterClassA(&wc);
    RECT rc = {0, 0, w, hh};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND win = CreateWindowExA(0, "vhost", "VADER", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, rc.right - rc.left, rc.bottom - rc.top, 0, 0, wc.hInstance, 0);
    ShowWindow(win, SW_SHOW);
    printf("window %dx%d\n", w, hh); fflush(stdout);
    e->dispatcher(e, 14, 0, 0, (void *)win, 0);
    midi(e, 0x90, 48, 110); midi(e, 0x90, 55, 100); midi(e, 0x90, 64, 100);
    std::vector<float> L(512), R(512);
    float *out[2] = {L.data(), R.data()}, *in[2] = {nullptr, nullptr};
    double secs = atof(argv[2]);
    DWORD t0 = GetTickCount();
    while ((GetTickCount() - t0) / 1000.0 < secs) {
        e->processReplacing(e, in, out, 512);
        e->dispatcher(e, 19, 0, 0, 0, 0);
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        Sleep(10);
    }
    e->dispatcher(e, 15, 0, 0, 0, 0);
    e->dispatcher(e, 12, 0, 0, 0, 0);
    e->dispatcher(e, 1, 0, 0, 0, 0);
    return 0;
}
