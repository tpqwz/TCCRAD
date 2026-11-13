#ifndef TCCRAD_REPORT_H
#define TCCRAD_REPORT_H

#include <stddef.h>

#define TR_EXPLORE_MAX 80
#define TR_CALL_MAX 12
#define TR_DECIDE_MAX 16
#define TR_FIND_MAX 48

typedef struct TrExplore {
    char name[48];
    int line;
    int lines;
    int complexity;
    int depth;
    int fan_in;
    int fan_out;
    int user;
    int callers[TR_CALL_MAX];
    int ncallers;
    int callees[TR_CALL_MAX];
    int callee_line[TR_CALL_MAX];
    int ncallees;
    int caller_line[TR_CALL_MAX];
    int decide_line[TR_DECIDE_MAX];
    int decide_depth[TR_DECIDE_MAX];
    int ndecide;
    int ndecide_all;
    char decide[TR_DECIDE_MAX][8];
} TrExplore;

typedef struct TrFinding {
    int line;
    int severity;
    char checker[24];
    char message[80];
} TrFinding;

int tr_explore(const char *text, TrExplore *out, int max);
int tr_check(const char *text, TrFinding *out, int max);
char *tr_backend_view(const char *text, int backend);
int tr_line_text(const char *text, int line, char *dst, size_t cap);
int tr_find_row(const TrFinding *f, const char *text, char *dst, size_t cap);

#endif
