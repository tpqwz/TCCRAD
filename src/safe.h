#ifndef TCCRAD_SAFE_H
#define TCCRAD_SAFE_H

/*
 * Bounded strings, checked allocation, and whole-file IO.
 * Callers pass a format literal to tr_buf_printf. User text is never a format.
 */

#include <stddef.h>
#include <stdarg.h>

#define TR_PATH_MAX 4096

typedef struct TrBuf {
    char *data;
    size_t len;
    size_t cap;
} TrBuf;

int tr_buf_init(TrBuf *b);
void tr_buf_free(TrBuf *b);
void tr_buf_clear(TrBuf *b);
int tr_buf_reserve(TrBuf *b, size_t need_len);
int tr_buf_append(TrBuf *b, const char *s, size_t n);
int tr_buf_append_cstr(TrBuf *b, const char *s);
int tr_buf_vprintf(TrBuf *b, const char *fmt, va_list ap);
int tr_buf_printf(TrBuf *b, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

int tr_copy_cap(char *dst, size_t cap, const char *src);
int tr_is_ident(const char *s);
int tr_is_keyword(const char *s);
int tr_expr_ok(const char *s);

int tr_read_file(const char *path, char **out, size_t *out_len, size_t max_len);
int tr_write_file(const char *path, const char *data, size_t n);

int tr_path_dirname(const char *path, char *dst, size_t cap);
int tr_path_basename(const char *path, char *dst, size_t cap);
int tr_path_join(char *dst, size_t cap, const char *dir, const char *name);

#endif
