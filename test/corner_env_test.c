/* 0.10.5: the per-corner envelope through the Force wrapper (vst2_wrap.c) with the real engine: the values MPC reads back
 * (getParameter) follow CORNER SELECT, and the wrapper reports them to the host (audioMasterAutomate). test/run_tests.sh runs it. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
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
static float reported[NPARAMS]; static int nrep[NPARAMS];
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){ if (op == 0 && i >= 0 && i < NPARAMS) { reported[i] = o; nrep[i]++; } return 0; }
static void run(AEffect *a, int blocks) { float L[128], R[128], *o[2] = {L, R}; for (int k = 0; k < blocks; k++) a->pr(a, 0, o, 128); }
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
int main(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, 0, 0); a->d(a, 10, 0, 0, 0, 44100.f); a->d(a, 11, 0, 128, 0, 0); a->d(a, 12, 0, 1, 0, 0);
    int co = idx("corner"), at = idx("attack"), aa = idx("a_attack"), ba = idx("b_attack");
    int fails = 0;
    #define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
    a->setP(a, co, 0.f); run(a, 20);
    a->setP(a, at, 0.8f); run(a, 20);
    CHECK(fabsf(a->getP(a, aa) - 0.8f) < 1e-3f && fabsf(a->getP(a, ba) - 0.05f) < 1e-3f, "corner A: ATTACK 0.8 goes to A only (a %.2f b %.2f)", a->getP(a, aa), a->getP(a, ba));
    nrep[at] = 0;
    a->setP(a, co, 0.25f); run(a, 20);   /* B */
    CHECK(fabsf(a->getP(a, at) - 0.05f) < 1e-3f && nrep[at] > 0 && fabsf(reported[at] - 0.05f) < 1e-3f, "corner B: ATTACK reads B's 0.05 and the host was told (%.2f, %d report(s) of %.2f)", a->getP(a, at), nrep[at], reported[at]);
    a->setP(a, at, 0.2f); run(a, 20);
    a->setP(a, co, 0.f); run(a, 20);
    CHECK(fabsf(a->getP(a, at) - 0.8f) < 1e-3f, "back on A: ATTACK reads 0.8 (%.2f)", a->getP(a, at));
    a->setP(a, co, 0.25f); run(a, 20);
    CHECK(fabsf(a->getP(a, at) - 0.2f) < 1e-3f, "B again: 0.2 (%.2f)", a->getP(a, at));
    a->setP(a, co, 1.f); run(a, 20); a->setP(a, at, 0.5f); run(a, 20);
    CHECK(fabsf(a->getP(a, aa) - 0.5f) < 1e-3f && fabsf(a->getP(a, ba) - 0.5f) < 1e-3f, "ALL: every corner 0.5");
    /* MPC restoring a project pushes every parameter by index; the state chunk carries the truth and comes too */
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
