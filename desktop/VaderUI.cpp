/* VADER desktop plugin: the window (DPF + NanoVG, OpenGL). The layout is the Force's six pages (tools/vader_skin.py,
 * through desktop/gen/vader_gen.h) in a 1280 x 628 space under a tab row, scaled to the window; the orbit screen is
 * drawn here in full quality from the engine's live view (the same drawing as the companion page, web/index.html,
 * with real glow). Everything the user changes goes to the host as a parameter edit. */
#include "DistrhoUI.hpp"
#include "VaderPlugin.hpp"
#include "gen/vader_res.h"
#include <algorithm>
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nanovg/nanovg.h"

START_NAMESPACE_DISTRHO

static const float TAU_ = 6.28318530718f;
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

struct Col { uint8_t r, g, b; };
static const Col ACCENT = {58, 160, 255}, TRACK = {34, 68, 108}, INK = {255, 255, 255}, INK_DIM = {232, 236, 242},
                 CYAN = {125, 252, 255};
static const Col PATH_COLS[6] = {{255, 225, 74}, {255, 176, 32}, {255, 128, 26}, {255, 84, 40}, {255, 52, 90}, {255, 60, 170}};
static const Col ORB_COLS[6] = {{70, 120, 255}, {60, 175, 255}, {125, 252, 255}, {90, 255, 200}, {110, 255, 140}, {190, 255, 90}};
static int colStep(float s) { return std::max(0, std::min(5, (int)floorf(s * 6))); }

class VaderUI : public UI {
public:
    VaderUI() : UI(VG_CANVAS_W, VG_CANVAS_H) {
        setGeometryConstraints(VG_CANVAS_W / 2, VG_CANVAS_H / 2, true);
        fP = static_cast<VaderPlugin *>(getPluginInstancePointer());
        int n = 0;
        const unsigned char *d = vg_font_bold(&n);
        fBold = createFontFromMemory("vader-bold", d, (uint)n, false);
        d = vg_font_semi(&n);
        fSemi = createFontFromMemory("vader-semi", d, (uint)n, false);
        memset(fv, 0, sizeof fv);
        for (int i = 0; i < VG_NEXP; i++) fv[i] = VG_PARS[i].def;
        memset(&fView, 0, sizeof fView);
        memset(&fShape, 0, sizeof fShape);
        fShapeVer = ~0u;
        fT0 = std::chrono::steady_clock::now();
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index < (uint32_t)VG_NEXP) fv[index] = value;
    }
    void uiIdle() override {
        if (fP) fP->uiPoll();
        repaint();
    }
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent &) override;
    bool onMotion(const MotionEvent &) override;
    bool onScroll(const ScrollEvent &) override;
    bool onKeyboard(const KeyboardEvent &ev) override {
        if (ev.press && ev.key == 27 && fPop.open) { fPop.open = false; return true; }
        if (ev.press && !fPop.open && (ev.key == kKeyLeft || ev.key == kKeyRight)) {   /* 0.8: arrow keys turn the pages */
            fPage = (fPage + (ev.key == kKeyRight ? 1 : VG_NPAGES - 1)) % VG_NPAGES;
            repaint();
            return true;
        }
        return false;
    }

private:
    /* ---- what a control writes: one host parameter, or (the main page's corner knobs) the selected corner's field */
    struct Bind {
        int n = 0, par[4] = {-1, -1, -1, -1};
    };
    struct Drag {
        bool on = false;
        const VgCtl *c = nullptr;
        Bind b;
        float v0 = 0, acc = 0;
        double x0 = 0, y0 = 0;
        int opt = -1;
    };
    struct Pop {
        bool open = false;
        const VgCtl *c = nullptr;
        Bind b;
        bool preset = false;
        int n = 0, cols = 2, ow = 150, oh = 40;
        float lx = 0, ly = 0, pw = 0, ph = 0;
    };

    VaderPlugin *fP;
    FontId fBold, fSemi;
    float fv[VG_NEXP];
    int fPage = 0;
    float fScale = 1, fOx = 0, fOy = 0;
    double fMx = -1, fMy = -1;
    Drag fDrag;
    Pop fPop;
    const VgCtl *fPressedBtn = nullptr;
    const VgCtl *fLastClick = nullptr;
    std::chrono::steady_clock::time_point fLastClickT, fT0;
    float fReadLine = 0;
    ovl_view_t fView;
    ovl_shape_t fShape;
    unsigned fShapeVer;
    float fx2[256], fy2[256], fz2[256];
    NanoImage fImg;
    bool fImgOk = false;

    /* ---- coordinates */
    void toCanvas(double px, double py, double &cx, double &cy) const {
        cx = (px - fOx) / fScale;
        cy = (py - fOy) / fScale;
    }

    /* ---- values */
    int corner() const { return fP ? fP->uiCorner() : (int)lroundf(fv[VGP_CORNER]); }
    Bind bindOf(const VgCtl &c) const {
        Bind b;
        if (c.front) {
            const int cn = corner(), fi = c.front - 1, f = VG_FRONT_FIELD[fi];
            if (cn > 3) {
                b.n = 4;
                for (int k = 0; k < 4; k++) b.par[k] = VG_CORNER_PAR[k][f];
            } else {
                b.n = 1;
                b.par[0] = VG_CORNER_PAR[cn][f];
            }
        } else if (c.par >= 0) {
            b.n = 1;
            b.par[0] = c.par;
        }
        return b;
    }
    float valueOf(const Bind &b) const { return b.n ? fv[b.par[0]] : 0; }
    float normOf(const Bind &b) const {
        if (!b.n) return 0;
        const VgPar &P = VG_PARS[b.par[0]];
        return P.mx > P.mn ? clampf((fv[b.par[0]] - P.mn) / (P.mx - P.mn), 0, 1) : 0;
    }
    float quant(const VgPar &P, float v) const {
        v = clampf(v, P.mn, P.mx);
        if (P.nopts || P.isint) v = roundf(v);
        return v;
    }
    void beginEdit(const Bind &b) {
        for (int k = 0; k < b.n; k++) editParameter(b.par[k], true);
    }
    void endEdit(const Bind &b) {
        for (int k = 0; k < b.n; k++) editParameter(b.par[k], false);
    }
    void setBound(const Bind &b, float v) {
        if (!b.n) return;
        const VgPar &P = VG_PARS[b.par[0]];
        v = quant(P, v);
        for (int k = 0; k < b.n; k++) {
            fv[b.par[k]] = v;
            setParameterValue(b.par[k], v);
        }
        fReadLine = P.mx > P.mn ? (v - P.mn) / (P.mx - P.mn) : 0;
        if (b.par[0] == VGP_CORNER || b.par[0] == VGP_TIMBRE || b.par[0] == VGP_PRESET) {
            if (fP) fP->uiPoll();
        }
    }
    void valueText(const Bind &b, char *out, int n) const {
        if (!b.n) { out[0] = 0; return; }
        const int par = b.par[0];
        for (int s = 0; s < 4; s++)
            if (VG_TEXT_PARS[s] == par && fP) { fP->uiText(s, out, n); if (out[0]) return; }
        const VgPar &P = VG_PARS[par];
        const float v = fv[par];
        if (P.nopts) {
            int i = (int)lroundf(v);
            i = i < 0 ? 0 : i >= P.nopts ? P.nopts - 1 : i;
            snprintf(out, (size_t)n, "%s", P.opts[i]);
        } else if (P.isint) {
            snprintf(out, (size_t)n, "%s%d", (P.mn < 0 && v > 0) ? "+" : "", (int)lroundf(v));
        } else {
            snprintf(out, (size_t)n, "%s%d", (P.mn < 0 && v > 0.004f) ? "+" : "", (int)lroundf(v * 100));
        }
    }
    bool visible(const VgCtl &c) const {
        return c.wpar < 0 || (int)lroundf(fv[c.wpar]) == c.wopt;
    }

    /* ---- geometry of the controls */
    struct R { float x, y, w, h; };
    R rectOf(const VgCtl &c) const {
        if (c.kind == VGK_HLINE) return {(float)c.x, (float)c.y, (float)c.w, (float)(c.big ? 52 : 50)};
        if (c.kind == VGK_FADER) return {(float)c.x, (float)c.y, (float)c.w, 186.f};
        if (c.kind == VGK_METER) return {(float)c.x, (float)c.y, (float)c.w, 30.f};
        return {(float)c.x, (float)c.y, (float)c.w, (float)c.h};
    }
    static bool inside(const R &r, double x, double y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
    R radioSlot(const VgCtl &c, int slot) const {
        const int n = std::max(1, (int)c.nlab);
        if (c.vertical) return {(float)c.x, (float)(c.y + slot * (c.h + 6)), (float)c.w, (float)c.h};
        const float rw = (c.w - 6.f * (n - 1)) / n;
        return {c.x + slot * (rw + 6), (float)c.y, rw, (float)c.h};
    }
    /* hline: the line's length and the value's width */
    float lineLen(const VgCtl &c) const { return c.w - (c.big ? 64 : 56) - 4; }

    /* ---- drawing helpers */
    void colr(Col c, int a = 255) { fillColor(c.r, c.g, c.b, a); }
    void strk(Col c, int a, float w) { strokeColor(c.r, c.g, c.b, a); strokeWidth(w); }
    void text_(float x, float y, const char *s, float size, int align, Col c, int a = 255, bool bold = true) {
        fontFaceId(bold ? fBold : fSemi);
        fontSize(size);
        textAlign(align);
        colr(c, a);
        text(x, y, s, nullptr);
    }
    float textW(const char *s, float size, bool bold = true) {
        fontFaceId(bold ? fBold : fSemi);
        fontSize(size);
        Rectangle<float> b;
        textBounds(0, 0, s, nullptr, b);
        return b.getWidth();
    }
    /* a label that shrinks to fit */
    void fitText(float x, float y, float maxW, const char *s, float size, int align, Col c, int a = 255, bool bold = true) {
        float sz = size;
        while (sz > 13 && textW(s, sz, bold) > maxW) sz -= 1;
        text_(x, y, s, sz, align, c, a, bold);
    }
    void glowLine(float x0, float y0, float x1, float y1, Col c, float w, float strength = 1.f) {
        NVGcontext *ctx = getContext();
        nvgGlobalCompositeBlendFunc(ctx, NVG_SRC_ALPHA, NVG_ONE);
        lineCap(NanoVG::ROUND);
        const float ws[3] = {w * 3.2f, w * 2.0f, w * 1.3f}, al[3] = {0.10f, 0.16f, 0.30f};
        for (int i = 0; i < 3; i++) {
            beginPath();
            moveTo(x0, y0);
            lineTo(x1, y1);
            strk(c, (int)(255 * al[i] * strength), ws[i]);
            stroke();
        }
        nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        strk(c, (int)(255 * strength), w);
        stroke();
    }
    void drawButton(const R &r, const char *label, bool on, bool hover, float size = 19) {
        beginPath();
        roundedRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 9);
        fillColor(0, 0, 0, hover ? 150 : 215);
        fill();
        if (on) {
            NVGcontext *ctx = getContext();
            nvgGlobalCompositeBlendFunc(ctx, NVG_SRC_ALPHA, NVG_ONE);
            for (int i = 0; i < 2; i++) {
                beginPath();
                roundedRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 9);
                strk(ACCENT, i ? 70 : 36, i ? 4.f : 9.f);
                stroke();
            }
            nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
        }
        beginPath();
        roundedRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 9);
        const Col bc = on ? ACCENT : (hover ? Col{70, 120, 180} : TRACK);
        strk(bc, 255, 2);
        stroke();
        if (label && *label) fitText(r.x + r.w / 2, r.y + r.h / 2 + 1, r.w - 18, label, size, ALIGN_CENTER | ALIGN_MIDDLE, on ? INK : INK_DIM);
    }
    void drawWindowBox(const R &r) {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 9);
        fillColor(0, 0, 0, 215);
        fill();
        strk(TRACK, 255, 2);
        stroke();
    }

    /* ---- the controls */
    void drawCtl(const VgCtl &c);
    void drawStripe();
    void drawTabs();
    void drawScreen(const R &r);
    void drawEnv(const R &r);
    void drawPopup();
    void prepareScreenData();
    void project(float rot, float yaw, float tilt);
    void pathAt(float t, float &x, float &y) const {
        float u = (t - floorf(t)) * 256.f;
        int i = (int)u;
        const float f = u - (float)i;
        i &= 255;
        const int j = (i + 1) & 255;
        x = fx2[i] + (fx2[j] - fx2[i]) * f;
        y = fy2[i] + (fy2[j] - fy2[i]) * f;
    }
    const VgCtl *hit(double x, double y, bool *stripe = nullptr) const;
    void openPopup(const VgCtl &c, bool preset);
    void clickCtl(const VgCtl &c, double x, double y, bool press);
    void stepBound(const Bind &b, int dir, bool fine);
    bool isHover(const R &r) const { return inside(r, fMx, fMy); }
};

/* ============================================================================================================ screen */
void VaderUI::project(float rot, float yaw, float tilt) {
    const float cr = cosf(rot * TAU_), sr = sinf(rot * TAU_);
    float m = 0;
    if (fShape.is3d) {
        const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(tilt), sp = sinf(tilt);
        for (int i = 0; i < 256; i++) {
            const float x = fShape.px[i], y = fShape.py[i], z = fShape.pz[i];
            const float x1 = x * cy + z * sy, z1 = -x * sy + z * cy;
            const float y1 = y * cp - z1 * sp, zz = y * sp + z1 * cp;
            const float persp = 1.f / (1.f + 0.25f * zz), a = x1 * persp, b = y1 * persp;
            fx2[i] = a * cr - b * sr;
            fy2[i] = a * sr + b * cr;
            fz2[i] = zz;
        }
    } else {
        for (int i = 0; i < 256; i++) {
            fx2[i] = fShape.px[i] * cr - fShape.py[i] * sr;
            fy2[i] = fShape.px[i] * sr + fShape.py[i] * cr;
            fz2[i] = 0;
        }
    }
    for (int i = 0; i < 256; i++) m = std::max(m, std::max(fabsf(fx2[i]), fabsf(fy2[i])));
    if (m > 1.f)
        for (int i = 0; i < 256; i++) { fx2[i] /= m; fy2[i] /= m; }
}

void VaderUI::prepareScreenData() {
    overlay_t *bus = fP ? fP->uiViewBus() : nullptr;
    if (!bus) return;
    overlay_read_view(bus, &fView);
    const unsigned ver = overlay_shape_version(bus);
    if (ver != fShapeVer && !(ver & 1)) {
        overlay_read_shape(bus, &fShape);
        fShapeVer = overlay_shape_version(bus);
        fImgOk = false;
        if (fShape.shape_user && fShape.has_img) {
            static unsigned char rgba[64 * 64 * 4];
            for (int i = 0; i < 64 * 64; i++) {
                const float b = clampf(fShape.img[i], 0, 1);
                rgba[i * 4 + 0] = 90;
                rgba[i * 4 + 1] = 150;
                rgba[i * 4 + 2] = 255;
                rgba[i * 4 + 3] = (unsigned char)(b < 0.04f ? 0 : b * 0.35f * 255);
            }
            fImg = createImageFromRGBA(64, 64, rgba, (NanoVG::ImageFlags)0);
            fImgOk = fImg.isValid();
        }
    }
}

void VaderUI::drawScreen(const R &r) {
    NVGcontext *ctx = getContext();
    // the glass: a dark panel with a soft vignette
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, 12);
    fillColor(2, 5, 12, 235);
    fill();
    Paint vg = radialGradient(r.x + r.w / 2, r.y + r.h / 2, std::min(r.w, r.h) * 0.2f, std::max(r.w, r.h) * 0.75f,
                              Color(14, 24, 48, 140), Color(0, 0, 0, 0));
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, 12);
    fillPaint(vg);
    fill();
    strk(TRACK, 255, 2);
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, 12);
    stroke();
    save();
    intersectScissor(r.x, r.y, r.w, r.h);
    prepareScreenData();
    const float barsH = std::max(22.f, r.h * 0.07f);
    const float S = std::max(40.f, std::min(r.w - 2 * std::max(28.f, r.w * 0.06f), r.h - barsH - 22.f));
    const float CX = r.x + r.w / 2, CY = r.y + 10 + (r.h - barsH - 12 - 10) / 2;
    const float k = std::max(0.8f, S / 400.f);   // line weights follow the picture's size
    auto sx = [&](float x) { return CX + x * S * 0.5f; };
    auto sy = [&](float y) { return CY + y * S * 0.5f; };
    const float c0 = CX - S / 2, c1 = CX + S / 2, r0 = CY - S / 2, r1 = CY + S / 2;
    // the square's corners and the corner names
    {
        const float L = S * 0.06f;
        lineCap(NanoVG::ROUND);
        strk(Col{70, 120, 180}, 230, std::max(1.5f, 2 * k));
        const float cs[4][4] = {{c0, r0, 1, 1}, {c1, r0, -1, 1}, {c0, r1, 1, -1}, {c1, r1, -1, -1}};
        for (auto &q : cs) {
            beginPath();
            moveTo(q[0] + q[2] * L, q[1]);
            lineTo(q[0], q[1]);
            lineTo(q[0], q[1] + q[3] * L);
            stroke();
        }
        const float fs = std::max(13.f, S * 0.055f);
        text_(c0 - S * 0.03f, r0 + fs * 0.4f, "A", fs, ALIGN_RIGHT | ALIGN_MIDDLE, Col{90, 140, 200});
        text_(c1 + S * 0.03f, r0 + fs * 0.4f, "B", fs, ALIGN_LEFT | ALIGN_MIDDLE, Col{90, 140, 200});
        text_(c0 - S * 0.03f, r1 - fs * 0.4f, "C", fs, ALIGN_RIGHT | ALIGN_MIDDLE, Col{90, 140, 200});
        text_(c1 + S * 0.03f, r1 - fs * 0.4f, "D", fs, ALIGN_LEFT | ALIGN_MIDDLE, Col{90, 140, 200});
    }
    const bool haveShape = fShapeVer != ~0u;
    if (haveShape) {
        const ovl_view_t &v = fView;
        project(v.rot, v.yaw, v.tilt);
        const float size = v.size, sr_ = v.sub_size * 0.6f;
        const float ot = v.ot, oo = v.oo;
        // a loaded picture, dim, under its shape
        if (fImgOk) {
            save();
            translate(CX, CY);
            rotate(v.rot * TAU_);
            const float side = S * size;
            Paint ip = imagePattern(-side / 2, -side / 2, side, side, 0, fImg, 1.f);
            beginPath();
            rect(-side / 2, -side / 2, side, side);
            fillPaint(ip);
            fill();
            restore();
        }
        // the orbit line: the orbiter's whole path over one orbit, faint
        if (v.motion && fabsf(v.hz) > 0.05f && sr_ > 0.004f) {
            const int ratio = (int)clampf(roundf(v.sub / v.hz), -12, 12);
            const float b0 = oo - ratio * ot;
            const int ln = std::min(640, 96 + std::abs(ratio) * 40);
            beginPath();
            for (int i = 0; i < ln; i++) {
                const float a = (float)i / (ln - 1), b = b0 + ratio * a;
                float px, py;
                pathAt(a, px, py);
                px *= size;
                py *= size;
                const float X = sx(clampf(px + sr_ * cosf(TAU_ * b), -1, 1)), Y = sy(clampf(py + sr_ * sinf(TAU_ * b), -1, 1));
                if (i) lineTo(X, Y); else moveTo(X, Y);
            }
            lineJoin(NanoVG::ROUND);
            strk(Col{90, 160, 245}, 80, std::max(1.f, 1.2f * k));
            stroke();
        }
        // the shape: a glowing line (a 3D shape shaded by depth)
        lineJoin(NanoVG::ROUND);
        lineCap(NanoVG::ROUND);
        nvgGlobalCompositeBlendFunc(ctx, NVG_SRC_ALPHA, NVG_ONE);
        if (fShape.is3d) {
            for (int pass = 0; pass < 3; pass++) {
                for (int i = 0; i < 256; i++) {
                    const int j = (i + 1) & 255;
                    const float depth = clampf(0.5f - 0.25f * (fz2[i] + fz2[j]), 0, 1);
                    beginPath();
                    moveTo(sx(fx2[i] * size), sy(fy2[i] * size));
                    lineTo(sx(fx2[j] * size), sy(fy2[j] * size));
                    const float w = (1.4f + 1.6f * depth) * k;
                    const float ws[3] = {w * 4.f, w * 2.2f, w};
                    const int al[3] = {(int)(28 * (0.4f + depth)), (int)(60 * (0.4f + depth)), (int)(255 * (0.35f + 0.65f * depth))};
                    strk(Col{80, 165, 255}, std::min(255, al[pass]), ws[pass]);
                    stroke();
                }
            }
        } else {
            const float ws[4] = {14 * k, 8 * k, 4.2f * k, 2.0f * k};
            const int al[4] = {26, 50, 110, 255};
            const Col cl[4] = {ACCENT, ACCENT, Col{110, 190, 255}, Col{200, 232, 255}};
            for (int pass = 0; pass < 4; pass++) {
                if (pass == 3) nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
                beginPath();
                for (int i = 0; i <= 256; i++) {
                    const int q = i & 255;
                    if (i) lineTo(sx(fx2[q] * size), sy(fy2[q] * size)); else moveTo(sx(fx2[q] * size), sy(fy2[q] * size));
                }
                strk(cl[pass], al[pass], ws[pass]);
                stroke();
            }
        }
        nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
        // the orbiter and the path point
        float fx, fy;
        pathAt(ot, fx, fy);
        fx *= size;
        fy *= size;
        float ox = clampf(fx + sr_ * cosf(TAU_ * oo), -1, 1), oy = clampf(fy + sr_ * sinf(TAU_ * oo), -1, 1);
        if (v.use_pos) { fx = v.fx; fy = v.fy; ox = v.ox; oy = v.oy; }
        const Col oc = ORB_COLS[colStep(v.sub_size)], pc = PATH_COLS[colStep(v.size)];
        if (v.motion) {
            // the light trail: where the orbiter has just been, a fading glowing curve
            float tl = fabsf(v.sub) > 0.01f ? 0.6f / fabsf(v.sub) : 0.35f;
            tl = clampf(tl, 0.06f, 0.6f);
            const int N = 40;
            float lx = sx(ox), ly = sy(oy);
            nvgGlobalCompositeBlendFunc(ctx, NVG_SRC_ALPHA, NVG_ONE);
            lineCap(NanoVG::ROUND);
            for (int i = 1; i < N; i++) {
                const float tk = tl * i / (N - 1);
                float px, py;
                pathAt(ot - v.hz * tk, px, py);
                px *= size;
                py *= size;
                const float b = oo - v.sub * tk;
                const float X = sx(clampf(px + sr_ * cosf(TAU_ * b), -1, 1)), Y = sy(clampf(py + sr_ * sinf(TAU_ * b), -1, 1));
                const float f = 1.f - (float)i / N;
                for (int pass = 0; pass < 2; pass++) {
                    beginPath();
                    moveTo(lx, ly);
                    lineTo(X, Y);
                    strk(oc, (int)(255 * (pass ? (0.15f + 0.85f * f * f) : 0.40f * f * f)), (pass ? (1.2f + 5.5f * f * f) : (4 + 18 * f * f)) * k);
                    stroke();
                }
                lx = X;
                ly = Y;
            }
            nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
            const bool behind = sinf(TAU_ * oo) < 0;
            auto ball = [&](float x, float y, float rad, Col col, float glow) {
                const float gx = sx(x), gy = sy(y);
                nvgGlobalCompositeBlendFunc(ctx, NVG_SRC_ALPHA, NVG_ONE);
                Paint halo = radialGradient(gx, gy, rad * 0.4f, rad + glow * k, Color(col.r, col.g, col.b, 120), Color(col.r, col.g, col.b, 0));
                beginPath();
                circle(gx, gy, rad + glow * k);
                fillPaint(halo);
                fill();
                nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
                Paint rg = radialGradient(gx - rad * 0.3f, gy - rad * 0.3f, rad * 0.1f, rad * 1.05f, Color(255, 255, 255, 255),
                                          Color(col.r, col.g, col.b, 255));
                beginPath();
                circle(gx, gy, rad);
                fillPaint(rg);
                fill();
            };
            const float RO = S * 0.055f * (0.8f + 0.4f * v.env), RP = S * 0.038f;
            if (v.ps > 0.001f || v.pe < 0.999f)   // PATH START / END markers
                for (int q = 0; q < 2; q++) { float mx, my; pathAt(q ? v.pe : v.ps, mx, my); ball(mx * size, my * size, S * 0.014f, Col{255, 210, 74}, 10); }
            if (v.use_pos) for (int k = 0; k < v.nballs && k < 16; k++) ball(v.bx[k], v.by[k], RO * 0.7f, oc, 16);
            if (behind) { ball(ox, oy, RO, oc, 26); ball(fx, fy, RP, pc, 18); }
            else { ball(fx, fy, RP, pc, 18); ball(ox, oy, RO, oc, 26); }
        }
    } else {
        text_(CX, CY, "waiting for the engine", 16, ALIGN_CENTER | ALIGN_MIDDLE, Col{90, 140, 200});
    }
    // the corner bars: how much of each corner is heard
    {
        const float bh = std::max(3.f, 4 * k), by = r.y + r.h - barsH + (barsH - bh) / 2 - 2;
        const char *names[4] = {"A", "B", "C", "D"};
        for (int i = 0; i < 4; i++) {
            const float x0 = r.x + 14 + i * ((r.w - 28) / 4), w = (r.w - 28) / 4 - 14;
            beginPath();
            rect(x0 + 18, by, w - 18, bh);
            fillColor(40, 60, 90, 205);
            fill();
            const float wv = clampf(fView.w[i], 0, 1);
            if (wv > 0.002f) {
                glowLine(x0 + 18, by + bh / 2, x0 + 18 + (w - 18) * wv, by + bh / 2, CYAN, bh, 0.9f);
            }
            text_(x0, by + bh / 2, names[i], 14, ALIGN_LEFT | ALIGN_MIDDLE, Col{90, 140, 200});
        }
    }
    // scanlines
    {
        beginPath();
        for (float y = r.y + 1; y < r.y + r.h; y += 3) rect(r.x, y, r.w, 1);
        fillColor(0, 0, 0, 30);
        fill();
    }
    restore();
}

void VaderUI::drawEnv(const R &r) {
    const float a = fv[VGP_ATTACK], d = fv[VGP_DECAY], sus = fv[VGP_SUSTAIN], rel = fv[VGP_RELEASE];
    const float A = 0.04f + a * a * 1.2f, D = 0.04f + d * d * 1.2f, H = 0.5f, Rl = 0.04f + rel * rel * 1.2f;
    const float tot = A + D + H + Rl;
    auto env = [&](float u) {
        if (u < A) return u / A;
        u -= A;
        if (u < D) return 1 - (1 - sus) * u / D;
        u -= D;
        if (u < H) return sus;
        u -= H;
        return u < Rl ? sus * (1 - u / Rl) : 0.f;
    };
    save();
    intersectScissor(r.x, r.y, r.w, r.h);
    beginPath();
    for (int i = 0; i <= 160; i++) {
        const float u = tot * i / 160.f;
        const float X = r.x + 8 + (r.w - 16) * i / 160.f, Y = r.y + r.h - 6 - env(u) * (r.h - 12);
        if (i) lineTo(X, Y); else moveTo(X, Y);
    }
    NVGcontext *ctx = getContext();
    lineJoin(NanoVG::ROUND);
    lineCap(NanoVG::ROUND);
    nvgGlobalCompositeBlendFunc(ctx, NVG_SRC_ALPHA, NVG_ONE);
    const float ws[3] = {9, 5, 3};
    const int al[3] = {26, 60, 255};
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 2) nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
        strk(pass == 2 ? Col{216, 236, 255} : ACCENT, al[pass], ws[pass]);
        stroke();
    }
    nvgGlobalCompositeOperation(ctx, NVG_SOURCE_OVER);
    restore();
}

/* ============================================================================================================ controls */
void VaderUI::drawCtl(const VgCtl &c) {
    if (!visible(c)) return;
    const R r = rectOf(c);
    char val[96];
    switch (c.kind) {
    case VGK_TITLE: {
        text_(c.x, c.y + 9, c.label, 18, ALIGN_LEFT | ALIGN_MIDDLE, ACCENT);
        const float tw = textW(c.label, 18);
        if (c.w > tw + 16) {
            beginPath();
            moveTo(c.x + tw + 10, c.y + 10.5f);
            lineTo(c.x + c.w, c.y + 10.5f);
            strk(TRACK, 255, 1);
            stroke();
        }
        break;
    }
    case VGK_TEXT: {
        const Col col = {(uint8_t)(c.colour >> 16), (uint8_t)(c.colour >> 8), (uint8_t)c.colour};
        text_(c.x, c.y + c.size * 0.55f, c.label, (float)c.size, ALIGN_LEFT | ALIGN_MIDDLE, col);
        break;
    }
    case VGK_LIVETEXT: {   // 0.8: the name of a mode knob follows the mode and the layer
        char nm[48] = "";
        if (c.aux >= 200) break;   // the CPU readout is the Force's; a computer's host has its own meter
        if (c.aux >= 100) {   // a corner's character control: named by the corner's voice
            const int idx = c.aux - 101, corner_ = idx / 4, k = idx % 4;
            const int eng = (int)lroundf(fv[VGP_A_ENG + 0 + corner_ * 5]);
            if (eng > 0 && eng <= VG_NENG) snprintf(nm, sizeof nm, "%s", VG_ENG_KNOBS[eng - 1][k]);
        } else {
            const int mode = (int)lroundf(fv[VGP_SIM_MODE]), layer = (int)lroundf(fv[VGP_SIM_LAYER]);
            if (mode > 0 && mode < VG_NMODES) {
                if (layer == 0) snprintf(nm, sizeof nm, "%s", VG_MODE_KNOBS[mode][c.aux - 1]);
                else snprintf(nm, sizeof nm, "L%d K%d", layer + 1, c.aux);
            }
        }
        fitText(c.x, c.y + 13, c.w - 8, nm, 22.f, ALIGN_LEFT | ALIGN_MIDDLE, INK);
        break;
    }
    case VGK_ENVWIN:
        drawEnv(r);
        break;
    case VGK_SCREEN:
        drawScreen(r);
        break;
    case VGK_HLINE: {
        const Bind b = bindOf(c);
        if (!b.n) break;
        const float vw = c.big ? 64.f : 56.f, ly = c.big ? 32.f : 30.f, lpx = c.big ? 22.f : 18.f, vpx = c.big ? 26.f : 22.f;
        char lab[96];
        if (c.front) snprintf(lab, sizeof lab, "%s  [%s]", c.label, VG_CORNER_NAME[corner()]);
        else snprintf(lab, sizeof lab, "%s", c.label);
        fitText(c.x, c.y + 13, c.w - vw - 6, lab, lpx, ALIGN_LEFT | ALIGN_MIDDLE, INK);
        valueText(b, val, sizeof val);
        fitText(c.x + c.w, c.y + (c.big ? 33.f : 31.f), vw + 4, val, vpx, ALIGN_RIGHT | ALIGN_MIDDLE, INK, 255, false);
        const float len = lineLen(c), yy = c.y + ly, n = normOf(b);
        beginPath();
        moveTo(c.x + 2, yy);
        lineTo(c.x + len, yy);
        lineCap(NanoVG::ROUND);
        strk(TRACK, 255, 2);
        stroke();
        if (n > 0.004f) glowLine(c.x + 3, yy, c.x + 3 + (len - 6) * n, yy, ACCENT, c.big ? 5.f : 4.f);
        break;
    }
    case VGK_FADER: {
        const Bind b = bindOf(c);
        if (!b.n) break;
        const float fh = 120, top = c.y + (c.big ? 30.f : 28.f), cx = c.x + c.w / 2.f;
        fitText(c.x + c.w / 2, c.y + 12, c.w - 2, c.label, c.big ? 20.f : 17.f, ALIGN_CENTER | ALIGN_MIDDLE, INK);
        beginPath();
        moveTo(cx, top + 2);
        lineTo(cx, top + fh - 3);
        lineCap(NanoVG::ROUND);
        strk(TRACK, 255, 2);
        stroke();
        const float n = normOf(b);
        if (n > 0.004f) glowLine(cx, top + fh - 4, cx, top + fh - 4 - (fh - 8) * n, ACCENT, 5.f);
        valueText(b, val, sizeof val);
        text_(c.x + c.w / 2, top + fh + 22, val, c.big ? 26.f : 22.f, ALIGN_CENTER | ALIGN_MIDDLE, INK, 255, false);
        break;
    }
    case VGK_TOGGLE:
    case VGK_BUTTON: {
        const Bind b = bindOf(c);
        if (!b.n) break;
        const bool on = c.kind == VGK_TOGGLE ? fv[b.par[0]] >= 0.5f : fPressedBtn == &c;
        drawButton(r, c.label, on, isHover(r));
        break;
    }
    case VGK_RADIO: {
        const Bind b = bindOf(c);
        if (!b.n) break;
        const int cur = (int)lroundf(fv[b.par[0]]);
        for (int slot = 0; slot < c.nlab; slot++) {
            const int o = c.order[slot];
            const R sr = radioSlot(c, slot);
            drawButton(sr, c.labels[o], o == cur, isHover(sr));
        }
        break;
    }
    case VGK_POPUP: {
        const Bind b = bindOf(c);
        if (!b.n) break;
        drawWindowBox(r);
        float tx = r.x + 12;
        if (c.label[0]) {
            text_(tx, r.y + r.h / 2, c.label, 18, ALIGN_LEFT | ALIGN_MIDDLE, INK_DIM);
            tx += textW(c.label, 18) + 14;
        }
        valueText(b, val, sizeof val);
        fitText(tx, r.y + r.h / 2 + 1, r.x + r.w - 40 - tx, val, 22, ALIGN_LEFT | ALIGN_MIDDLE, INK, 255, false);
        beginPath();
        moveTo(r.x + r.w - 26, r.y + r.h / 2 - 4);
        lineTo(r.x + r.w - 14, r.y + r.h / 2 - 4);
        lineTo(r.x + r.w - 20, r.y + r.h / 2 + 4);
        closePath();
        colr(ACCENT);
        fill();
        break;
    }
    case VGK_FIELD: {
        const Bind b = bindOf(c);
        if (!b.n) break;
        drawWindowBox(r);
        text_(r.x + 12, r.y + r.h / 2, c.label, 18, ALIGN_LEFT | ALIGN_MIDDLE, INK_DIM);
        const float tx = r.x + 12 + textW(c.label, 18) + 14;
        valueText(b, val, sizeof val);
        fitText(tx, r.y + r.h / 2 + 1, r.x + r.w - 58 - tx, val, 24, ALIGN_LEFT | ALIGN_MIDDLE, INK, 255, false);
        text_(r.x + r.w - 38, r.y + r.h / 2, "<", 20, ALIGN_CENTER | ALIGN_MIDDLE, ACCENT);
        text_(r.x + r.w - 16, r.y + r.h / 2, ">", 20, ALIGN_CENTER | ALIGN_MIDDLE, ACCENT);
        break;
    }
    case VGK_METER: {
        const float v = clampf(fView.im[std::max(0, std::min(3, c.aux - 1))], 0, 1);
        text_(c.x, c.y + 10, c.label, 17, ALIGN_LEFT | ALIGN_MIDDLE, INK_DIM);
        const float yy = c.y + 22;
        beginPath();
        moveTo(c.x + 2, yy);
        lineTo(c.x + c.w - 2, yy);
        lineCap(NanoVG::ROUND);
        strk(TRACK, 255, 2);
        stroke();
        if (v > 0.004f) glowLine(c.x + 3, yy, c.x + 3 + (c.w - 6) * v, yy, ACCENT, 5.f);
        break;
    }
    default:
        break;
    }
}

void VaderUI::drawStripe() {
    // the mark and the name, then the line the Force shows across the top: the control last touched, with its value
    const float y0 = VG_TAB_H;
    text_(18, y0 + 24, "VADER", 36, ALIGN_LEFT | ALIGN_MIDDLE, Col{200, 228, 255});
    text_(20, y0 + 46, "L'CRONX", 12, ALIGN_LEFT | ALIGN_MIDDLE, Col{90, 140, 200});
    char line[96];
    if (fP) fP->uiReadout(line, sizeof line); else line[0] = 0;
    fitText(196, y0 + 21, 470, line[0] ? line : "VADER", 26, ALIGN_LEFT | ALIGN_MIDDLE, INK);
    beginPath();
    moveTo(198, y0 + 46);
    lineTo(666, y0 + 46);
    lineCap(NanoVG::ROUND);
    strk(TRACK, 255, 2);
    stroke();
    if (fReadLine > 0.004f) glowLine(199, y0 + 46, 199 + 466 * fReadLine, y0 + 46, ACCENT, 4.f);
    for (int i = 0; i < VG_STRIPE_N; i++) drawCtl(VG_STRIPE[i]);
}

void VaderUI::drawTabs() {
    const float tw = (float)VG_CANVAS_W / VG_NPAGES;
    for (int i = 0; i < VG_NPAGES; i++) {
        const R r = {i * tw, 0, tw, (float)VG_TAB_H};
        const bool on = i == fPage, hv = isHover(r);
        text_(r.x + r.w / 2, r.y + r.h / 2 - 1, VG_PAGE_NAME[i], 20, ALIGN_CENTER | ALIGN_MIDDLE, on ? INK : (hv ? INK_DIM : Col{110, 150, 200}));
        if (on) glowLine(r.x + 14, r.y + r.h - 4, r.x + r.w - 14, r.y + r.h - 4, ACCENT, 3.f);
        else if (i) {
            beginPath();
            moveTo(r.x, r.y + 10);
            lineTo(r.x, r.y + r.h - 10);
            strk(TRACK, 200, 1);
            stroke();
        }
    }
}

void VaderUI::drawPopup() {
    if (!fPop.open) return;
    const Pop &p = fPop;
    // dim everything behind
    beginPath();
    rect(0, 0, VG_CANVAS_W, VG_CANVAS_H);
    fillColor(0, 0, 0, 120);
    fill();
    beginPath();
    roundedRect(p.lx, p.ly, p.pw, p.ph, 12);
    fillColor(3, 8, 16, 250);
    fill();
    strk(ACCENT, 255, 2);
    stroke();
    const int cur = p.b.n ? (int)lroundf(fv[p.b.par[0]]) : -1;
    for (int o = 0; o < p.n; o++) {
        const R r = {p.lx + 7 + (o % p.cols) * (p.ow + 6.f), p.ly + 7 + (o / p.cols) * (p.oh + 6.f), (float)p.ow, (float)p.oh};
        char nm[96];
        if (p.preset) {
            if (!(fP && fP->uiPresetName(o, nm, sizeof nm))) snprintf(nm, sizeof nm, "%d", o + 1);
        } else {
            snprintf(nm, sizeof nm, "%s", VG_PARS[p.b.par[0]].opts[o]);
        }
        drawButton(r, nm, o == cur, isHover(r));
    }
}

void VaderUI::onNanoDisplay() {
    const float W = (float)getWidth(), H = (float)getHeight();
    fScale = std::min(W / VG_CANVAS_W, H / VG_CANVAS_H);
    fOx = (W - VG_CANVAS_W * fScale) / 2;
    fOy = (H - VG_CANVAS_H * fScale) / 2;
    // the ground, over the whole window
    beginPath();
    rect(0, 0, W, H);
    fillColor(4, 7, 13, 255);
    fill();
    save();
    translate(fOx, fOy);
    scale(fScale, fScale);
    intersectScissor(0, 0, VG_CANVAS_W, VG_CANVAS_H);
    {
        Paint g = radialGradient(VG_CANVAS_W * 0.5f, VG_CANVAS_H * 0.45f, 120, 900, Color(16, 30, 56, 255), Color(5, 8, 14, 255));
        beginPath();
        rect(0, 0, VG_CANVAS_W, VG_CANVAS_H);
        fillPaint(g);
        fill();
    }
    if (fP) {
        // the values: from the plugin (the engine changes some by itself: a preset, a corner)
        for (int i = 0; i < VG_NEXP; i++) fv[i] = fP->uiValue(i);
    }
    drawTabs();
    drawStripe();
    const VgCtl *page = VG_PAGE[fPage];
    // the screen first (behind), then the rest
    for (int i = 0; i < VG_PAGE_N[fPage]; i++) if (page[i].kind == VGK_SCREEN) drawCtl(page[i]);
    for (int i = 0; i < VG_PAGE_N[fPage]; i++) if (page[i].kind != VGK_SCREEN) drawCtl(page[i]);
    drawPopup();
    restore();
}

/* ============================================================================================================ input */
const VgCtl *VaderUI::hit(double x, double y, bool *stripe) const {
    for (int i = VG_STRIPE_N - 1; i >= 0; i--) {
        const VgCtl &c = VG_STRIPE[i];
        if (c.par < 0 && c.front == 0) continue;
        if (visible(c) && inside(rectOf(c), x, y)) { if (stripe) *stripe = true; return &c; }
    }
    const VgCtl *page = VG_PAGE[fPage];
    for (int i = VG_PAGE_N[fPage] - 1; i >= 0; i--) {
        const VgCtl &c = page[i];
        if (c.kind == VGK_TITLE || c.kind == VGK_TEXT || c.kind == VGK_ENVWIN || c.kind == VGK_SCREEN || c.kind == VGK_METER || c.kind == VGK_LIVETEXT) continue;
        if (!visible(c)) continue;
        if (c.kind == VGK_RADIO) {
            for (int s = 0; s < c.nlab; s++) if (inside(radioSlot(c, s), x, y)) return &c;
            continue;
        }
        if (inside(rectOf(c), x, y)) { if (stripe) *stripe = false; return &c; }
    }
    return nullptr;
}

void VaderUI::openPopup(const VgCtl &c, bool preset) {
    Pop p;
    p.open = true;
    p.c = &c;
    p.b = bindOf(c);
    p.preset = preset;
    if (preset) { p.n = 16; p.cols = 4; p.ow = 150; p.oh = 40; }
    else { p.n = VG_PARS[p.b.par[0]].nopts; p.cols = std::max(1, (int)c.cols); p.ow = c.ow; p.oh = c.oh; }
    const int rows = (p.n + p.cols - 1) / p.cols;
    p.pw = p.cols * (p.ow + 6.f) + 14;
    p.ph = rows * (p.oh + 6.f) + 14;
    float lx = preset ? (float)c.x : (float)c.lx, ly = preset ? (float)c.y + c.h + 4 : (float)c.ly;
    p.lx = std::min(std::max(4.f, lx), VG_CANVAS_W - p.pw - 4);
    p.ly = std::min(std::max((float)VG_TAB_H + 64, ly), VG_CANVAS_H - p.ph - 4);
    fPop = p;
}

void VaderUI::stepBound(const Bind &b, int dir, bool fine) {
    if (!b.n) return;
    const VgPar &P = VG_PARS[b.par[0]];
    float step = (P.nopts || P.isint) ? 1.f : (P.mx - P.mn) * (fine ? 0.002f : 0.01f);
    if (P.nopts) {   // a list wraps round
        int v = (int)lroundf(fv[b.par[0]]) + dir;
        if (v < 0) v = P.nopts - 1;
        if (v >= P.nopts) v = 0;
        beginEdit(b); setBound(b, (float)v); endEdit(b);
        return;
    }
    beginEdit(b);
    setBound(b, fv[b.par[0]] + dir * step);
    endEdit(b);
}

void VaderUI::clickCtl(const VgCtl &c, double x, double y, bool) {
    const Bind b = bindOf(c);
    if (!b.n && c.kind != VGK_BUTTON) return;
    const VgPar *P = b.n ? &VG_PARS[b.par[0]] : nullptr;
    switch (c.kind) {
    case VGK_TOGGLE:
        beginEdit(b);
        setBound(b, fv[b.par[0]] >= 0.5f ? 0.f : 1.f);
        endEdit(b);
        break;
    case VGK_BUTTON:
        fPressedBtn = &c;
        if (fP && c.par >= 0) fP->uiFire(c.par);
        break;
    case VGK_RADIO:
        for (int s = 0; s < c.nlab; s++)
            if (inside(radioSlot(c, s), x, y)) {
                beginEdit(b);
                setBound(b, (float)c.order[s]);
                endEdit(b);
            }
        break;
    case VGK_POPUP:
        openPopup(c, false);
        break;
    case VGK_FIELD: {
        const R r = rectOf(c);
        if (b.par[0] == VGP_PRESET && x < r.x + r.w - 56) { openPopup(c, true); break; }
        if (x >= r.x + r.w - 50) { stepBound(b, x < r.x + r.w - 27 ? -1 : +1, false); break; }
        fDrag = Drag{true, &c, b, fv[b.par[0]], 0, x, y, -1};
        beginEdit(b);
        break;
    }
    case VGK_HLINE:
    case VGK_FADER:
        if (P) {
            // a quick second click resets to the default
            const auto now = std::chrono::steady_clock::now();
            if (fLastClick == &c && now - fLastClickT < std::chrono::milliseconds(350)) {
                beginEdit(b);
                setBound(b, P->def);
                endEdit(b);
                fLastClick = nullptr;
                break;
            }
            fLastClick = &c;
            fLastClickT = now;
            fDrag = Drag{true, &c, b, fv[b.par[0]], 0, x, y, -1};
            beginEdit(b);
        }
        break;
    default:
        break;
    }
}

bool VaderUI::onMouse(const MouseEvent &ev) {
    double x, y;
    toCanvas(ev.pos.getX(), ev.pos.getY(), x, y);
    fMx = x;
    fMy = y;
    if (ev.button != 1) return false;
    if (!ev.press) {
        if (fDrag.on) { endEdit(fDrag.b); fDrag.on = false; }
        fPressedBtn = nullptr;
        return true;
    }
    if (fPop.open) {
        const Pop p = fPop;
        fPop.open = false;
        for (int o = 0; o < p.n; o++) {
            const R r = {p.lx + 7 + (o % p.cols) * (p.ow + 6.f), p.ly + 7 + (o / p.cols) * (p.oh + 6.f), (float)p.ow, (float)p.oh};
            if (inside(r, x, y)) {
                beginEdit(p.b);
                setBound(p.b, (float)o);
                endEdit(p.b);
                break;
            }
        }
        return true;
    }
    if (y < VG_TAB_H) {
        const int i = (int)(x / ((float)VG_CANVAS_W / VG_NPAGES));
        if (i >= 0 && i < VG_NPAGES) fPage = i;
        return true;
    }
    if (const VgCtl *c = hit(x, y)) { clickCtl(*c, x, y, true); return true; }
    return false;
}

bool VaderUI::onMotion(const MotionEvent &ev) {
    double x, y;
    toCanvas(ev.pos.getX(), ev.pos.getY(), x, y);
    fMx = x;
    fMy = y;
    if (!fDrag.on) return false;
    const VgCtl &c = *fDrag.c;
    const VgPar &P = VG_PARS[fDrag.b.par[0]];
    const bool fine = (ev.mod & kModifierShift) != 0;
    const double dx = x - fDrag.x0, dy = y - fDrag.y0;
    if (c.kind == VGK_FIELD) {   // a list or number: one step for every 14 px up or down
        fDrag.acc += (float)(fDrag.y0 - y);
        fDrag.y0 = y;
        while (fDrag.acc >= 14.f) { fDrag.acc -= 14.f; stepBound(fDrag.b, +1, false); }
        while (fDrag.acc <= -14.f) { fDrag.acc += 14.f; stepBound(fDrag.b, -1, false); }
        return true;
    }
    const double len = c.kind == VGK_FADER ? 120.0 : (double)lineLen(c) - 6;
    const double range = (double)(P.mx - P.mn);
    double move = c.kind == VGK_FADER ? -dy : dx;
    if (c.kind == VGK_HLINE && fabs(dy) > fabs(dx)) move = -dy;   // up and down works on a line too
    double v;
    if (P.isint || P.nopts) {
        const double pxPerStep = std::min(40.0, std::max(8.0, len / std::max(1.0, range)));
        v = fDrag.v0 + move / pxPerStep * (fine ? 0.2 : 1.0);
    } else {
        v = fDrag.v0 + move * range / len * (fine ? 0.1 : 1.0);
    }
    setBound(fDrag.b, (float)v);
    return true;
}

bool VaderUI::onScroll(const ScrollEvent &ev) {
    double x, y;
    toCanvas(ev.pos.getX(), ev.pos.getY(), x, y);
    if (fPop.open) return true;
    if (const VgCtl *c = hit(x, y)) {
        const Bind b = bindOf(*c);
        if (b.n && c->kind != VGK_BUTTON) {
            stepBound(b, ev.delta.getY() > 0 ? 1 : -1, (ev.mod & kModifierShift) != 0);
            return true;
        }
    }
    return false;
}

UI *createUI() {
    return new VaderUI();
}

END_NAMESPACE_DISTRHO
