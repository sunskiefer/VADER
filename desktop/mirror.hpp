/* VADER desktop, PLUG (0.9): the desktop plugin as a mirror of the VADER running on a Force.
 *
 * Press PLUG and the window shows the Force's VADER: its screen (the shape, the orbiter, the balls, every mode's
 * movement, straight from the Force, drawn here in full quality), its controls and its top line, and what you change
 * here goes to the Force at once. The link is the Force plugin's own web port (6731, src/web.c, path /mirror), a
 * WebSocket on a plain TCP connection: the Force sends its whole state once, then every parameter that changes, the
 * shape when it changes and the screen's data 60 times a second; this side sends each control change as it happens.
 * The Force is found on the network by a broadcast ("VADER PLUG?" to UDP 6731..6739), or by an address typed in the
 * window; the last address is remembered next to the Shapes folder. Everything runs on its own thread: the host's
 * audio thread only hands over small queues (see VaderPlugin::run). */
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <stdint.h>
extern "C" {
#include "overlay.h"
}

class MirrorClient {
public:
    enum State { Off, Finding, Connecting, Plugged, NotFound, Unreachable, Rejected };
    struct Delta { uint16_t idx; float val; };

    MirrorClient();
    ~MirrorClient();

    /* ---- the window */
    void plug(const std::string &address);     /* "" : find the Force by broadcast */
    void unplug();
    State state() const { return (State)fState.load(std::memory_order_acquire); }
    bool plugged() const { return state() == Plugged; }
    std::string address();                     /* the Force's address (found or typed) */
    std::string forceVersion();                /* what the Force said it runs (after hello) */
    void readout(char *buf, int n, float *line);   /* the Force's top line */
    int pgctr() const { return fPgctr.load(std::memory_order_relaxed); }
    overlay_t *bus() { return &fBus; }         /* the Force's screen data, as the engine's own bus */

    /* ---- the audio thread (VaderPlugin::run) */
    bool takeState(std::string &out);          /* 1: the whole state text arrived (apply, then the deltas) */
    size_t takeDeltas(std::vector<Delta> &out);
    void sendSet(uint16_t idx, float v);
    void sendFire(uint16_t idx);

    static const int kProto = 1;

private:
    void run();
    bool discover(std::string &addr, int &port);
    bool connectTo(const std::string &addr, int port);
    bool handshake();
    void session();
    void handle(const unsigned char *m, size_t n);
    bool sendFrame(const unsigned char *payload, size_t n);
    bool sendAll(const unsigned char *p, size_t n);
    void closeSock();
    void setState(State s) { fState.store(s, std::memory_order_release); }

    std::thread fThread;
    std::atomic<bool> fWant{false}, fStop{false};
    std::atomic<int> fState{Off};
    std::atomic<int> fPgctr{0};
    std::atomic<unsigned> fGen{0};
    mutable std::mutex fLock;
    std::string fAddr, fAddrTyped, fVersion, fStateText, fReadout;
    float fReadLine = 0;
    bool fHaveState = false;
    std::vector<Delta> fIn;
    std::vector<unsigned char> fOut;
    std::vector<unsigned char> fRx;
    overlay_t fBus;
    intptr_t fSock = -1;
    int fPort = 0;
    unsigned fNP = 0, fSchema = 0;
};
