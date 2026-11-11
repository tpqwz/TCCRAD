#ifndef TCCRAD_H
#define TCCRAD_H

/*
 * Runtime helpers for code tccrad generates.
 * The IDE is separate from this header. Generated programs include it.
 * Helpers are static inline so a generated file does not need a runtime library.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TCCRAD_BACKEND_NUKLEAR 1
#define TCCRAD_BACKEND_LIBUI 2
#define TCCRAD_BACKEND_LVGL 3

#ifndef TCCRAD_BACKEND
#define TCCRAD_BACKEND TCCRAD_BACKEND_NUKLEAR
#endif

/* Generated loops refuse to run past this, whatever the binding says. */
#define TCCRAD_LOOP_MAX 10000

static inline int
tr_bound_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Scale a design length. scale_milli is thousandths (1000 == 1.0). */
static inline int
tr_unit(int v, int scale_milli)
{
    int neg = 0;
    long long s;
    long long r;
    if (v < 0) {
        if (v == INT32_MIN) v = INT32_MIN + 1;
        neg = 1;
        v = -v;
    }
    s = scale_milli;
    if (s < 100) s = 100;
    if (s > 8000) s = 8000;
    r = ((long long)v * s) / 1000;
    if (r > 1000000LL) r = 1000000LL;
    if (neg) r = -r;
    return (int)r;
}

/* Positive design size, or parent_px - gap when v is negative. */
static inline int
tr_span(int parent_px, int v_px)
{
    int gap;
    if (v_px >= 0) return v_px;
    if (v_px == INT32_MIN) v_px = INT32_MIN + 1;
    gap = -v_px;
    if (parent_px <= gap) return 0;
    return parent_px - gap;
}

static inline void
tr_copy(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1 < cap && src[i] != '\0'; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static inline int
tr_sep_count(const char *s, char sep)
{
    int n = 0;
    if (!s || s[0] == '\0') return 0;
    n = 1;
    for (; *s != '\0'; s++) {
        if (*s == sep) {
            if (n == 256) return n;
            n++;
        }
    }
    return n;
}

static inline int
tr_cstr_len(const char *s, int cap)
{
    int n = 0;
    if (!s || cap < 1) return 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

#endif
