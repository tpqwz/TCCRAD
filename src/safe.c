#define _POSIX_C_SOURCE 200809L

#include "safe.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
tr_buf_init(TrBuf *b)
{
    if (!b) return -1;
    b->data = (char *)malloc(1);
    if (!b->data) {
        b->len = 0;
        b->cap = 0;
        return -1;
    }
    b->data[0] = '\0';
    b->len = 0;
    b->cap = 1;
    return 0;
}

void
tr_buf_free(TrBuf *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void
tr_buf_clear(TrBuf *b)
{
    if (!b || !b->data || b->cap == 0) return;
    b->data[0] = '\0';
    b->len = 0;
}

int
tr_buf_reserve(TrBuf *b, size_t need_len)
{
    size_t need;
    size_t cap;
    char *p;
    if (!b || !b->data) return -1;
    if (need_len > SIZE_MAX - 1) return -1;
    need = need_len + 1;
    if (need <= b->cap) return 0;
    cap = b->cap < 16 ? 16 : b->cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) return -1;
        cap *= 2;
    }
    p = (char *)realloc(b->data, cap);
    if (!p) return -1;
    b->data = p;
    b->cap = cap;
    return 0;
}

int
tr_buf_append(TrBuf *b, const char *s, size_t n)
{
    if (!b) return -1;
    if (n == 0) return 0;
    if (!s) return -1;
    if (b->len > SIZE_MAX - n) return -1;
    if (tr_buf_reserve(b, b->len + n) != 0) return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

int
tr_buf_append_cstr(TrBuf *b, const char *s)
{
    if (!s) return -1;
    return tr_buf_append(b, s, strlen(s));
}

int
tr_buf_vprintf(TrBuf *b, const char *fmt, va_list ap)
{
    va_list aq;
    int n;
    if (!b || !fmt) return -1;
    va_copy(aq, ap);
    n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0) return -1;
    if (b->len > SIZE_MAX - (size_t)n) return -1;
    if (tr_buf_reserve(b, b->len + (size_t)n) != 0) return -1;
    va_copy(aq, ap);
    if (vsnprintf(b->data + b->len, b->cap - b->len, fmt, aq) != n) {
        va_end(aq);
        return -1;
    }
    va_end(aq);
    b->len += (size_t)n;
    return 0;
}

int
tr_buf_printf(TrBuf *b, const char *fmt, ...)
{
    va_list ap;
    int rc;
    if (!b || !fmt) return -1;
    va_start(ap, fmt);
    rc = tr_buf_vprintf(b, fmt, ap);
    va_end(ap);
    return rc;
}

int
tr_copy_cap(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!dst || cap == 0) return -1;
    if (!src) src = "";
    for (i = 0; i + 1 < cap && src[i] != '\0'; i++) dst[i] = src[i];
    dst[i] = '\0';
    return src[i] == '\0' ? 0 : -1;
}

static int
ident_start(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int
ident_char(unsigned char c)
{
    return ident_start(c) || (c >= '0' && c <= '9');
}

int
tr_is_ident(const char *s)
{
    size_t i;
    if (!s || !ident_start((unsigned char)s[0])) return 0;
    for (i = 1; s[i] != '\0'; i++) {
        if (i >= 63) return 0;
        if (!ident_char((unsigned char)s[i])) return 0;
    }
    if (tr_is_keyword(s)) return 0;
    return 1;
}

int
tr_is_keyword(const char *s)
{
    static const char *const words[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while", "_Bool", "_Complex",
        "_Imaginary", "ui", "ctx", "row", "col", "dst", "cap", "parent", NULL
    };
    size_t i;
    if (!s) return 0;
    for (i = 0; words[i]; i++) {
        if (strcmp(s, words[i]) == 0) return 1;
    }
    return 0;
}

int
tr_expr_ok(const char *s)
{
    size_t n;
    size_t i;
    if (!s || s[0] == '\0') return 0;
    n = strlen(s);
    if (n > 95) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126) return 0;
        if (c == '#' ) return 0;
    }
    if (strstr(s, "*/") || strstr(s, "//")) return 0;
    return 1;
}

int
tr_read_file(const char *path, char **out, size_t *out_len, size_t max_len)
{
    FILE *f;
    char *buf;
    size_t cap;
    size_t len;
    if (!path || !out || !out_len || max_len < 1) return -1;
    *out = NULL;
    *out_len = 0;
    f = fopen(path, "rb");
    if (!f) return -1;
    cap = 4096;
    if (cap > max_len + 1) cap = max_len + 1;
    buf = (char *)malloc(cap);
    if (!buf) {
        fclose(f);
        return -1;
    }
    len = 0;
    for (;;) {
        size_t room;
        size_t got;
        if (len > max_len) {
            free(buf);
            fclose(f);
            return -1;
        }
        if (len + 1 >= cap) {
            size_t nc;
            char *nb;
            if (cap > SIZE_MAX / 2) {
                free(buf);
                fclose(f);
                return -1;
            }
            nc = cap * 2;
            if (nc > max_len + 1) nc = max_len + 1;
            if (nc <= cap) {
                free(buf);
                fclose(f);
                return -1;
            }
            nb = (char *)realloc(buf, nc);
            if (!nb) {
                free(buf);
                fclose(f);
                return -1;
            }
            buf = nb;
            cap = nc;
        }
        room = cap - len - 1;
        got = fread(buf + len, 1, room, f);
        len += got;
        if (got < room) break;
    }
    if (ferror(f)) {
        free(buf);
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        free(buf);
        return -1;
    }
    buf[len] = '\0';
    if (strlen(buf) != len) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = len;
    return 0;
}

int
tr_write_file(const char *path, const char *data, size_t n)
{
    char tmp[TR_PATH_MAX];
    FILE *f;
    int nw;
    if (!path || (!data && n > 0)) return -1;
    if (!data) data = "";
    nw = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (nw < 0 || (size_t)nw >= sizeof tmp) return -1;
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if (n > 0 && fwrite(data, 1, n, f) != n) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int
tr_path_dirname(const char *path, char *dst, size_t cap)
{
    const char *slash;
    size_t n;
    if (!path || !dst || cap == 0) return -1;
    slash = strrchr(path, '/');
    if (!slash) return tr_copy_cap(dst, cap, ".");
    if (slash == path) return tr_copy_cap(dst, cap, "/");
    n = (size_t)(slash - path);
    if (n + 1 > cap) return -1;
    memcpy(dst, path, n);
    dst[n] = '\0';
    return 0;
}

int
tr_path_basename(const char *path, char *dst, size_t cap)
{
    const char *slash;
    if (!path || !dst) return -1;
    slash = strrchr(path, '/');
    return tr_copy_cap(dst, cap, slash ? slash + 1 : path);
}

int
tr_path_join(char *dst, size_t cap, const char *dir, const char *name)
{
    int n;
    if (!dst || !dir || !name || cap == 0) return -1;
    if (name[0] == '\0' || strchr(name, '/') || strstr(name, "..")) return -1;
    n = snprintf(dst, cap, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}
