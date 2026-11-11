#ifndef TCCRAD_CODEGEN_PRIV_H
#define TCCRAD_CODEGEN_PRIV_H

#include "codegen.h"

typedef struct Gen {
    const TrDoc *doc;
    const TrStyle *style;
    TrBuf *o;
    char *err;
    size_t errcap;
} Gen;

typedef struct CgSlot {
    char lv[96];
    char preload[512];
    int preload_on;
} CgSlot;

int cg_err(Gen *g, const char *msg);
int cg_gp(Gen *g, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
int cg_gi(Gen *g, int depth, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
int cg_cstr(Gen *g, const char *s);
int cg_text(Gen *g, const TrWidget *w, const char *key);
int cg_region(Gen *g, const char *tag, TrSpan sp, const char *fallback);
int cg_int_slot(Gen *g, const TrWidget *w, const char *key, CgSlot *s);
int cg_handler(const TrDoc *doc, const TrWidget *w, const TrEvent *ev, char *dst, size_t cap);
const TrEvent *cg_event(const TrWidget *w, const char *name);
int cg_nk(Gen *g);
int cg_libui(Gen *g);
int cg_lvgl(Gen *g);

#endif
