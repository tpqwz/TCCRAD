#include "report.h"
#include "ide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TR_SEEN_MAX 32

static int
is_id(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_';
}

static int
is_kw(const char *s, int n)
{
    static const char *const w[] = {
        "if", "for", "while", "switch", "return", "sizeof", "case", "else", NULL
    };
    int i;
    for (i = 0; w[i]; i++) {
        int k = (int)strlen(w[i]);
        if (k == n && memcmp(s, w[i], (size_t)n) == 0) return 1;
    }
    return 0;
}

static int
line_proto(const char *row)
{
    size_t n = strlen(row);
    while (n > 0 && (row[n - 1] == ' ' || row[n - 1] == '\t' || row[n - 1] == '\r'))
        n--;
    return n > 0 && row[n - 1] == ';';
}

static int
find_name(const TrExplore *fn, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(fn[i].name, name) == 0) return i;
    }
    return -1;
}

static int
add_edge(int *xs, int *n, int id)
{
    int i;
    if (id < 0) return 0;
    for (i = 0; i < *n; i++) {
        if (xs[i] == id) return 0;
    }
    if (*n >= TR_CALL_MAX) return 1;
    xs[*n] = id;
    *n += 1;
    return 1;
}

static void
note_call(char calls[][TR_SEEN_MAX][48], int call_line[][TR_SEEN_MAX], int *ncalls,
    int fn, const char *name, int len, int line)
{
    int i;
    if (fn < 0 || len <= 0 || len >= 48 || ncalls[fn] >= TR_SEEN_MAX) return;
    for (i = 0; i < ncalls[fn]; i++) {
        if (strncmp(calls[fn][i], name, (size_t)len) == 0 && calls[fn][i][len] == '\0')
            return;
    }
    memcpy(calls[fn][ncalls[fn]], name, (size_t)len);
    calls[fn][ncalls[fn]][len] = '\0';
    call_line[fn][ncalls[fn]] = line;
    ncalls[fn]++;
}

int
tr_explore(const char *text, TrExplore *out, int max)
{
    static char calls[TR_EXPLORE_MAX][TR_SEEN_MAX][48];
    static int call_line[TR_EXPLORE_MAX][TR_SEEN_MAX];
    static int ncalls[TR_EXPLORE_MAX];
    int depth = 0;
    int in_block = 0;
    int cur = -1;
    int entered = 0;
    int line = 0;
    int n = 0;
    const char *p;
    int i;
    int c;
    if (!out || max < 1) return -1;
    if (!text) return 0;
    if (max > TR_EXPLORE_MAX) max = TR_EXPLORE_MAX;
    memset(out, 0, (size_t)max * sizeof *out);
    memset(ncalls, 0, sizeof ncalls);
    p = text;
    while (*p && line < 100000) {
        char row[512];
        size_t k = 0;
        int i2;
        int hash;
        int t;
        while (p[k] && p[k] != '\n' && k + 1 < sizeof row) {
            row[k] = p[k];
            k++;
        }
        row[k] = '\0';
        hash = 0;
        t = 0;
        while (row[t] == ' ' || row[t] == '\t') t++;
        if (row[t] == '#') hash = 1;
        if (!in_block && depth == 0 && !line_proto(row)) {
            if (n < max && ide_routine_name(row, out[n].name, sizeof out[n].name)) {
                out[n].line = line;
                out[n].lines = 1;
                out[n].complexity = 1;
                cur = n;
                entered = 0;
                n++;
            } else if (n >= max) {
                char scratch[48];
                if (ide_routine_name(row, scratch, sizeof scratch)) {
                    cur = -1;
                    entered = 0;
                }
            }
        }
        i2 = 0;
        while (row[i2] != '\0') {
            char ch = row[i2];
            if (in_block) {
                if (ch == '*' && row[i2 + 1] == '/') {
                    in_block = 0;
                    i2 += 2;
                } else {
                    i2++;
                }
                continue;
            }
            if (ch == '/' && row[i2 + 1] == '*') {
                if (cur >= 0 && entered &&
                    strncmp(row + i2, "/* @tccrad user ", 16) == 0)
                    out[cur].user = 1;
                in_block = 1;
                i2 += 2;
                continue;
            }
            if (ch == '/' && row[i2 + 1] == '/') break;
            if (ch == '"') {
                i2++;
                while (row[i2] != '\0' && row[i2] != '"') {
                    if (row[i2] == '\\' && row[i2 + 1] != '\0') i2++;
                    i2++;
                }
                if (row[i2] == '"') i2++;
                continue;
            }
            if (ch == '{') {
                depth++;
                if (cur >= 0) {
                    entered = 1;
                    if (depth > out[cur].depth && depth < 100000) out[cur].depth = depth;
                }
            } else if (ch == '}' && depth > 0) {
                depth--;
            } else if (!hash && cur >= 0 && entered && is_id(ch)) {
                int start = i2;
                int len;
                while (is_id(row[i2])) i2++;
                len = i2 - start;
                if (row[i2] == '(' && !is_kw(row + start, len))
                    note_call(calls, call_line, ncalls, cur, row + start, len, line);
                if ((len == 2 && memcmp(row + start, "if", 2) == 0) ||
                    (len == 3 && memcmp(row + start, "for", 3) == 0) ||
                    (len == 5 && memcmp(row + start, "while", 5) == 0) ||
                    (len == 4 && memcmp(row + start, "case", 4) == 0)) {
                    if (out[cur].complexity < 100000) out[cur].complexity++;
                    if (out[cur].ndecide_all < 100000) out[cur].ndecide_all++;
                    if (out[cur].ndecide < TR_DECIDE_MAX && len < 8) {
                        int rel = depth - 1;
                        if (rel < 0) rel = 0;
                        if (rel > 6) rel = 6;
                        memcpy(out[cur].decide[out[cur].ndecide], row + start, (size_t)len);
                        out[cur].decide[out[cur].ndecide][len] = '\0';
                        out[cur].decide_line[out[cur].ndecide] = line;
                        out[cur].decide_depth[out[cur].ndecide] = rel;
                        out[cur].ndecide++;
                    }
                }
                continue;
            } else if (!hash && cur >= 0 && entered && ch == '&' && row[i2 + 1] == '&') {
                if (out[cur].complexity < 100000) out[cur].complexity++;
                i2 += 2;
                continue;
            } else if (!hash && cur >= 0 && entered && ch == '|' && row[i2 + 1] == '|') {
                if (out[cur].complexity < 100000) out[cur].complexity++;
                i2 += 2;
                continue;
            } else if (!hash && cur >= 0 && entered && ch == '?') {
                if (out[cur].complexity < 100000) out[cur].complexity++;
            }
            i2++;
        }
        if (cur >= 0) {
            int span = line - out[cur].line + 1;
            if (span > 0 && span < 1000000) out[cur].lines = span;
        }
        if (cur >= 0 && entered && depth == 0) cur = -1;
        p += k;
        if (*p == '\n') p++;
        line++;
    }
    for (i = 0; i < n; i++) {
        for (c = 0; c < ncalls[i]; c++) {
            int dst = find_name(out, n, calls[i][c]);
            int before;
            if (dst < 0) continue;
            before = out[i].ncallees;
            if (add_edge(out[i].callees, &out[i].ncallees, dst) && out[i].fan_out < 100000)
                out[i].fan_out++;
            if (out[i].ncallees > before)
                out[i].callee_line[before] = call_line[i][c];
            before = out[dst].ncallers;
            if (add_edge(out[dst].callers, &out[dst].ncallers, i) && out[dst].fan_in < 100000)
                out[dst].fan_in++;
            if (out[dst].ncallers > before)
                out[dst].caller_line[before] = call_line[i][c];
        }
    }
    return n;
}

static int
call_at(const char *row, const char *fn)
{
    size_t n = strlen(fn);
    const char *s = row;
    while ((s = strstr(s, fn)) != NULL) {
        const char *after = s + n;
        int before_ok = s == row || !is_id(s[-1]);
        while (*after == ' ' || *after == '\t') after++;
        if (before_ok && *after == '(') return 1;
        s += n;
    }
    return 0;
}

static int
call_arg_literal(const char *row, const char *fn, int which)
{
    size_t n = strlen(fn);
    const char *s = row;
    while ((s = strstr(s, fn)) != NULL) {
        const char *p;
        int depth;
        int arg;
        int in_str;
        p = s + n;
        while (*p == ' ' || *p == '\t') p++;
        if ((s != row && is_id(s[-1])) || *p != '(') {
            s += n;
            continue;
        }
        p++;
        depth = 1;
        arg = 0;
        in_str = 0;
        while (*p != '\0' && depth > 0) {
            if (in_str) {
                if (*p == '\\' && p[1] != '\0') {
                    p += 2;
                    continue;
                }
                if (*p == '"') in_str = 0;
                p++;
                continue;
            }
            if (*p == '"') {
                if (arg == which && depth == 1) return 1;
                in_str = 1;
                p++;
                continue;
            }
            if (*p == '(') {
                depth++;
            } else if (*p == ')') {
                depth--;
                if (depth == 0) break;
            } else if (*p == ',' && depth == 1) {
                arg++;
            } else if (arg == which && depth == 1 && *p != ' ' && *p != '\t') {
                return 0;
            }
            p++;
        }
        if (depth > 0) return 1;
        return 0;
    }
    return 1;
}

static int
has_bare_s(const char *row)
{
    int i = 0;
    int in_str = 0;
    while (row[i] != '\0') {
        if (!in_str) {
            if (row[i] == '"') in_str = 1;
            i++;
            continue;
        }
        if (row[i] == '\\' && row[i + 1] != '\0') {
            i += 2;
            continue;
        }
        if (row[i] == '"') {
            in_str = 0;
            i++;
            continue;
        }
        if (row[i] == '%') {
            int j = i + 1;
            if (row[j] == '%') {
                i = j + 1;
                continue;
            }
            if (row[j] == '*') j++;
            if (row[j] >= '1' && row[j] <= '9') {
                while (row[j] >= '0' && row[j] <= '9') j++;
                i = j;
                continue;
            }
            if (row[j] == 's') return 1;
        }
        i++;
    }
    return 0;
}

static void
blank_comments(const char *row, char *dst, size_t cap, int *in_block)
{
    size_t i = 0;
    size_t o = 0;
    if (!dst || cap == 0) return;
    while (row[i] != '\0' && o + 1 < cap) {
        if (*in_block) {
            if (row[i] == '*' && row[i + 1] == '/') {
                *in_block = 0;
                dst[o++] = ' ';
                if (o + 1 < cap) dst[o++] = ' ';
                i += 2;
            } else {
                dst[o++] = ' ';
                i++;
            }
            continue;
        }
        if (row[i] == '/' && row[i + 1] == '*') {
            *in_block = 1;
            dst[o++] = ' ';
            if (o + 1 < cap) dst[o++] = ' ';
            i += 2;
            continue;
        }
        if (row[i] == '/' && row[i + 1] == '/') {
            while (o + 1 < cap) dst[o++] = ' ';
            break;
        }
        if (row[i] == '"') {
            dst[o++] = row[i++];
            while (row[i] != '\0' && row[i] != '"' && o + 1 < cap) {
                if (row[i] == '\\' && row[i + 1] != '\0' && o + 2 < cap) {
                    dst[o++] = row[i++];
                    dst[o++] = row[i++];
                    continue;
                }
                dst[o++] = row[i++];
            }
            if (row[i] == '"' && o + 1 < cap) dst[o++] = row[i++];
            continue;
        }
        dst[o++] = row[i++];
    }
    dst[o] = '\0';
}

static void
add_find(TrFinding *out, int *n, int max, int line, int sev, const char *checker,
    const char *msg)
{
    if (*n >= max) return;
    out[*n].line = line;
    out[*n].severity = sev;
    out[*n].checker[0] = '\0';
    out[*n].message[0] = '\0';
    if (checker) {
        size_t k = strlen(checker);
        if (k >= sizeof out[*n].checker) k = sizeof out[*n].checker - 1;
        memcpy(out[*n].checker, checker, k);
        out[*n].checker[k] = '\0';
    }
    if (msg) {
        size_t k = strlen(msg);
        if (k >= sizeof out[*n].message) k = sizeof out[*n].message - 1;
        memcpy(out[*n].message, msg, k);
        out[*n].message[k] = '\0';
    }
    *n += 1;
}

static int
comment_only(const char *row)
{
    const char *s;
    if (!row) return 0;
    s = row;
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '/' && (s[1] == '/' || s[1] == '*')) return 1;
    if (s[0] == '*') return 1;
    return 0;
}

static void
line_ignore(const char *row, int *all, char *which, size_t cap)
{
    const char *p;
    if (!row || !all || !which || cap < 2) return;
    p = row;
    while ((p = strstr(p, "@tccrad ignore")) != NULL) {
        size_t n = 0;
        p += strlen("@tccrad ignore");
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '*' || *p == '/' || *p == '\r') {
            *all = 1;
            return;
        }
        if (!is_id(*p)) return;
        while ((is_id(*p) || *p == '.') && n + 1 < cap) which[n++] = *p++;
        which[n] = '\0';
        return;
    }
}

static void
report_find(TrFinding *out, int *n, int max, int line, int sev, const char *checker,
    const char *msg, int all, const char *which)
{
    if (all) return;
    if (which && which[0] != '\0' && checker && strcmp(which, checker) == 0) return;
    add_find(out, n, max, line, sev, checker, msg);
}

int
tr_check(const char *text, TrFinding *out, int max)
{
    const char *p;
    char prev[512];
    int line = 0;
    int n = 0;
    int in_block = 0;
    if (!out || max < 1) return -1;
    if (!text) return 0;
    if (max > TR_FIND_MAX) max = TR_FIND_MAX;
    prev[0] = '\0';
    p = text;
    while (*p && line < 100000 && n < max) {
        char row[512];
        char code[512];
        char which[24];
        size_t k = 0;
        int all = 0;
        while (p[k] && p[k] != '\n' && k + 1 < sizeof row) {
            row[k] = p[k];
            k++;
        }
        row[k] = '\0';
        which[0] = '\0';
        if (comment_only(prev)) line_ignore(prev, &all, which, sizeof which);
        line_ignore(row, &all, which, sizeof which);
        blank_comments(row, code, sizeof code, &in_block);
        if (call_at(code, "gets"))
            report_find(out, &n, max, line, 3, "security.gets", "gets is unbounded", all, which);
        if (call_at(code, "strcpy"))
            report_find(out, &n, max, line, 2, "security.strcpy", "strcpy does not bound the copy",
                all, which);
        if (call_at(code, "strcat"))
            report_find(out, &n, max, line, 2, "security.strcat", "strcat does not bound the copy",
                all, which);
        if (call_at(code, "sprintf"))
            report_find(out, &n, max, line, 2, "security.sprintf", "sprintf does not bound the write",
                all, which);
        if (call_at(code, "strncpy"))
            report_find(out, &n, max, line, 1, "security.strncpy",
                "strncpy may omit the terminator", all, which);
        if (call_at(code, "system"))
            report_find(out, &n, max, line, 3, "security.system", "system starts a shell", all, which);
        if (call_at(code, "tmpnam"))
            report_find(out, &n, max, line, 3, "security.tmpnam", "tmpnam is unsafe", all, which);
        if (call_at(code, "mktemp"))
            report_find(out, &n, max, line, 3, "security.mktemp", "mktemp is unsafe", all, which);
        if (call_at(code, "alloca"))
            report_find(out, &n, max, line, 2, "security.alloca",
                "alloca grows the stack without a check", all, which);
        if ((call_at(code, "scanf") || call_at(code, "fscanf") || call_at(code, "sscanf")) &&
            has_bare_s(code))
            report_find(out, &n, max, line, 3, "security.scanf", "scanf conversion is unbounded",
                all, which);
        if (call_at(code, "printf") && !call_arg_literal(code, "printf", 0))
            report_find(out, &n, max, line, 3, "security.format", "format string is not a literal",
                all, which);
        if (call_at(code, "fprintf") && !call_arg_literal(code, "fprintf", 1))
            report_find(out, &n, max, line, 3, "security.format", "format string is not a literal",
                all, which);
        if (call_at(code, "snprintf") && !call_arg_literal(code, "snprintf", 2))
            report_find(out, &n, max, line, 3, "security.format", "format string is not a literal",
                all, which);
        memcpy(prev, row, k + 1);
        p += k;
        if (*p == '\n') p++;
        line++;
    }
    return n;
}

#define TR_PP_MAX 16

static const char *
skip_sp(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int
starts_dir(const char *row, const char *kw)
{
    size_t n = strlen(kw);
    const char *s = skip_sp(row);
    if (strncmp(s, kw, n) != 0) return 0;
    return s[n] == '\0' || s[n] == ' ' || s[n] == '\t' || s[n] == '\r';
}

static int
backend_arm(const char *row, int *id)
{
    static const char *const name[] = {
        "TCCRAD_BACKEND_NUKLEAR",
        "TCCRAD_BACKEND_LIBUI",
        "TCCRAD_BACKEND_LVGL"
    };
    const char *s = skip_sp(row);
    const char *p;
    int i;
    if (strncmp(s, "#if", 3) != 0 && strncmp(s, "#elif", 5) != 0) return 0;
    p = strstr(s, "TCCRAD_BACKEND");
    if (!p) return 0;
    p += strlen("TCCRAD_BACKEND");
    p = skip_sp(p);
    if (p[0] != '=' || p[1] != '=') return 0;
    p = skip_sp(p + 2);
    for (i = 0; i < 3; i++) {
        size_t nlen = strlen(name[i]);
        if (strncmp(p, name[i], nlen) != 0) continue;
        p = skip_sp(p + nlen);
        if (*p == '\0' || *p == '\r') {
            *id = i + 1;
            return 1;
        }
    }
    return 0;
}

char *
tr_backend_view(const char *text, int backend)
{
    struct Pp {
        int backend;
        int taken;
        int active;
    } stk[TR_PP_MAX];
    int sp = 0;
    size_t n;
    char *out;
    char *w;
    const char *p;
    if (!text) return NULL;
    if (backend < 1 || backend > 3) backend = 1;
    n = strlen(text);
    out = malloc(n + 1);
    if (!out) return NULL;
    w = out;
    p = text;
    while (*p) {
        char row[512];
        size_t k = 0;
        size_t i;
        int emit;
        int blank = 0;
        int id = 0;
        while (p[k] && p[k] != '\n' && k + 1 < sizeof row) {
            row[k] = p[k];
            k++;
        }
        row[k] = '\0';
        if (starts_dir(row, "#elif") && backend_arm(row, &id) && sp > 0 && stk[sp - 1].backend) {
            int parent = sp == 1 ? 1 : stk[sp - 2].active;
            int take = parent && !stk[sp - 1].taken && id == backend;
            if (take) stk[sp - 1].taken = 1;
            stk[sp - 1].active = take;
            blank = 1;
        } else if (starts_dir(row, "#else") && sp > 0 && stk[sp - 1].backend) {
            int parent = sp == 1 ? 1 : stk[sp - 2].active;
            int take = parent && !stk[sp - 1].taken;
            stk[sp - 1].taken = 1;
            stk[sp - 1].active = take;
            blank = 1;
        } else if (starts_dir(row, "#endif") && sp > 0) {
            int was = stk[sp - 1].backend;
            sp--;
            if (was) blank = 1;
        } else if (starts_dir(row, "#if") && backend_arm(row, &id) && sp < TR_PP_MAX) {
            int parent = sp == 0 ? 1 : stk[sp - 1].active;
            stk[sp].backend = 1;
            stk[sp].taken = parent && id == backend;
            stk[sp].active = parent && id == backend;
            sp++;
            blank = 1;
        } else if ((starts_dir(row, "#if") || starts_dir(row, "#ifdef") ||
                starts_dir(row, "#ifndef")) && sp < TR_PP_MAX) {
            int parent = sp == 0 ? 1 : stk[sp - 1].active;
            stk[sp].backend = 0;
            stk[sp].taken = 1;
            stk[sp].active = parent;
            sp++;
        }
        emit = sp == 0 || stk[sp - 1].active;
        if (blank || !emit) {
            for (i = 0; i < k; i++) w[i] = ' ';
        } else {
            memcpy(w, p, k);
        }
        w += k;
        if (p[k] == '\n') *w++ = '\n';
        p += k;
        if (*p == '\n') p++;
    }
    *w = '\0';
    return out;
}

int
tr_line_text(const char *text, int line, char *dst, size_t cap)
{
    const char *p;
    size_t i = 0;
    int n = 0;
    if (!dst || cap < 2) return -1;
    dst[0] = '\0';
    if (!text || line < 0) return 0;
    p = text;
    while (*p != '\0' && n < line) {
        if (*p == '\n') n++;
        p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    while (*p != '\0' && *p != '\n' && i + 1 < cap) dst[i++] = *p++;
    dst[i] = '\0';
    return 0;
}

int
tr_find_row(const TrFinding *f, const char *text, char *dst, size_t cap)
{
    char src[48];
    const char *sev = "low";
    if (!f || !dst || cap < 8) return -1;
    if (f->severity >= 3) sev = "high";
    else if (f->severity == 2) sev = "medium";
    src[0] = '\0';
    tr_line_text(text, f->line, src, sizeof src);
    if (src[0] != '\0')
        snprintf(dst, cap, "find  %s  %s  %d  %s", sev, f->checker, f->line + 1, src);
    else
        snprintf(dst, cap, "find  %s  %s  %d", sev, f->checker, f->line + 1);
    return 0;
}
