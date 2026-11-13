#ifndef TCCRAD_ROPE_H
#define TCCRAD_ROPE_H

#include <stddef.h>

struct TrRope;

struct TrRope *tr_rope_new(const char *s, size_t n);
void tr_rope_free(struct TrRope *r);
size_t tr_rope_len(const struct TrRope *r);
int tr_rope_insert(struct TrRope **rp, size_t at, const char *s, size_t n);
int tr_rope_delete(struct TrRope **rp, size_t at, size_t n);
int tr_rope_at(const struct TrRope *r, size_t i);
char *tr_rope_flat(const struct TrRope *r, size_t *n);

#endif
