/* LXRXXL shim: portable C versions of the Cortex-M intrinsics the LXR DSP uses (the firmware's
 * Core/compat/cmsis_intrinsics.h wraps the ARM instructions in inline asm). Same results, bit for bit:
 * signed/unsigned saturation, the saturating adds, and CLZ returning 32 for 0 (the Euklid generator relies on it). */
#ifndef CMSIS_INTRINSICS_H_
#define CMSIS_INTRINSICS_H_
#include <stdint.h>
static inline int32_t lxr_ssat(int32_t v, int bits) {
    const int32_t mx = (int32_t)((1u << (bits - 1)) - 1u), mn = -mx - 1;
    return v > mx ? mx : (v < mn ? mn : v);
}
static inline uint32_t lxr_usat(int32_t v, int bits) {
    const int32_t mx = bits >= 31 ? INT32_MAX : (int32_t)((1u << bits) - 1u);
    return (uint32_t)(v > mx ? mx : (v < 0 ? 0 : v));
}
#define __SSAT(ARG1, ARG2) lxr_ssat((int32_t)(ARG1), (ARG2))
#define __USAT(ARG1, ARG2) lxr_usat((int32_t)(ARG1), (ARG2))
static inline uint32_t __QADD16(uint32_t a, uint32_t b) {
    int32_t lo = lxr_ssat((int32_t)(int16_t)a + (int16_t)b, 16), hi = lxr_ssat((int32_t)(int16_t)(a >> 16) + (int16_t)(b >> 16), 16);
    return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo;
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b) {
    int32_t lo = lxr_ssat((int32_t)(int16_t)a - (int16_t)b, 16), hi = lxr_ssat((int32_t)(int16_t)(a >> 16) - (int16_t)(b >> 16), 16);
    return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo;
}
static inline int32_t __QADD(int32_t a, int32_t b) { int64_t r = (int64_t)a + b; return r > INT32_MAX ? INT32_MAX : r < INT32_MIN ? INT32_MIN : (int32_t)r; }
static inline int32_t __QSUB(int32_t a, int32_t b) { int64_t r = (int64_t)a - b; return r > INT32_MAX ? INT32_MAX : r < INT32_MIN ? INT32_MIN : (int32_t)r; }
static inline uint8_t lxr_clz(uint32_t v) { return v ? (uint8_t)__builtin_clz(v) : 32u; }
#define __CLZ(value) lxr_clz((uint32_t)(value))
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
#endif
#endif
