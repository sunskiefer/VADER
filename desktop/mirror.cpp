/* VADER desktop, PLUG: the client side of src/web.c's /mirror. See mirror.hpp. */
#include "mirror.hpp"
#include <chrono>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t_;
#define SOCK_T SOCKET
#define CLOSESOCK closesocket
#define SOCKERR WSAGetLastError()
#define E_WOULDBLOCK WSAEWOULDBLOCK
#define E_INPROGRESS WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define SOCK_T int
#define CLOSESOCK close
#define SOCKERR errno
#define E_WOULDBLOCK EWOULDBLOCK
#define E_INPROGRESS EINPROGRESS
#define INVALID_SOCKET (-1)
#endif
extern "C" {
#include "vader_params.h"
#include "mirror_schema.h"
void overlay_publish(overlay_t *o, const ovl_view_t *v);
void overlay_publish_shape(overlay_t *o, const ovl_shape_t *sh);
}

#define WEB_PORT 6731
#define WEB_PORTS 9

static void setNonBlocking(SOCK_T s, bool nb) {
#ifdef _WIN32
    u_long m = nb ? 1 : 0;
    ioctlsocket(s, FIONBIO, &m);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, nb ? fl | O_NONBLOCK : fl & ~O_NONBLOCK);
#endif
}

static void netInit() {
#ifdef _WIN32
    static bool done = false;
    if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = true; }
#endif
}

static double nowS() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

MirrorClient::MirrorClient() {
    memset(&fBus, 0, sizeof fBus);
    fBus.fd = -1;
    fBus.started = 1;
    netInit();
}

MirrorClient::~MirrorClient() {
    fStop = true;
    fWant = false;
    if (fThread.joinable()) fThread.join();
    closeSock();
}

/* ---------------------------------------------------------------------------------------------- the window's side */
void MirrorClient::plug(const std::string &address) {
    {
        std::lock_guard<std::mutex> g(fLock);
        fAddrTyped = address;
        fAddr = address;
        fVersion.clear();
        fReadout.clear();
        fReadLine = 0;
    }
    fGen++;
    fWant = true;
    if (!fThread.joinable()) {
        fStop = false;
        fThread = std::thread([this] { run(); });
    }
    setState(address.empty() ? Finding : Connecting);
}

void MirrorClient::unplug() {
    fWant = false;
    fGen++;
    setState(Off);
}

std::string MirrorClient::address() {
    std::lock_guard<std::mutex> g(fLock);
    return fAddr;
}

std::string MirrorClient::forceVersion() {
    std::lock_guard<std::mutex> g(fLock);
    return fVersion;
}

void MirrorClient::readout(char *buf, int n, float *line) {
    std::lock_guard<std::mutex> g(fLock);
    snprintf(buf, (size_t)n, "%s", fReadout.c_str());
    if (line) *line = fReadLine;
}

/* ---------------------------------------------------------------------------------------------- the audio thread's side */
bool MirrorClient::takeState(std::string &out) {
    std::lock_guard<std::mutex> g(fLock);
    if (!fHaveState) return false;
    fHaveState = false;
    out.swap(fStateText);
    fStateText.clear();
    return true;
}

size_t MirrorClient::takeDeltas(std::vector<Delta> &out) {
    std::lock_guard<std::mutex> g(fLock);
    out.swap(fIn);
    fIn.clear();
    return out.size();
}

void MirrorClient::sendSet(uint16_t idx, float v) {
    if (!plugged()) return;
    unsigned char m[7] = {0x41};
    memcpy(m + 1, &idx, 2);
    memcpy(m + 3, &v, 4);
    std::lock_guard<std::mutex> g(fLock);
    if (fOut.size() < 65536) fOut.insert(fOut.end(), m, m + 7);
}

void MirrorClient::sendFire(uint16_t idx) {
    if (!plugged()) return;
    unsigned char m[7] = {0x42};
    memcpy(m + 1, &idx, 2);
    std::lock_guard<std::mutex> g(fLock);
    if (fOut.size() < 65536) fOut.insert(fOut.end(), m, m + 7);
}

/* ---------------------------------------------------------------------------------------------- sockets */
void MirrorClient::closeSock() {
    if (fSock != (intptr_t)INVALID_SOCKET && fSock >= 0) CLOSESOCK((SOCK_T)fSock);
    fSock = -1;
}

bool MirrorClient::sendAll(const unsigned char *p, size_t n) {
    while (n) {
#ifdef _WIN32
        int w = send((SOCK_T)fSock, (const char *)p, (int)n, 0);
#else
        ssize_t w = send((SOCK_T)fSock, p, n, MSG_NOSIGNAL);
#endif
        if (w < 0) {
            if (SOCKERR == E_WOULDBLOCK || SOCKERR == EAGAIN) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET((SOCK_T)fSock, &wf);
                struct timeval tv = {0, 200000};
                select((int)fSock + 1, nullptr, &wf, nullptr, &tv);
                continue;
            }
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

/* a masked binary frame (a client must mask) */
bool MirrorClient::sendFrame(const unsigned char *payload, size_t n) {
    unsigned char h[14];
    size_t hl = 0;
    h[hl++] = 0x82;
    if (n < 126) h[hl++] = (unsigned char)(0x80 | n);
    else if (n < 65536) { h[hl++] = 0x80 | 126; h[hl++] = (unsigned char)(n >> 8); h[hl++] = (unsigned char)n; }
    else { h[hl++] = 0x80 | 127; for (int i = 7; i >= 0; i--) h[hl++] = (unsigned char)((uint64_t)n >> (8 * i)); }
    unsigned char mask[4];
    unsigned r = (unsigned)rand();
    for (int i = 0; i < 4; i++) { mask[i] = (unsigned char)(r >> (8 * i)); h[hl++] = mask[i]; }
    std::vector<unsigned char> body(payload, payload + n);
    for (size_t i = 0; i < n; i++) body[i] ^= mask[i & 3];
    return sendAll(h, hl) && (n == 0 || sendAll(body.data(), n));
}

/* "VADER PLUG?" to every address on the network, on each port the Force may use; the first "VADER <port> <version>" wins */
bool MirrorClient::discover(std::string &addr, int &port) {
    SOCK_T s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return false;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
    setNonBlocking(s, true);
    const double t0 = nowS();
    double lastSend = -1;
    bool found = false;
    while (!found && nowS() - t0 < 2.5 && fWant && !fStop) {
        if (nowS() - lastSend > 0.5) {   /* asked a few times: a first broadcast is sometimes lost */
            lastSend = nowS();
            for (int k = 0; k < WEB_PORTS; k++) {
                struct sockaddr_in to;
                memset(&to, 0, sizeof to);
                to.sin_family = AF_INET;
                to.sin_port = htons((uint16_t)(WEB_PORT + k));
                to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
                sendto(s, "VADER PLUG?", 11, 0, (struct sockaddr *)&to, sizeof to);
            }
        }
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(s, &rf);
        struct timeval tv = {0, 100000};
        if (select((int)s + 1, &rf, nullptr, nullptr, &tv) > 0) {
            char buf[128];
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            int n = (int)recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
            if (n > 6) {
                buf[n] = 0;
                int p = 0;
                if (sscanf(buf, "VADER %d", &p) == 1 && p > 0) {
                    char ip[64];
                    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                    addr = ip;
                    port = p;
                    found = true;
                }
            }
        }
    }
    CLOSESOCK(s);
    return found;
}

bool MirrorClient::connectTo(const std::string &addr, int port) {
    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(addr.c_str(), ps, &hints, &res) || !res) return false;
    SOCK_T s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); return false; }
    setNonBlocking(s, true);
    int r = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (r < 0 && SOCKERR != E_INPROGRESS && SOCKERR != E_WOULDBLOCK) { CLOSESOCK(s); return false; }
    if (r < 0) {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(s, &wf);
        struct timeval tv = {3, 0};
        if (select((int)s + 1, nullptr, &wf, nullptr, &tv) <= 0) { CLOSESOCK(s); return false; }
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
        if (err) { CLOSESOCK(s); return false; }
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    fSock = (intptr_t)s;
    return true;
}

/* the WebSocket upgrade, then our hello */
bool MirrorClient::handshake() {
    char req[512];
    int n = snprintf(req, sizeof req,
                     "GET /mirror HTTP/1.1\r\nHost: vader\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: VkFERVIgUExVRyBkZXNrdG9wAA==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    if (!sendAll((const unsigned char *)req, (size_t)n)) return false;
    /* the 101 */
    fRx.clear();
    const double t0 = nowS();
    while (nowS() - t0 < 3.0 && fWant) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET((SOCK_T)fSock, &rf);
        struct timeval tv = {0, 100000};
        if (select((int)fSock + 1, &rf, nullptr, nullptr, &tv) <= 0) continue;
        unsigned char b[1024];
#ifdef _WIN32
        int r = recv((SOCK_T)fSock, (char *)b, sizeof b, 0);
#else
        ssize_t r = recv((SOCK_T)fSock, b, sizeof b, 0);
#endif
        if (r <= 0) return false;
        fRx.insert(fRx.end(), b, b + r);
        std::string s(fRx.begin(), fRx.end());
        size_t e = s.find("\r\n\r\n");
        if (e == std::string::npos) continue;
        if (s.compare(0, 12, "HTTP/1.1 101") != 0) return false;
        fRx.erase(fRx.begin(), fRx.begin() + (long)(e + 4));
        unsigned char hello[13] = {0x40};
        uint32_t pr = kProto, np = NP, sc = mirror_schema();
        memcpy(hello + 1, &pr, 4);
        memcpy(hello + 5, &np, 4);
        memcpy(hello + 9, &sc, 4);
        return sendFrame(hello, sizeof hello);
    }
    return false;
}

static inline float getF(const unsigned char *&p) { float f; memcpy(&f, p, 4); p += 4; return f; }

void MirrorClient::handle(const unsigned char *m, size_t n) {
    if (n < 1) return;
    switch (m[0]) {
    case 0x30: {   /* hello: the Force's parameters and version */
        if (n < 15) return;
        uint32_t pr, np, sc;
        memcpy(&pr, m + 1, 4); memcpy(&np, m + 5, 4); memcpy(&sc, m + 9, 4);
        /* m[13]: our slot number on the Force; m[14]: the length of its version text */
        int vl = m[14];
        std::string ver((const char *)m + 15, (size_t)vl <= n - 15 ? (size_t)vl : n - 15);
        {
            std::lock_guard<std::mutex> g(fLock);
            fVersion = ver;
        }
        if (pr != (uint32_t)kProto || np != (uint32_t)NP || sc != mirror_schema()) { setState(Rejected); fWant = false; return; }
        break;
    }
    case 0x31: {   /* the whole state */
        std::lock_guard<std::mutex> g(fLock);
        fStateText.assign((const char *)m + 1, n - 1);
        fHaveState = true;
        fIn.clear();   /* older deltas are inside it */
        setState(Plugged);
        break;
    }
    case 0x32: {   /* deltas */
        if (n < 3) return;
        unsigned cnt = m[1] | (unsigned)m[2] << 8;
        if (3 + cnt * 6 > n) return;
        std::lock_guard<std::mutex> g(fLock);
        const unsigned char *p = m + 3;
        for (unsigned i = 0; i < cnt && fIn.size() < 8192; i++) {
            Delta d;
            memcpy(&d.idx, p, 2);
            memcpy(&d.val, p + 2, 4);
            p += 6;
            fIn.push_back(d);
        }
        break;
    }
    case 0x33: {   /* the readout line */
        if (n < 5) return;
        float line;
        memcpy(&line, m + 1, 4);
        std::lock_guard<std::mutex> g(fLock);
        fReadLine = line;
        fReadout.assign((const char *)m + 5, n - 5);
        break;
    }
    case 0x02: {   /* the shape */
        if (n < 8) return;
        static thread_local ovl_shape_t sh;
        memset(&sh, 0, sizeof sh);
        sh.is3d = m[1];
        sh.shape_user = m[2];
        sh.has_img = m[3];
        uint32_t cnt;
        memcpy(&cnt, m + 4, 4);
        if (cnt != OVL_PATHN || n < 8 + (size_t)cnt * 12) return;
        const unsigned char *p = m + 8;
        for (int k = 0; k < OVL_PATHN; k++) { sh.px[k] = getF(p); sh.py[k] = getF(p); sh.pz[k] = getF(p); }
        if (sh.has_img) {
            if (n < 8 + (size_t)cnt * 12 + OVL_IMG_N * OVL_IMG_N) sh.has_img = 0;
            else for (int k = 0; k < OVL_IMG_N * OVL_IMG_N; k++) sh.img[k] = p[k] / 255.0f;
        }
        overlay_publish_shape(&fBus, &sh);
        break;
    }
    case 0x21: {   /* the screen's data: the phone page's view with the meters and the page counter */
        if (n < 4 + 4 * (19 + 1 + 32 + 3 + 4 + 1)) return;
        ovl_view_t v;
        memset(&v, 0, sizeof v);
        v.motion = m[1];
        const unsigned char *p = m + 4;
        v.rot = getF(p); v.yaw = getF(p); v.tilt = getF(p); v.size = getF(p); v.sub_size = getF(p);
        v.ot = getF(p); v.oo = getF(p); v.hz = getF(p); v.sub = getF(p);
        v.env = getF(p); v.w[0] = getF(p); v.w[1] = getF(p); v.w[2] = getF(p); v.w[3] = getF(p);
        v.use_pos = (int)getF(p); v.fx = getF(p); v.fy = getF(p); v.ox = getF(p); v.oy = getF(p);
        v.nballs = (int)getF(p);
        for (int k = 0; k < 16; k++) { v.bx[k] = getF(p); v.by[k] = getF(p); }
        v.ps = getF(p); v.pe = getF(p); v.style = getF(p);
        for (int k = 0; k < 4; k++) v.im[k] = getF(p);
        v.pgctr = (int)getF(p);
        if (v.nballs < 0) v.nballs = 0;
        if (v.nballs > 16) v.nballs = 16;
        fPgctr.store(v.pgctr, std::memory_order_relaxed);
        overlay_publish(&fBus, &v);
        break;
    }
    default:
        break;
    }
}

/* one connection: frames in, queued messages out, until the socket drops or PLUG is pressed again */
void MirrorClient::session() {
    const unsigned gen = fGen.load();
    while (fWant && !fStop && fGen.load() == gen) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET((SOCK_T)fSock, &rf);
        struct timeval tv = {0, 2000};   /* 2 ms: a control moved here goes out within that */
        int r = select((int)fSock + 1, &rf, nullptr, nullptr, &tv);
        if (r > 0) {
            unsigned char b[16384];
#ifdef _WIN32
            int got = recv((SOCK_T)fSock, (char *)b, sizeof b, 0);
#else
            ssize_t got = recv((SOCK_T)fSock, b, sizeof b, 0);
#endif
            if (got == 0) return;
            if (got < 0) { if (SOCKERR == E_WOULDBLOCK || SOCKERR == EAGAIN) continue; return; }
            fRx.insert(fRx.end(), b, b + got);
            /* whole frames (the server does not mask) */
            for (;;) {
                if (fRx.size() < 2) break;
                unsigned op = fRx[0] & 0x0f;
                bool masked = (fRx[1] & 0x80) != 0;
                size_t len = fRx[1] & 0x7f, hl = 2;
                if (len == 126) { if (fRx.size() < 4) break; len = (size_t)fRx[2] << 8 | fRx[3]; hl = 4; }
                else if (len == 127) { if (fRx.size() < 10) break; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | fRx[2 + i]; hl = 10; }
                if (masked) hl += 4;
                if (len > 4u * 1024 * 1024) return;
                if (fRx.size() < hl + len) break;
                if (op == 8) return;
                if (op == 9) {   /* ping -> pong */
                    unsigned char pong[2] = {0x8a, 0x80};
                    unsigned char mk[4] = {0, 0, 0, 0};
                    sendAll(pong, 2);
                    sendAll(mk, 4);
                } else if (op == 2) handle(fRx.data() + hl, len);
                fRx.erase(fRx.begin(), fRx.begin() + (long)(hl + len));
            }
        }
        /* what the window changed */
        std::vector<unsigned char> out;
        {
            std::lock_guard<std::mutex> g(fLock);
            out.swap(fOut);
        }
        for (size_t i = 0; i + 7 <= out.size(); i += 7)
            if (!sendFrame(out.data() + i, out[i] == 0x42 ? 3 : 7)) return;
    }
}

void MirrorClient::run() {
    while (!fStop) {
        if (!fWant) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (fStop) break;
            continue;
        }
        const unsigned gen = fGen.load();
        std::string addr, typed;
        {
            std::lock_guard<std::mutex> g(fLock);
            addr = typed = fAddrTyped;
        }
        int port = WEB_PORT;
        if (addr.empty()) {
            setState(Finding);
            std::string found;
            if (!discover(found, port)) {
                if (fWant && fGen.load() == gen) { setState(NotFound); fWant = false; }
                continue;
            }
            addr = found;
            std::lock_guard<std::mutex> g(fLock);
            fAddr = found;
        } else {
            /* "address" or "address:port" */
            size_t c = addr.find(':');
            if (c != std::string::npos) { port = atoi(addr.c_str() + c + 1); addr = addr.substr(0, c); if (port <= 0) port = WEB_PORT; }
        }
        setState(Connecting);
        bool ok = false;
        const int tries = !typed.empty() && typed.find(':') == std::string::npos ? WEB_PORTS : 1;
        for (int k = 0; k < tries && !ok && fWant && fGen.load() == gen; k++)
            ok = connectTo(addr, port + k);   /* a typed address: 6731, or the next ports if the Force fell back to one */
        if (!ok) {
            if (fWant && fGen.load() == gen) {
                setState(Unreachable);
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));   /* and try again while PLUG is on */
            }
            continue;
        }
        if (!handshake()) {
            closeSock();
            if (fWant && fGen.load() == gen) { setState(Unreachable); std::this_thread::sleep_for(std::chrono::milliseconds(1500)); }
            continue;
        }
        session();
        closeSock();
        if (fWant && fGen.load() == gen && state() != Rejected) {
            setState(Connecting);   /* the link dropped: back in a moment */
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        }
    }
}
