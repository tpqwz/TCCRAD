#ifndef TCCRAD_CODEGEN_H
#define TCCRAD_CODEGEN_H

#include "model.h"
#include "style.h"

/*
 * Emit a .h, a .c, and a backend main.c from a document.
 * User regions are preserved from the document spans.
 * err is optional. On failure it receives one sentence.
 */
int tr_codegen(const TrDoc *doc, const TrStyle *style, const char *include_name,
    TrBuf *hout, TrBuf *cout, char *err, size_t errcap);
int tr_codegen_main(const TrDoc *doc, const TrStyle *style, const char *include_name,
    TrBuf *mout, char *err, size_t errcap);

#endif
