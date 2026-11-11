#ifndef TCCRAD_STYLE_H
#define TCCRAD_STYLE_H

#include <stddef.h>

#define TR_STYLE_COLORS 32
#define TR_STYLE_NAME_MAX 48

typedef struct TrColor {
    unsigned char r, g, b, a;
} TrColor;

typedef struct TrStyle {
    char name[TR_STYLE_NAME_MAX];
    TrColor color[TR_STYLE_COLORS];
    int font_px;
} TrStyle;

const char *tr_style_color_name(int index);
void tr_style_defaults(TrStyle *st, const char *name);
int tr_style_name_ok(const char *name);
int tr_style_load(TrStyle *st, const char *dir, const char *name, char *err, size_t errcap);
int tr_style_save(const TrStyle *st, const char *dir, char *err, size_t errcap);

#endif
