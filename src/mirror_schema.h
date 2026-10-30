/* PLUG: a number that is the same only for two builds with the same parameters (the Force plugin and the desktop plugin
 * check it when plugging in). Shared by src/web.c and desktop/view_bus.c so it can't drift. */
#pragma once
#include "vader_params.h"
static inline unsigned mirror_schema(void) {
    unsigned h = 2166136261u ^ (unsigned)NP;
    for (int i = 0; i < NP; i++)
        if (VP[i].kind == K_SND || VP[i].kind == K_GLOB || VP[i].kind == K_TRIG)
            for (const char *k = VP[i].key; *k; k++) h = (h ^ (unsigned char)*k) * 16777619u;
    return h;
}
