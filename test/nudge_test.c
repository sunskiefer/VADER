/* 0.10.0: the MPC host writes 1.0 or 0.0 to parameter 0 and straight back each time it prepares the plugin (Akai's SDK,
 * docs/host-contract.md). Parameter 0 is the focused corner's OVERTONE GEN 1; with CORNER SELECT on ALL that must not
 * copy corner A's GEN 1 onto the others. Also: a real turn to the end stop still arrives (with the next audio block). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
extern AEffect* VSTPluginMain(cb);
static double ti[16];
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){ if (op == 7) { ti[4] = 120.0; ((int32_t*)&ti[8])[5] = 1 << 10; return (intptr_t)ti; } return 0; }
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
static int fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static void nudge(AEffect *a) { float old = a->getP(a, 0); a->setP(a, 0, old < 0.5f ? 1.0f : 0.0f); a->setP(a, 0, old); }
static void run(AEffect *a) { float L[128], R[128], *o[2] = {L, R}; a->pr(a, 0, o, 128); }
int main(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, 0, 0); a->d(a, 10, 0, 0, 0, 44100.f); a->d(a, 11, 0, 128, 0, 0); a->d(a, 12, 0, 1, 0, 0);
    int ia = idx("a_og1"), ib = idx("b_og1"), ic = idx("corner");
    CHECK(!strcmp(PARAMS[0].key, "f_og1") && ia > 0 && ib > 0 && ic > 0, "parameter 0 is f_og1");
    a->setP(a, ia, 0.2f); a->setP(a, ib, 0.8f); a->setP(a, ic, 1.0f); run(a);   /* CORNER SELECT: ALL */
    for (int k = 0; k < 3; k++) { nudge(a); run(a); }
    CHECK(fabsf(a->getP(a, ia) - 0.2f) < 1e-3f && fabsf(a->getP(a, ib) - 0.8f) < 1e-3f, "the host's nudge leaves the corners alone (A %.2f, B %.2f)", a->getP(a, ia), a->getP(a, ib));
    a->setP(a, 0, 1.0f);
    CHECK(a->getP(a, 0) == 1.0f, "a turn to the end stop reads back at once");
    run(a);
    CHECK(fabsf(a->getP(a, ia) - 1.0f) < 1e-3f && fabsf(a->getP(a, ib) - 1.0f) < 1e-3f, "and reaches the corners with the next block (A %.2f, B %.2f)", a->getP(a, ia), a->getP(a, ib));
    a->setP(a, 0, 0.0f); a->setP(a, 0, 0.5f); run(a);
    CHECK(fabsf(a->getP(a, ia) - 0.5f) < 1e-3f, "a sweep through the end stop ends where it was left (A %.2f)", a->getP(a, ia));
    a->d(a, 1, 0, 0, 0, 0);
    printf(fails ? "nudge_test: %d FAILED\n" : "nudge_test: all ok\n", fails);
    return fails != 0;
}
