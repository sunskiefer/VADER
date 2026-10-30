/* VADER desktop plugin: the DSP side (DPF). Wraps the Force build's engine (src/engine.c, unchanged sound) and gives the
 * window (VaderUI) a direct, lock-free path to what it draws. See docs/DESKTOP.md. */
#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include "DistrhoPlugin.hpp"
#include "Resampler.hpp"
#include "gen/vader_gen.h"
#include "mirror.hpp"

extern "C" {
#include "engine.h"
#include "overlay.h"
void vader_render_f(void *inst, float *l, float *r, int frames);
void *vader_view_bus(void *inst);
}

START_NAMESPACE_DISTRHO

class VaderPlugin : public Plugin {
public:
    VaderPlugin();
    ~VaderPlugin() override;

    /* ---- for the window (DISTRHO_PLUGIN_WANT_DIRECT_ACCESS): every call is safe from the window's thread */
    float uiValue(int par) const { return fValues[par].load(std::memory_order_relaxed); }
    unsigned uiVersion() const { return fVersion.load(std::memory_order_acquire); }
    void uiPoll();                                         /* asks the engine what changed by itself, if run() has not lately */
    void uiReadout(char *buf, int n) const;                /* the line the Force shows across the top: "NAME   value" */
    void uiText(int slot, char *buf, int n) const;         /* VG_TEXT_PARS[slot]'s text: FILE, MY PRESET SLOT, PRESET, SHAPE */
    bool uiPresetName(int i, char *buf, int n);
    overlay_t *uiViewBus() { return fMirror.plugged() ? fMirror.bus() : fBus; }   /* the orbit screen's data: the Force's while plugged */
    /* PLUG (0.9): the window's button. plug("") finds the Force on the network; an address (or address:port) is remembered */
    void uiPlug(const std::string &address);
    void uiUnplug() { fMirror.unplug(); }
    MirrorClient &mirror() { return fMirror; }
    std::string uiRememberedAddress() const { return fForceAddr; }
    void uiFire(int par);                                  /* a momentary button pressed in the window (TAP, LOAD, ...): straight to the engine */
    int uiCorner() const;                                  /* 0..4 */

protected:
    const char *getLabel() const override { return "VADER"; }
    const char *getDescription() const override {
        return "VADER: a vector synth. An orbiter travels a shape and its position blends four corner sounds.";
    }
    const char *getMaker() const override { return "L'Cronx"; }
    const char *getHomePage() const override { return "https://github.com/sunskiefer/VADER"; }
    const char *getLicense() const override { return "Proprietary / see repository LICENSE"; }
    uint32_t getVersion() const override { return d_version(0, 9, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'n', 'V', 'd'); }

    void initParameter(uint32_t index, Parameter &p) override;
    void initProgramName(uint32_t index, String &name) override;
    void initState(uint32_t index, State &state) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;
    void loadProgram(uint32_t index) override;
    String getState(const char *key) const override;
    void setState(const char *key, const char *value) override;
    void activate() override;
    void sampleRateChanged(double sr) override;
    void run(const float **, float **outputs, uint32_t frames, const MidiEvent *ev, uint32_t nev) override;

private:
    struct Ev {
        double at;           /* on the engine's clock */
        uint8_t n, d[3];
    };
    static constexpr int EVMAX = 1024;

    void engSet(const char *key, const char *val);
    bool engGet(const char *key, char *buf, int n) const;
    void primeAll();                         /* reads every host parameter back from the engine (after a state restore) */
    void pollLocked();                       /* the engine's "dirty" list -> fValues, the texts */
    void applyEvent(const Ev &e);
    void produce(int64_t until, int64_t limit);
    void bumpVersion() { fVersion.fetch_add(1, std::memory_order_release); }
    void setTextLocked(char *dst, const char *src);

    const mpc_engine_t *fEng;
    void *fInst;
    overlay_t *fBus;
    mutable std::mutex fMutex;
    std::atomic<float> fValues[VG_NEXP];
    float fTrigState[VG_NEXP];
    std::atomic<unsigned> fVersion;
    mutable std::atomic_flag fTextLock = ATOMIC_FLAG_INIT;
    char fReadout[96];
    char fTexts[4][96];
    std::string fPresetNames[16];
    VaderResampler fRes;
    Ev fEv[EVMAX];
    int fEvHead = 0, fEvTail = 0;           /* the queue, in order of arrival (the host sorts them) */
    double fPollAcc = 0;
    std::chrono::steady_clock::time_point fLastPoll;
    float fBlkL[128], fBlkR[128];
    /* PLUG */
    MirrorClient fMirror;
    int16_t fExp2Eng[VG_NEXP];               /* host parameter -> engine parameter index */
    std::string fForceAddr, fAddrFile;       /* the Force's address last used, and where it is kept */
    std::vector<MirrorClient::Delta> fDeltas;
    std::string fStateIn;
    void mirrorApply();                      /* what the Force sent, into the engine (under fMutex) */
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaderPlugin)
};

END_NAMESPACE_DISTRHO
