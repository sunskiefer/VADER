#include "VaderPlugin.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include "paths.hpp"
#include "DistrhoPluginUtils.hpp"
#include <string>
#include <fstream>
extern "C" {
#include "vader_params.h"   /* the engine's parameter table: PLUG speaks in its indices */
}

START_NAMESPACE_DISTRHO

/* the engine writes and reads numbers with printf / strtod: always in the "C" locale, whatever the host set */
#if defined(__GLIBC__) || defined(__APPLE__)
#ifdef __APPLE__
#include <xlocale.h>
#endif
struct CLocale {
    locale_t prev;
    CLocale() { static locale_t c = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0); prev = uselocale(c); }
    ~CLocale() { uselocale(prev); }
};
#else
struct CLocale {};
#endif

static void spin_lock(std::atomic_flag &f) { while (f.test_and_set(std::memory_order_acquire)) {} }
static void spin_unlock(std::atomic_flag &f) { f.clear(std::memory_order_release); }

VaderPlugin::VaderPlugin() : Plugin(VG_NEXP, 16, 1), fEng(mpc_engine()), fInst(nullptr), fBus(nullptr), fVersion(1) {
    std::string dir = vaderShapesDir();
    {   // the example samples travel inside the plugin: <bundle>/Contents/Resources/Samples (VST3), or a Samples / VADER Samples folder beside the file (VST2)
        std::string bin = DISTRHO_NAMESPACE::getBinaryFilename();
        for (char &c : bin)
            if (c == '\\') c = '/';
        std::string list;
        auto up = [](std::string p) { size_t i = p.find_last_of('/'); return i == std::string::npos ? std::string() : p.substr(0, i); };
        const std::string d1 = up(bin), d2 = up(d1);
        auto add = [&](const std::string &p) { if (!p.empty()) list += (list.empty() ? "" : "|") + p; };
        add(d2 + "/Resources/Samples");
        add(d1 + "/Samples");
        add(d1 + "/VADER Samples");
        add(d1 + "/Resources/Samples");
#ifdef _WIN32
        _putenv_s("VADER_SAMPLES_EXTRA", list.c_str());
#else
        setenv("VADER_SAMPLES_EXTRA", list.c_str(), 1);
#endif
    }
    {
        CLocale c;
        fInst = fEng->create(dir.empty() ? nullptr : dir.c_str());
    }
    DISTRHO_SAFE_ASSERT(fInst != nullptr);
    if (fInst) fBus = (overlay_t *)vader_view_bus(fInst);
    fReadout[0] = 0;
    for (auto &t : fTexts) t[0] = 0;
    for (int i = 0; i < VG_NEXP; i++) {
        fValues[i].store(VG_PARS[i].def);
        fTrigState[i] = 0;
    }
    fLastPoll = std::chrono::steady_clock::now();
    for (int i = 0; i < VG_NEXP; i++) fExp2Eng[i] = -1;
    for (int k = 0; k < NP; k++) if (VG_ENG2EXP[k] >= 0 && VG_ENG2EXP[k] < VG_NEXP) fExp2Eng[VG_ENG2EXP[k]] = (int16_t)k;
    if (!dir.empty()) {   /* the Force's address, remembered next to the Shapes folder */
        size_t sl = dir.find_last_of('/');
        fAddrFile = (sl == std::string::npos ? dir : dir.substr(0, sl)) + "/force_address.txt";
        std::ifstream f(fAddrFile);
        if (f) { std::getline(f, fForceAddr); while (!fForceAddr.empty() && (fForceAddr.back() == '\r' || fForceAddr.back() == '\n' || fForceAddr.back() == ' ')) fForceAddr.pop_back(); }
    }
    if (!fInst) return;
    for (int i = 0; i < 16; i++) {
        char k[24], b[96];
        snprintf(k, sizeof k, "preset:%d", i);
        fPresetNames[i] = engGet(k, b, sizeof b) ? b : "PRESET";
    }
    fRes.reset(getSampleRate());
    setLatency(fRes.latency());
    std::lock_guard<std::mutex> g(fMutex);
    primeAll();
}

VaderPlugin::~VaderPlugin() {
    if (fInst) fEng->destroy(fInst);
}

/* ---------------------------------------------------------------------------------------------- engine helpers */
void VaderPlugin::engSet(const char *key, const char *val) {
    CLocale c;
    fEng->set_param(fInst, key, val);
}

bool VaderPlugin::engGet(const char *key, char *buf, int n) const {
    CLocale c;
    buf[0] = 0;
    int r = fEng->get_param(fInst, key, buf, n);
    if (r <= 0) return false;
    buf[n - 1] = 0;
    return true;
}

void VaderPlugin::setTextLocked(char *dst, const char *src) {
    snprintf(dst, 96, "%s", src);
}

void VaderPlugin::primeAll() {
    char b[96];
    for (int i = 0; i < VG_NEXP; i++) {
        if (VG_PARS[i].kind == VG_TRIG) continue;
        if (engGet(VG_PARS[i].key, b, sizeof b)) fValues[i].store(strtof(b, nullptr), std::memory_order_relaxed);
    }
    pollLocked();
    bumpVersion();
}

void VaderPlugin::pollLocked() {
    char buf[16384];
    bool changed = false;
    if (engGet("dirty", buf, sizeof buf)) {
        const char *p = buf;
        char b[96];
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            char *end = nullptr;
            long k = strtol(p, &end, 10);
            if (end == p) break;
            p = end;
            if (k < 0 || k >= VG_NENG) continue;
            const int e = VG_ENG2EXP[k];
            if (e < 0 || VG_PARS[e].kind == VG_TRIG) continue;
            if (!engGet(VG_PARS[e].key, b, sizeof b)) continue;
            const float v = strtof(b, nullptr);
            if (v != fValues[e].load(std::memory_order_relaxed)) {
                fValues[e].store(v, std::memory_order_relaxed);
                changed = true;
            }
        }
    }
    char line[96], t[4][96];
    if (!engGet("readout", line, sizeof line)) line[0] = 0;
    for (int s = 0; s < 4; s++) {
        char k[64];
        snprintf(k, sizeof k, "%s_display", VG_PARS[VG_TEXT_PARS[s]].key);
        if (!engGet(k, t[s], sizeof t[s])) t[s][0] = 0;
    }
    spin_lock(fTextLock);
    setTextLocked(fReadout, line);
    for (int s = 0; s < 4; s++) setTextLocked(fTexts[s], t[s]);
    spin_unlock(fTextLock);
    if (changed) bumpVersion();
    fLastPoll = std::chrono::steady_clock::now();
}

void VaderPlugin::uiPoll() {
    if (!fInst) return;
    if (std::chrono::steady_clock::now() - fLastPoll < std::chrono::milliseconds(60)) return;   /* run() is doing it */
    std::unique_lock<std::mutex> g(fMutex, std::try_to_lock);
    if (g.owns_lock()) pollLocked();
}

void VaderPlugin::uiReadout(char *buf, int n) const {
    spin_lock(fTextLock);
    snprintf(buf, (size_t)n, "%s", fReadout);
    spin_unlock(fTextLock);
}

void VaderPlugin::uiText(int slot, char *buf, int n) const {
    spin_lock(fTextLock);
    snprintf(buf, (size_t)n, "%s", fTexts[slot]);
    spin_unlock(fTextLock);
}

bool VaderPlugin::uiPresetName(int i, char *buf, int n) {
    if (i < 0 || i >= 16) return false;
    snprintf(buf, (size_t)n, "%s", fPresetNames[i].c_str());
    return true;
}

int VaderPlugin::uiCorner() const {
    const int c = (int)lroundf(uiValue(VGP_CORNER));
    return c < 0 ? 0 : c > 4 ? 4 : c;
}

void VaderPlugin::uiFire(int par) {
    if (!fInst || par < 0 || par >= VG_NEXP || VG_PARS[par].kind != VG_TRIG) return;
    std::lock_guard<std::mutex> g(fMutex);
    engSet(VG_PARS[par].key, "1");
    pollLocked();
    if (fExp2Eng[par] >= 0) fMirror.sendFire((uint16_t)fExp2Eng[par]);
}

/* ---------------------------------------------------------------------------------------------- PLUG */
void VaderPlugin::uiPlug(const std::string &address) {
    if (!address.empty() && address != fForceAddr) {
        fForceAddr = address;
        if (!fAddrFile.empty()) { std::ofstream f(fAddrFile); if (f) f << address << "\n"; }
    }
    fMirror.plug(address);
}

/* the Force's state and changes, applied to this engine as if the host had set them (so every text and name comes out the
 * same here); the host is not told (DPF has no DSP-to-host value path), the window reads fValues itself */
void VaderPlugin::mirrorApply() {
    bool any = false;
    if (fMirror.takeState(fStateIn) && !fStateIn.empty()) {
        engSet("state", fStateIn.c_str());
        primeAll();
        any = true;
    }
    if (fMirror.takeDeltas(fDeltas)) {
        char b[48];
        for (const MirrorClient::Delta &d : fDeltas) {
            if (d.idx >= NP) continue;
            snprintf(b, sizeof b, "%.9g", (double)d.val);
            engSet(VP[d.idx].key, b);
            const int e = VG_ENG2EXP[d.idx];
            if (e >= 0 && VG_PARS[e].kind != VG_TRIG) fValues[e].store(d.val, std::memory_order_relaxed);
        }
        any = true;
    }
    if (any) { pollLocked(); bumpVersion(); }
}

/* ---------------------------------------------------------------------------------------------- DPF: description */
void VaderPlugin::initParameter(uint32_t index, Parameter &p) {
    const VgPar &P = VG_PARS[index];
    p.hints = kParameterIsAutomatable;
    p.name = P.name;
    p.shortName = P.name;
    p.symbol = P.key;
    p.ranges.def = P.def;
    p.ranges.min = P.mn;
    p.ranges.max = P.mx;
    if (P.kind == VG_TRIG) {
        p.hints |= kParameterIsBoolean;   /* a plain on/off to the host: DPF's own "trigger" handling differs per format */
        p.ranges.def = 0;
        p.ranges.min = 0;
        p.ranges.max = 1;
    } else if (P.nopts) {
        p.hints |= kParameterIsInteger;
        if (P.nopts == 2) p.hints |= kParameterIsBoolean;
        p.enumValues.count = (uint8_t)P.nopts;
        p.enumValues.restrictedMode = true;
        p.enumValues.values = new ParameterEnumerationValue[P.nopts];
        p.enumValues.deleteLater = true;
        for (int i = 0; i < P.nopts; i++) {
            p.enumValues.values[i].label = P.opts[i];
            p.enumValues.values[i].value = (float)i;
        }
    } else if (P.isint) {
        p.hints |= kParameterIsInteger;
        if ((int)index == VGP_PRESET) {
            p.enumValues.count = 16;
            p.enumValues.restrictedMode = true;
            p.enumValues.values = new ParameterEnumerationValue[16];
            p.enumValues.deleteLater = true;
            for (int i = 0; i < 16; i++) {
                p.enumValues.values[i].label = fPresetNames[i].c_str();
                p.enumValues.values[i].value = (float)i;
            }
        }
    }
}

void VaderPlugin::initProgramName(uint32_t index, String &name) {
    name = index < 16 ? fPresetNames[index].c_str() : "PRESET";
}

void VaderPlugin::initState(uint32_t, State &state) {
    state.hints = kStateIsOnlyForDSP;
    state.key = "engine";
    state.defaultValue = "";
    state.label = "VADER state";
}

/* ---------------------------------------------------------------------------------------------- DPF: values and state */
float VaderPlugin::getParameterValue(uint32_t index) const {
    return fValues[index].load(std::memory_order_relaxed);
}

void VaderPlugin::setParameterValue(uint32_t index, float value) {
    if (!fInst || index >= (uint32_t)VG_NEXP) return;
    const VgPar &P = VG_PARS[index];
    std::lock_guard<std::mutex> g(fMutex);
    char b[48];
    if (P.kind == VG_TRIG) {
        const bool rise = value > 0.5f && fTrigState[index] <= 0.5f;   /* a press is the rise; holding it fires once */
        fTrigState[index] = value;
        fValues[index].store(value, std::memory_order_relaxed);
        if (rise) {
            engSet(P.key, "1");
            pollLocked();
            if (fExp2Eng[index] >= 0) fMirror.sendFire((uint16_t)fExp2Eng[index]);   /* PLUG: the Force presses it too */
        }
        return;
    }
    snprintf(b, sizeof b, "%.9g", (double)value);
    engSet(P.key, b);
    fValues[index].store(value, std::memory_order_relaxed);
    if (fExp2Eng[index] >= 0) fMirror.sendSet((uint16_t)fExp2Eng[index], value);   /* PLUG: and on the Force */
    /* these change the values the others show (a corner's or timbre's sound, a preset's everything) */
    if ((int)index == VGP_TIMBRE || (int)index == VGP_CORNER || (int)index == VGP_PRESET) pollLocked();
    bumpVersion();
}

void VaderPlugin::loadProgram(uint32_t index) {
    setParameterValue(VGP_PRESET, (float)index);
}

String VaderPlugin::getState(const char *key) const {
    if (!fInst || strcmp(key, "engine") != 0) return String();
    std::lock_guard<std::mutex> g(fMutex);
    std::string buf(256 * 1024, '\0');
    CLocale c;
    int n = fEng->get_param(fInst, "state", &buf[0], (int)buf.size());
    if (n <= 0) return String();
    buf[buf.size() - 1] = 0;
    return String(buf.c_str());
}

void VaderPlugin::setState(const char *key, const char *value) {
    if (!fInst || strcmp(key, "engine") != 0 || !value || !*value) return;
    std::lock_guard<std::mutex> g(fMutex);
    engSet("state", value);
    primeAll();
}

/* ---------------------------------------------------------------------------------------------- DPF: audio */
void VaderPlugin::activate() {
    std::lock_guard<std::mutex> g(fMutex);
    fRes.reset(getSampleRate());
    fEvHead = fEvTail = 0;
    if (fInst) {
        const uint8_t panic[3] = {0xB0, 123, 0};
        fEng->midi(fInst, panic, 3);
    }
}

void VaderPlugin::sampleRateChanged(double sr) {
    std::lock_guard<std::mutex> g(fMutex);
    fRes.reset(sr);
    fEvHead = fEvTail = 0;
    setLatency(fRes.latency());
}

void VaderPlugin::applyEvent(const Ev &e) {
    fEng->midi(fInst, e.d, e.n);
}

/* bring the engine's output up to `until` samples, applying each event at its own sample. It renders in pieces of up to
 * 128 and may go on to `limit` (what this host block needs in all): no event of a later block can be due before that,
 * so rendering ahead of the sample asked for is safe, and it is what keeps the engine's calls big. */
void VaderPlugin::produce(int64_t until, int64_t limit) {
    while (fRes.produced() < until) {
        const int64_t p = fRes.produced();
        while (fEvHead != fEvTail && (int64_t)ceil(fEv[fEvHead].at) <= p) {
            applyEvent(fEv[fEvHead]);
            fEvHead = (fEvHead + 1) % EVMAX;
        }
        int64_t end = limit > until ? limit : until;
        if (fEvHead != fEvTail) {
            const int64_t ne = (int64_t)ceil(fEv[fEvHead].at);
            if (ne < end) end = ne;
        }
        int n = (int)(end - p);
        if (n > 128) n = 128;
        if (n < 1) n = 1;
        vader_render_f(fInst, fBlkL, fBlkR, n);
        fRes.push(fBlkL, fBlkR, n);
    }
}

void VaderPlugin::run(const float **, float **outputs, uint32_t frames, const MidiEvent *ev, uint32_t nev) {
    float *outL = outputs[0], *outR = outputs[1];
    if (!fInst) {
        memset(outL, 0, sizeof(float) * frames);
        memset(outR, 0, sizeof(float) * frames);
        return;
    }
    std::lock_guard<std::mutex> g(fMutex);
    mirrorApply();   /* PLUG: what the Force sent since the last block */
    /* the host's transport: the arpeggiator and the synced orbit follow its song position */
    {
        const TimePosition &tp = getTimePosition();
        double ppq = 0, tempo = 0;
        if (tp.bbt.valid) {
            const double beats = (double)(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + (double)(tp.bbt.beat - 1) +
                                 (tp.bbt.ticksPerBeat > 0 ? tp.bbt.tick / tp.bbt.ticksPerBeat : 0.0);
            ppq = beats * 4.0 / (tp.bbt.beatType > 0 ? tp.bbt.beatType : 4.0);
            tempo = tp.bbt.beatsPerMinute;
        }
        char b[96];
        snprintf(b, sizeof b, "%d %.9f %.6f", tp.playing ? 1 : 0, ppq, tempo);
        engSet("host_time", b);
    }
    if (fRes.bypass()) {
        /* 44100 Hz: the engine's own rate. Render between the events, in pieces of at most 128 */
        uint32_t cur = 0;
        auto render_to = [&](uint32_t end) {
            while (cur < end) {
                int n = (int)(end - cur);
                if (n > 128) n = 128;
                vader_render_f(fInst, outL + cur, outR + cur, n);
                cur += (uint32_t)n;
            }
        };
        for (uint32_t i = 0; i < nev; i++) {
            const MidiEvent &m = ev[i];
            if (m.size < 1 || m.size > 3) continue;
            const uint8_t *d = m.size > 4 ? m.dataExt : m.data;
            render_to(m.frame < frames ? m.frame : frames);
            fEng->midi(fInst, d, (int)m.size);
        }
        render_to(frames);
    } else {
        for (uint32_t i = 0; i < nev; i++) {
            const MidiEvent &m = ev[i];
            if (m.size < 1 || m.size > 3) continue;
            const int next = (fEvTail + 1) % EVMAX;
            if (next == fEvHead) break;       /* a flood: drop the rest of this block's events */
            Ev &e = fEv[fEvTail];
            e.at = fRes.engine_time((int64_t)m.frame);
            e.n = (uint8_t)m.size;
            memcpy(e.d, m.data, m.size);
            fEvTail = next;
        }
        const int64_t limit = (int64_t)floor(fRes.engine_time((int64_t)frames - 1)) + 1;
        for (uint32_t j = 0; j < frames; j++) {
            produce(fRes.need_for_next(), limit);
            fRes.pull(&outL[j], &outR[j]);
        }
    }
    if (fMirror.plugged()) {   /* PLUG: the Force makes the sound; this copy of the engine only keeps the window right */
        memset(outL, 0, sizeof(float) * frames);
        memset(outR, 0, sizeof(float) * frames);
    }
    fPollAcc += frames;
    if (fPollAcc >= getSampleRate() / 25.0) {   /* 25 times a second, like the Force's wrapper */
        fPollAcc = 0;
        pollLocked();
    }
}

Plugin *createPlugin() {
    return new VaderPlugin();
}

END_NAMESPACE_DISTRHO
