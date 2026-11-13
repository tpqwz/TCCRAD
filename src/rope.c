#include "rope.h"

#include <stdlib.h>
#include <string.h>

#define TR_LEAF 128

struct TrRope {
    struct TrRope *a;
    struct TrRope *b;
    char *s;
    size_t n;
    size_t cap;
};

static struct TrRope *
leaf_new(const char *s, size_t n)
{
    struct TrRope *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->cap = n < TR_LEAF ? TR_LEAF : n;
    r->s = malloc(r->cap);
    if (!r->s) {
        free(r);
        return NULL;
    }
    if (n && s) memcpy(r->s, s, n);
    r->n = n;
    return r;
}

static struct TrRope *
branch_new(struct TrRope *a, struct TrRope *b)
{
    struct TrRope *r;
    if (!a) return b;
    if (!b) return a;
    r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->a = a;
    r->b = b;
    r->n = a->n + b->n;
    return r;
}

struct TrRope *
tr_rope_new(const char *s, size_t n)
{
    if (!s) n = 0;
    return leaf_new(s, n);
}

void
tr_rope_free(struct TrRope *r)
{
    if (!r) return;
    tr_rope_free(r->a);
    tr_rope_free(r->b);
    free(r->s);
    free(r);
}

size_t
tr_rope_len(const struct TrRope *r)
{
    return r ? r->n : 0;
}

int
tr_rope_at(const struct TrRope *r, size_t i)
{
    if (!r || i >= r->n) return -1;
    if (r->s) return (unsigned char)r->s[i];
    if (r->a && i < r->a->n) return tr_rope_at(r->a, i);
    if (!r->b) return -1;
    return tr_rope_at(r->b, i - (r->a ? r->a->n : 0));
}

static char *
flat_into(const struct TrRope *r, char *dst)
{
    if (!r) return dst;
    if (r->s) {
        if (r->n) memcpy(dst, r->s, r->n);
        return dst + r->n;
    }
    dst = flat_into(r->a, dst);
    return flat_into(r->b, dst);
}

char *
tr_rope_flat(const struct TrRope *r, size_t *n)
{
    char *s;
    size_t len = tr_rope_len(r);
    s = malloc(len + 1);
    if (!s) return NULL;
    flat_into(r, s);
    s[len] = '\0';
    if (n) *n = len;
    return s;
}

int
tr_rope_insert(struct TrRope **rp, size_t at, const char *s, size_t n)
{
    struct TrRope *r;
    if (!rp || (!s && n)) return -1;
    if (n == 0) return 0;
    r = *rp;
    if (!r) {
        *rp = leaf_new(s, n);
        return *rp ? 0 : -1;
    }
    if (at > r->n) at = r->n;
    if (r->s) {
        if (r->n + n <= r->cap && r->n + n <= TR_LEAF) {
            memmove(r->s + at + n, r->s + at, r->n - at);
            memcpy(r->s + at, s, n);
            r->n += n;
            return 0;
        }
        {
            struct TrRope *L = leaf_new(r->s, at);
            struct TrRope *R = leaf_new(r->s + at, r->n - at);
            struct TrRope *M = leaf_new(s, n);
            if (!L || !R || !M) return -1;
            free(r->s);
            free(r);
            *rp = branch_new(L, branch_new(M, R));
            return *rp ? 0 : -1;
        }
    }
    if (r->a && at <= r->a->n) {
        if (tr_rope_insert(&r->a, at, s, n) != 0) return -1;
    } else if (r->b) {
        size_t left = r->a ? r->a->n : 0;
        if (tr_rope_insert(&r->b, at - left, s, n) != 0) return -1;
    }
    r->n = tr_rope_len(r->a) + tr_rope_len(r->b);
    return 0;
}

int
tr_rope_delete(struct TrRope **rp, size_t at, size_t n)
{
    struct TrRope *r;
    if (!rp || !*rp || n == 0) return 0;
    r = *rp;
    if (at >= r->n) return 0;
    if (at + n > r->n) n = r->n - at;
    if (r->s) {
        memmove(r->s + at, r->s + at + n, r->n - at - n);
        r->n -= n;
        if (r->n == 0) {
            free(r->s);
            free(r);
            *rp = NULL;
        }
        return 0;
    }
    if (r->a && at < r->a->n) {
        size_t leftn = r->a->n - at;
        if (leftn > n) leftn = n;
        tr_rope_delete(&r->a, at, leftn);
        if (n > leftn && r->b) tr_rope_delete(&r->b, 0, n - leftn);
    } else if (r->b) {
        size_t left = r->a ? r->a->n : 0;
        tr_rope_delete(&r->b, at - left, n);
    }
    if (r->a && r->a->n == 0) {
        tr_rope_free(r->a);
        r->a = NULL;
    }
    if (r->b && r->b->n == 0) {
        tr_rope_free(r->b);
        r->b = NULL;
    }
    if (!r->a) {
        *rp = r->b;
        free(r);
        return 0;
    }
    if (!r->b) {
        *rp = r->a;
        free(r);
        return 0;
    }
    r->n = r->a->n + r->b->n;
    return 0;
}
