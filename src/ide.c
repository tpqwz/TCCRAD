#define _POSIX_C_SOURCE 200809L

#include "ide.h"
#include "codegen.h"
#include "project.h"
#include "rope.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char *
snap(const TrDoc *doc)
{
    TrBuf b;
    char *p;
    if (tr_buf_init(&b) != 0) return NULL;
    if (tr_doc_serialize(doc, &b) != 0) {
        tr_buf_free(&b);
        return NULL;
    }
    p = b.data;
    b.data = NULL;
    tr_buf_free(&b);
    return p;
}

static void
clear_redo(Ide *ide)
{
    int i;
    for (i = 0; i < ide->redo_n; i++) free(ide->redo_s[i]);
    ide->redo_n = 0;
}

static void
push_undo(Ide *ide, char *state)
{
    if (!state) return;
    if (ide->undo_n == IDE_UNDO) {
        free(ide->undo_s[0]);
        memmove(&ide->undo_s[0], &ide->undo_s[1], (IDE_UNDO - 1) * sizeof(char *));
        ide->undo_n--;
    }
    ide->undo_s[ide->undo_n++] = state;
}

static int
restore(Ide *ide, const char *text)
{
    char err[192];
    err[0] = '\0';
    if (!text || tr_doc_parse(ide->doc, text, strlen(text), err, sizeof err) != 0) {
        ide_set_status(ide, err[0] ? err : "undo state is invalid");
        return -1;
    }
    if (tr_doc_index(ide->doc, ide->selected) < 0) ide->selected = ide->doc->widgets[0].id;
    ide->body_id = 0;
    return 0;
}

void
ide_set_status(Ide *ide, const char *msg)
{
    if (!ide) return;
    tr_copy_cap(ide->status, sizeof ide->status, msg ? msg : "");
}

static int
ide_id_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_';
}

static int
ide_ascii_fold(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return (unsigned char)c;
}

int
ide_routine_name(const char *line, char *dst, size_t cap)
{
    const char *paren;
    const char *s;
    size_t n;
    if (!dst || cap < 2) return 0;
    dst[0] = '\0';
    if (!line || line[0] == '\0' || line[0] == ' ' || line[0] == '\t' ||
        line[0] == '#' || line[0] == '/' || line[0] == '*')
        return 0;
    paren = strchr(line, '(');
    if (!paren || paren == line) return 0;
    s = paren;
    while (s > line && ide_id_char(s[-1])) s--;
    if (s == paren) return 0;
    n = (size_t)(paren - s);
    if (n >= cap) return 0;
    if ((n == 2 && memcmp(s, "if", 2) == 0) ||
        (n == 3 && memcmp(s, "for", 3) == 0) ||
        (n == 5 && memcmp(s, "while", 5) == 0) ||
        (n == 6 && (memcmp(s, "switch", 6) == 0 || memcmp(s, "return", 6) == 0)) ||
        (n == 6 && memcmp(s, "sizeof", 6) == 0))
        return 0;
    memcpy(dst, s, n);
    dst[n] = '\0';
    return 1;
}

int
ide_routine_index(const int *lines, int n, int at)
{
    int i;
    int found = -1;
    if (!lines || n <= 0) return -1;
    for (i = 0; i < n; i++) {
        if (lines[i] <= at) found = i;
        else break;
    }
    return found;
}

int
ide_def_line(const char *text, const char *name)
{
    char needle[80];
    const char *p;
    int n;
    int ln;
    size_t nlen;
    if (!text || !name || name[0] == '\0') return -1;
    n = snprintf(needle, sizeof needle, "void %s(", name);
    if (n < 0 || (size_t)n >= sizeof needle) return -1;
    nlen = (size_t)n;
    p = text;
    ln = 0;
    while (*p && ln < 100000) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        size_t i;
        for (i = 0; i + nlen <= len; i++) {
            size_t k;
            if (memcmp(p + i, needle, nlen) != 0) continue;
            k = len;
            while (k > 0 && (p[k - 1] == ' ' || p[k - 1] == '\t' || p[k - 1] == '\r')) k--;
            if (k > 0 && p[k - 1] != ';') return ln;
        }
        if (!eol) break;
        p = eol + 1;
        ln++;
    }
    return -1;
}

int
ide_ident_at(const char *text, size_t n, size_t at, char *dst, size_t cap)
{
    size_t b;
    size_t e;
    size_t len;
    if (!dst || cap < 2) return 0;
    dst[0] = '\0';
    if (!text || n == 0 || at > n) return 0;
    if (at == n || !ide_id_char(text[at])) {
        if (at == 0 || !ide_id_char(text[at - 1])) return 0;
        at--;
    }
    b = at;
    while (b > 0 && ide_id_char(text[b - 1])) b--;
    e = at + 1;
    while (e < n && ide_id_char(text[e])) e++;
    len = e - b;
    if (len == 0 || len >= cap || len > 47) return 0;
    if (text[b] >= '0' && text[b] <= '9') return 0;
    memcpy(dst, text + b, len);
    dst[len] = '\0';
    return 1;
}

int
ide_ident_uses(const char *text, size_t n, const char *name, int *lines, int max)
{
    size_t nlen;
    size_t i = 0;
    int line = 0;
    int count = 0;
    if (!text || !name || !lines || max < 1) return 0;
    nlen = strlen(name);
    if (nlen == 0 || nlen > 64) return 0;
    while (i < n && count < max && line < 100000) {
        size_t eol = i;
        size_t k;
        while (eol < n && text[eol] != '\n') eol++;
        k = i;
        while (k + nlen <= eol) {
            int left = k == i || !ide_id_char(text[k - 1]);
            int right = k + nlen == eol || !ide_id_char(text[k + nlen]);
            if (left && right && memcmp(text + k, name, nlen) == 0) {
                lines[count++] = line;
                break;
            }
            k++;
        }
        if (eol >= n) break;
        i = eol + 1;
        line++;
    }
    return count;
}

static int
line_after_brace(const char *text, int sig)
{
    const char *p = text;
    int ln = 0;
    int left = 8;
    if (!text || sig < 0) return -1;
    while (*p && ln < sig) {
        if (*p == '\n') ln++;
        p++;
    }
    while (*p && left > 0) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        size_t k = len;
        while (k > 0 && (p[k - 1] == ' ' || p[k - 1] == '\t' || p[k - 1] == '\r')) k--;
        if (k > 0 && p[k - 1] == '{') return ln + 1;
        if (!eol) break;
        p = eol + 1;
        ln++;
        left--;
    }
    return sig;
}

int
ide_code_jump_line(const char *text, const char *handler, int id, const char *ev)
{
    char marker[96];
    int line;
    int n;
    int sig;
    if (!text) return -1;
    if (id > 0 && ev && ev[0] != '\0') {
        n = snprintf(marker, sizeof marker, "/* @tccrad user event %d %s */", id, ev);
        if (n > 0 && (size_t)n < sizeof marker) {
            line = ide_find_line(text, marker, 0);
            if (line >= 0) return line + 1;
        }
    }
    sig = ide_def_line(text, handler);
    if (sig >= 0) return line_after_brace(text, sig);
    if (id > 0) {
        n = snprintf(marker, sizeof marker, "/* @tccrad w %d */", id);
        if (n > 0 && (size_t)n < sizeof marker) {
            line = ide_find_line(text, marker, 0);
            if (line >= 0) return line;
        }
    }
    return -1;
}

static int
ensure_at(Ide *ide, int id, int ev)
{
    TrWidget *w;
    char name[TR_NAME_MAX];
    const char *body;
    const char *const *defs;
    int nd = 0;
    if (!ide || !ide->doc) return -1;
    w = tr_doc_widget(ide->doc, id);
    if (!w) return -1;
    if (ev < 0) ev = ide_primary_event(w);
    if (ev < 0) {
        const char *ename = "click";
        defs = tr_event_defs(w->kind, &nd);
        if (defs && nd > 0 && defs[0] && defs[0][0] != '\0') ename = defs[0];
        ev = tr_widget_add_event(w, ename);
        if (ev < 0) return -1;
        ide->dirty = 1;
    }
    ide->body_ev = ev;
    if (w->events[ev].handler[0] != '\0') {
        if (tr_copy_cap(name, sizeof name, w->events[ev].handler) != 0) return -1;
    } else {
        int nn = snprintf(name, sizeof name, "%s_%s_%s", ide->doc->class_name, w->name,
            w->events[ev].name);
        if (nn < 0 || (size_t)nn >= sizeof name) return -1;
        if (tr_copy_cap(w->events[ev].handler, sizeof w->events[ev].handler, name) != 0)
            return -1;
        ide->dirty = 1;
    }
    body = tr_span_ptr(ide->doc, w->events[ev].user);
    if (!body || body[0] == '\0') {
        if (tr_span_set(ide->doc, &w->events[ev].user, "    ", 4) == 0)
            ide->dirty = 1;
    }
    return tr_copy_cap(ide->jump, sizeof ide->jump, name);
}

static int
code_kw(const char *s, int n)
{
    static const char *const w[] = {
        "if", "else", "for", "while", "do", "return", "break", "continue",
        "int", "char", "void", "const", "static", "struct", "unsigned",
        "sizeof", "switch", "case", "default", "typedef", "enum", "goto",
        NULL
    };
    int i;
    for (i = 0; w[i]; i++) {
        int k = (int)strlen(w[i]);
        if (k == n && memcmp(s, w[i], (size_t)n) == 0) return 1;
    }
    return 0;
}

int
ide_code_token(const char *s, int at, int spaced, int *next)
{
    int i;
    if (!next) return 0;
    if (!s || at < 0 || s[at] == '\0') {
        *next = at < 0 ? 0 : at;
        return 0;
    }
    i = at;
    if (i == 0 && s[0] == '#') {
        while (s[i] != '\0') i++;
        *next = i;
        return 6;
    }
    if (s[i] == '/' && s[i + 1] == '/') {
        while (s[i] != '\0') i++;
        *next = i;
        return 1;
    }
    if (s[i] == '/' && s[i + 1] == '*') {
        i += 2;
        while (s[i] != '\0' && !(s[i] == '*' && s[i + 1] == '/')) i++;
        if (s[i] == '*') i += 2;
        *next = i;
        return 1;
    }
    if (s[i] == '"' || s[i] == '\'') {
        char q = s[i++];
        while (s[i] != '\0' && s[i] != q) {
            if (s[i] == '\\' && s[i + 1] != '\0') i++;
            i++;
        }
        if (s[i] == q) i++;
        *next = i;
        return 2;
    }
    if (spaced && s[i] == ':') {
        *next = i + 1;
        return 5;
    }
    if ((s[i] >= '0' && s[i] <= '9') ||
        (s[i] == '.' && s[i + 1] >= '0' && s[i + 1] <= '9')) {
        while (s[i] != '\0' && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
            s[i] == 'x' || s[i] == 'X' ||
            (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F')))
            i++;
        *next = i;
        return 3;
    }
    if ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || s[i] == '_') {
        int start = i;
        while ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
            (s[i] >= '0' && s[i] <= '9') || s[i] == '_')
            i++;
        *next = i;
        return code_kw(s + start, i - start) ? 4 : 0;
    }
    *next = i + 1;
    return 0;
}

static unsigned
text_sig(const char *s, size_t n)
{
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

int
ide_code_set_spaced(Ide *ide, int spaced, const char *text, size_t n)
{
    char err[160];
    char *out = NULL;
    size_t on = 0;
    if (!ide) return -1;
    spaced = spaced ? 1 : 0;
    if (spaced == ide->code_spaced) return 0;
    err[0] = '\0';
    if (ide->code_live && text) {
        int rc = spaced ? tr_spaced_space(text, n, &out, &on, err, sizeof err)
                        : tr_spaced_emit(text, n, &out, &on, err, sizeof err);
        if (rc != 0 || !out) {
            ide_set_status(ide, err[0] ? err : "spaced C did not convert");
            free(out);
            return -1;
        }
        tr_rope_free(ide->code_rope);
        ide->code_rope = tr_rope_new(out, on);
        free(out);
        if (!ide->code_rope) return -1;
        ide->code_at = 0;
        ide->code_caret = 0;
        ide->code_scroll = 0;
    } else {
        tr_rope_free(ide->code_rope);
        ide->code_rope = NULL;
        ide->code_live = 0;
    }
    ide->code_tied = 0;
    ide->code_tie = NULL;
    free(ide->code_lens);
    ide->code_lens = NULL;
    ide->code_lens_n = 0;
    ide->code_lens_sig = 0;
    ide->code_spaced = spaced;
    ide_set_status(ide, spaced ? "Spaced C" : "C");
    return 0;
}

int
ide_code_lens(Ide *ide, const char *host, size_t n, const char **shown)
{
    unsigned sig;
    char err[160];
    char *out = NULL;
    size_t on = 0;
    if (!shown) return -1;
    *shown = host ? host : "";
    if (!ide || !ide->code_spaced || ide->code_live) return 0;
    if (!host) host = "";
    sig = text_sig(host, n);
    if (ide->code_lens && ide->code_lens_sig == sig) {
        *shown = ide->code_lens;
        return 0;
    }
    err[0] = '\0';
    if (tr_spaced_space(host, n, &out, &on, err, sizeof err) != 0 || !out) {
        ide_set_status(ide, err[0] ? err : "spaced C did not convert");
        free(out);
        return -1;
    }
    free(ide->code_lens);
    ide->code_lens = out;
    ide->code_lens_n = on;
    ide->code_lens_sig = sig;
    *shown = ide->code_lens;
    return 0;
}

int
ide_user_writable(const char *text, size_t at, int del)
{
    const char *p;
    int any = 0;
    if (!text) return 0;
    p = text;
    while ((p = strstr(p, "/* @tccrad user ")) != NULL) {
        const char *nl = strchr(p, '\n');
        const char *stop = strstr(p, "/* @end user */");
        size_t begin;
        size_t end;
        any = 1;
        if (!nl || !stop || stop < nl) {
            p += 16;
            continue;
        }
        begin = (size_t)(nl + 1 - text);
        end = (size_t)(stop - text);
        if (del) {
            if (at >= begin && at < end) return 1;
        } else if (at >= begin && at <= end) {
            return 1;
        }
        p = stop + 14;
    }
    return any ? 0 : 1;
}

int
ide_span_writable(const char *text, size_t lo, size_t hi)
{
    const char *p;
    int any = 0;
    if (!text || hi < lo) return 0;
    p = text;
    while ((p = strstr(p, "/* @tccrad user ")) != NULL) {
        const char *nl = strchr(p, '\n');
        const char *stop = strstr(p, "/* @end user */");
        size_t begin;
        size_t end;
        any = 1;
        if (!nl || !stop || stop < nl) {
            p += 16;
            continue;
        }
        begin = (size_t)(nl + 1 - text);
        end = (size_t)(stop - text);
        if (lo >= begin && hi <= end) return 1;
        p = stop + 14;
    }
    return any ? 0 : 1;
}

int
ide_find_line(const char *text, const char *needle, int start)
{
    const char *p;
    size_t nlen;
    int line = 0;
    if (!text || !needle || needle[0] == '\0' || start < 0) return -1;
    nlen = strlen(needle);
    if (nlen > 200) return -1;
    p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (line >= start && len >= nlen) {
            size_t i;
            for (i = 0; i + nlen <= len; i++) {
                if (memcmp(p + i, needle, nlen) == 0) return line;
            }
        }
        if (!eol) break;
        p = eol + 1;
        line++;
        if (line > 100000) break;
    }
    return -1;
}

int
ide_text_has(const char *hay, const char *needle)
{
    size_t nlen;
    size_t i;
    size_t hlen;
    if (!needle || needle[0] == '\0') return 1;
    if (!hay) return 0;
    nlen = strlen(needle);
    hlen = strlen(hay);
    if (nlen > hlen) return 0;
    for (i = 0; i + nlen <= hlen; i++) {
        size_t k;
        for (k = 0; k < nlen; k++) {
            if (ide_ascii_fold(hay[i + k]) != ide_ascii_fold(needle[k])) break;
        }
        if (k == nlen) return 1;
    }
    return 0;
}

int
ide_ui_clamp(int pct)
{
    if (pct < 100) pct = 100;
    if (pct > 300) pct = 300;
    pct = ((pct + 12) / 25) * 25;
    if (pct < 100) pct = 100;
    if (pct > 300) pct = 300;
    return pct;
}

int
ide_ui_default_pct(int screen_h, int dpi)
{
    int from_h = 100;
    int from_dpi = 100;
    int pct;
    if (screen_h > 2520) screen_h = 2520;
    if (screen_h > 1080)
        from_h = 100 + (screen_h - 1080) * 50 / 360;
    if (dpi > 400) dpi = 400;
    if (dpi > 96)
        from_dpi = dpi * 100 / 96;
    pct = from_h > from_dpi ? from_h : from_dpi;
    return ide_ui_clamp(pct);
}

static int
ui_config_dir(char *dst, size_t cap, int make)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char base[TR_PATH_MAX];
    if (xdg && xdg[0] != '\0') {
        if (tr_copy_cap(base, sizeof base, xdg) != 0) return -1;
    } else if (home && home[0] != '\0') {
        if (tr_path_join(base, sizeof base, home, ".config") != 0) return -1;
    } else {
        return -1;
    }
    if (tr_path_join(dst, cap, base, "tccrad") != 0) return -1;
    if (make && mkdir(dst, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int
ui_scale_read(void)
{
    char dir[TR_PATH_MAX];
    char path[TR_PATH_MAX];
    char buf[16];
    ssize_t n;
    int fd;
    int v = 0;
    int any = 0;
    int i;
    if (ui_config_dir(dir, sizeof dir, 0) != 0) return -1;
    if (tr_path_join(path, sizeof path, dir, "ui_scale") != 0) return -1;
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    n = read(fd, buf, sizeof buf - 1);
    if (close(fd) != 0) return -1;
    if (n <= 0) return -1;
    buf[n] = '\0';
    if (buf[n - 1] == '\n') buf[n - 1] = '\0';
    for (i = 0; buf[i] != '\0'; i++) {
        if (buf[i] < '0' || buf[i] > '9') return -1;
        any = 1;
        v = v * 10 + (buf[i] - '0');
        if (v > 300) return -1;
    }
    if (!any || v < 100) return -1;
    return v;
}

static void
ui_scale_save(int pct)
{
    char dir[TR_PATH_MAX];
    char path[TR_PATH_MAX];
    char text[16];
    int n;
    if (ui_config_dir(dir, sizeof dir, 1) != 0) return;
    if (tr_path_join(path, sizeof path, dir, "ui_scale") != 0) return;
    n = snprintf(text, sizeof text, "%d\n", pct);
    if (n < 0 || (size_t)n >= sizeof text) return;
    (void)tr_write_file(path, text, (size_t)n);
}

int
ide_scheme_from_text(const char *text)
{
    if (!text) return -1;
    if (strcmp(text, "dark") == 0) return IDE_SCHEME_DARK;
    if (strcmp(text, "light") == 0) return IDE_SCHEME_LIGHT;
    return -1;
}

void
ide_scheme_bg(int scheme, unsigned char *r, unsigned char *g, unsigned char *b)
{
    unsigned char rr = 18;
    unsigned char gg = 18;
    unsigned char bb = 18;
    if (scheme == IDE_SCHEME_LIGHT) {
        rr = 245;
        gg = 245;
        bb = 245;
    }
    if (r) *r = rr;
    if (g) *g = gg;
    if (b) *b = bb;
}

static int
ui_scheme_read(void)
{
    char dir[TR_PATH_MAX];
    char path[TR_PATH_MAX];
    char buf[16];
    ssize_t n;
    int fd;
    if (ui_config_dir(dir, sizeof dir, 0) != 0) return -1;
    if (tr_path_join(path, sizeof path, dir, "ui_scheme") != 0) return -1;
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    n = read(fd, buf, sizeof buf - 1);
    if (close(fd) != 0) return -1;
    if (n <= 0) return -1;
    buf[n] = '\0';
    if (buf[n - 1] == '\n') buf[n - 1] = '\0';
    return ide_scheme_from_text(buf);
}

static void
ui_scheme_save(int scheme)
{
    char dir[TR_PATH_MAX];
    char path[TR_PATH_MAX];
    const char *word = scheme == IDE_SCHEME_LIGHT ? "light\n" : "dark\n";
    size_t n = scheme == IDE_SCHEME_LIGHT ? 6 : 5;
    if (ui_config_dir(dir, sizeof dir, 1) != 0) return;
    if (tr_path_join(path, sizeof path, dir, "ui_scheme") != 0) return;
    (void)tr_write_file(path, word, n);
}

int
ide_scope_show(const char *line, int *depth)
{
    int d = depth ? *depth : 0;
    int show;
    int net = 0;
    int in_str = 0;
    int i;
    if (!line) return 0;
    for (i = 0; line[i] != '\0'; i++) {
        char c = line[i];
        if (in_str) {
            if (c == '\\' && line[i + 1] != '\0') {
                i++;
                continue;
            }
            if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            continue;
        }
        if (c == '/' && line[i + 1] == '/') break;
        if (c == '{') net++;
        else if (c == '}') net--;
    }
    show = d;
    if (net < 0) show = d + net;
    if (show < 0) show = 0;
    if (show > 12) show = 12;
    d += net;
    if (d < 0) d = 0;
    if (depth) *depth = d;
    return show;
}

int
ide_scope_gutter_n(const char *line, size_t n, int *depth, char *dst, size_t cap)
{
    int d = depth ? *depth : 0;
    unsigned char kind[12];
    int i;
    int in_str = 0;
    int used = d;
    size_t at = 0;
    size_t o = 0;
    if (dst && cap) dst[0] = '\0';
    if (!line || !dst || cap < 4) return -1;
    for (i = 0; i < 12; i++) kind[i] = 0;
    for (i = 0; i < d && i < 12; i++) kind[i] = 2;
    while (at < n) {
        char c = line[at];
        if (in_str) {
            if (c == '\\' && at + 1 < n) {
                at += 2;
                continue;
            }
            if (c == '"') in_str = 0;
            at++;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            at++;
            continue;
        }
        if (c == '/' && at + 1 < n && line[at + 1] == '/') break;
        if (c == '{') {
            if (d < 12) kind[d] = 1;
            d++;
            if (d > used) used = d;
        } else if (c == '}' && d > 0) {
            d--;
            if (d < 12) kind[d] = 3;
            if (d + 1 > used) used = d + 1;
        }
        at++;
    }
    if (d < 0) d = 0;
    if (used > 12) used = 12;
    for (i = 0; i < used; i++) {
        const char *g = " ";
        if (kind[i] == 1) g = "\xE2\x94\x8C";
        else if (kind[i] == 2) g = "\xE2\x94\x82";
        else if (kind[i] == 3) g = "\xE2\x94\x94";
        if (o + 3 >= cap) break;
        dst[o++] = g[0];
        if ((unsigned char)g[0] > 127) {
            dst[o++] = g[1];
            dst[o++] = g[2];
        }
    }
    dst[o] = '\0';
    if (depth) *depth = d;
    return 0;
}

int
ide_scope_gutter(const char *line, int *depth, char *dst, size_t cap)
{
    size_t n = 0;
    if (line) n = strlen(line);
    return ide_scope_gutter_n(line, n, depth, dst, cap);
}

static int
line_is_def(const char *line, const char *name)
{
    char got[64];
    if (ide_routine_name(line, got, sizeof got) != 1) return 0;
    return strcmp(got, name) == 0;
}

static int
scan_def_file(const char *path, const char *name, int *lineno)
{
    char buf[4096];
    char line[512];
    int fd;
    int ln = 0;
    size_t have = 0;
    ssize_t n;
    size_t total = 0;
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    while ((n = read(fd, buf, sizeof buf)) > 0 && total < 1048576) {
        size_t i;
        total += (size_t)n;
        for (i = 0; i < (size_t)n; i++) {
            if (buf[i] == '\n' || have + 1 >= sizeof line) {
                line[have] = '\0';
                if (line_is_def(line, name)) {
                    if (lineno) *lineno = ln;
                    close(fd);
                    return 1;
                }
                have = 0;
                ln++;
            } else if (have + 1 < sizeof line) {
                line[have++] = buf[i];
            }
        }
    }
    close(fd);
    return 0;
}

int
ide_find_def(const char *home, const char *docpath, const char *name,
    char *path, size_t pathcap, int *lineno)
{
    char dir[TR_PATH_MAX];
    char cand[TR_PATH_MAX];
    const char *dirs[] = { "runtime", "src", "src", "src" };
    const char *files[] = { "tccrad.h", "codegen.h", "model.h", "ide.h" };
    int t;
    if (!name || !name[0] || !path || pathcap < 2) return 0;
    path[0] = '\0';
    if (docpath && docpath[0]) {
        size_t n = strlen(docpath);
        while (n > 0 && docpath[n - 1] != '/') n--;
        if (n > 0 && n < sizeof dir) {
            memcpy(dir, docpath, n - 1);
            dir[n - 1] = '\0';
        } else {
            dir[0] = '\0';
        }
    } else {
        dir[0] = '\0';
    }
    if (home && home[0]) {
        for (t = 0; t < 4; t++) {
            char mid[TR_PATH_MAX];
            if (tr_path_join(mid, sizeof mid, home, dirs[t]) != 0) continue;
            if (tr_path_join(cand, sizeof cand, mid, files[t]) != 0) continue;
            if (scan_def_file(cand, name, lineno)) {
                return tr_copy_cap(path, pathcap, cand) == 0;
            }
        }
    }
    if (dir[0] && tr_path_join(cand, sizeof cand, dir, name) == 0) {
        size_t L = strlen(cand);
        if (L + 2 < sizeof cand) {
            memcpy(cand + L, ".h", 3);
            if (scan_def_file(cand, name, lineno))
                return tr_copy_cap(path, pathcap, cand) == 0;
        }
    }
    return 0;
}

int
ide_primary_event(const TrWidget *w)
{
    static const char *const order[] = {
        "select", "click", "change", "toggle", "draw", "appear", "close", NULL
    };
    int i;
    int e;
    if (!w) return -1;
    for (i = 0; order[i]; i++) {
        for (e = 0; e < w->nevents; e++) {
            if (strcmp(w->events[e].name, order[i]) == 0) return e;
        }
    }
    return -1;
}

int
ide_handler_name(const TrDoc *doc, const TrWidget *w, char *dst, size_t cap)
{
    int ev;
    int n;
    if (!dst || cap < 2) return -1;
    dst[0] = '\0';
    ev = ide_primary_event(w);
    if (!doc || !w || ev < 0) return -1;
    if (w->events[ev].handler[0] != '\0')
        return tr_copy_cap(dst, cap, w->events[ev].handler);
    n = snprintf(dst, cap, "%s_%s_%s", doc->class_name, w->name, w->events[ev].name);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

int
ide_ensure_handler(Ide *ide, int id)
{
    return ensure_at(ide, id, -1);
}

static int
open_event_at(Ide *ide, int id, int ev)
{
    char msg[96];
    int n;
    if (!ide) return -1;
    ide_select(ide, id);
    if (ide->code_ext[0] != '\0') {
        ide->code_ext[0] = '\0';
        tr_rope_free(ide->code_rope);
        ide->code_rope = NULL;
        ide->code_live = 0;
    } else if (ide->code_live)
        ide_apply_code(ide);
    if (ensure_at(ide, id, ev) != 0) {
        ide_set_status(ide, "that control has no event");
        return -1;
    }
    ide->preview_fresh = 0;
    ide->code_tied = 0;
    ide->code_tie = NULL;
    ide->body_id = id;
    tr_rope_free(ide->code_rope);
    ide->code_rope = NULL;
    ide->code_live = 0;
    ide->code_hold = 1;
    ide->view = 1;
    ide->code_focus = 1;
    if (ide->modal == IDE_MODAL_CODE || ide->modal == IDE_MODAL_EXPLORE)
        ide->modal = IDE_MODAL_NONE;
    n = snprintf(msg, sizeof msg, "Code %s", ide->jump[0] ? ide->jump : "handler");
    if (n > 0 && (size_t)n < sizeof msg) ide_set_status(ide, msg);
    return 0;
}

void
ide_open_code(Ide *ide, int id)
{
    open_event_at(ide, id, -1);
}

int
ide_open_named_event(Ide *ide, int id, const char *evname)
{
    TrWidget *w;
    int ev;
    if (!ide || !ide->doc || !evname || evname[0] == '\0') return -1;
    w = tr_doc_widget(ide->doc, id);
    if (!w) return -1;
    ev = tr_widget_add_event(w, evname);
    if (ev < 0) {
        ide_set_status(ide, "that component cannot take another event");
        return -1;
    }
    return open_event_at(ide, id, ev);
}

void
ide_ui_load(Ide *ide, int screen_h, int dpi)
{
    int saved;
    int scheme;
    if (!ide) return;
    saved = ui_scale_read();
    if (saved > 0) ide->ui_pct = ide_ui_clamp(saved);
    else ide->ui_pct = ide_ui_default_pct(screen_h, dpi);
    scheme = ui_scheme_read();
    if (scheme == IDE_SCHEME_DARK || scheme == IDE_SCHEME_LIGHT)
        ide->scheme = scheme;
}

void
ide_scheme_set(Ide *ide, int scheme)
{
    if (!ide) return;
    if (scheme != IDE_SCHEME_LIGHT) scheme = IDE_SCHEME_DARK;
    if (scheme == ide->scheme) return;
    ide->scheme = scheme;
    ui_scheme_save(scheme);
    ide_set_status(ide, scheme == IDE_SCHEME_LIGHT ? "Light" : "Dark");
}

void
ide_ui_set(Ide *ide, int pct)
{
    char msg[32];
    int next;
    int n;
    if (!ide) return;
    next = ide_ui_clamp(pct);
    if (next == ide->ui_pct) return;
    ide->ui_pct = next;
    ui_scale_save(next);
    n = snprintf(msg, sizeof msg, "Display %d%%", next);
    if (n > 0 && (size_t)n < sizeof msg) ide_set_status(ide, msg);
}

void
ide_ui_bump(Ide *ide, int delta)
{
    if (!ide) return;
    ide_ui_set(ide, ide->ui_pct + delta);
}

Ide *
ide_new(void)
{
    Ide *ide = (Ide *)calloc(1, sizeof *ide);
    char style_dir[TR_PATH_MAX];
    if (!ide) return NULL;
    ide->doc = tr_doc_new();
    if (!ide->doc || tr_buf_init(&ide->preview) != 0) {
        tr_doc_free(ide->doc);
        free(ide);
        return NULL;
    }
    tr_doc_blank(ide->doc);
    tr_style_defaults(&ide->style, "default");
    if (tr_home_dir(ide->home, sizeof ide->home) != 0)
        tr_copy_cap(ide->home, sizeof ide->home, ".");
    if (tr_path_join(style_dir, sizeof style_dir, ide->home, "style") == 0) {
        char err[64];
        err[0] = '\0';
        (void)mkdir(style_dir, 0700);
        if (tr_style_load(&ide->style, style_dir, "default", err, sizeof err) != 0) {
            tr_style_defaults(&ide->style, "default");
            tr_style_save(&ide->style, style_dir, err, sizeof err);
        }
    }
    tr_copy_cap(ide->browse, sizeof ide->browse, ide->home);
    ide->selected = 1;
    ide->ui_pct = 100;
    ide->committed = snap(ide->doc);
    return ide;
}

static void
drop_live_code(Ide *ide)
{
    ide->code_live = 0;
    ide->code_applied = 0;
    tr_rope_free(ide->code_rope);
    ide->code_rope = NULL;
    free(ide->code_lens);
    ide->code_lens = NULL;
    ide->code_lens_n = 0;
    ide->code_lens_sig = 0;
}

void
ide_apply_code(Ide *ide)
{
    char *text;
    size_t n = 0;
    char err[160];
    char *host = NULL;
    char *use;
    size_t use_n;
    if (!ide || !ide->code_live || !ide->code_rope || ide->code_applied) return;
    text = tr_rope_flat(ide->code_rope, &n);
    if (!text) return;
    err[0] = '\0';
    use = text;
    use_n = n;
    if (ide->code_spaced) {
        if (tr_spaced_emit(text, n, &host, &use_n, err, sizeof err) != 0 || !host) {
            ide_set_status(ide, err[0] ? err : "spaced C did not convert");
            ide->code_applied = 1;
            free(host);
            free(text);
            return;
        }
        use = host;
    }
    if (ide->code_ext[0] != '\0') {
        if (tr_write_file(ide->code_ext, use, use_n) != 0) {
            ide_set_status(ide, "could not write that file");
            ide->code_applied = 1;
        } else {
            ide_set_status(ide, "wrote the file");
            drop_live_code(ide);
        }
    } else if (strstr(use, "@tccrad-doc") != NULL) {
        if (tr_doc_load_text(ide->doc, use, use_n, err, sizeof err) != 0) {
            ide_set_status(ide, err[0] ? err : "code did not apply");
            ide->code_applied = 1;
        } else {
            ide->dirty = 1;
            ide_set_status(ide, "code applied");
            ide->preview_fresh = 0;
            ide->code_tied = 0;
            ide->code_tie = NULL;
            drop_live_code(ide);
        }
    } else {
        ide->code_applied = 1;
    }
    free(host);
    free(text);
}

void
ide_free(Ide *ide)
{
    int i;
    if (!ide) return;
    for (i = 0; i < ide->undo_n; i++) free(ide->undo_s[i]);
    for (i = 0; i < ide->redo_n; i++) free(ide->redo_s[i]);
    free(ide->committed);
    tr_buf_free(&ide->preview);
    free(ide->code_lens);
    tr_rope_free(ide->code_rope);
    tr_doc_free(ide->doc);
    free(ide);
}

void
ide_select(Ide *ide, int id)
{
    if (!ide || !ide->doc) return;
    if (tr_doc_index(ide->doc, id) < 0) return;
    if (ide->selected != id) {
        ide->selected = id;
        ide->body_id = 0;
        ide->gesture = 0;
    }
}

static int
parent_for(const Ide *ide, TrKind kind)
{
    const TrWidget *w;
    int guard = 0;
    int id;
    if (!ide->doc || ide->doc->nwidgets < 1) return -1;
    id = ide->selected > 0 ? ide->selected : ide->doc->widgets[0].id;
    while (guard++ < TR_WIDGETS_MAX) {
        w = tr_doc_widget_c(ide->doc, id);
        if (!w) break;
        if (id != ide->doc->widgets[0].id && tr_parent_ok(w->kind, kind)) return w->id;
        if (w->parent < 0) break;
        id = w->parent;
    }
    w = &ide->doc->widgets[0];
    if (tr_parent_ok(w->kind, kind)) return w->id;
    return -1;
}

void
ide_add(Ide *ide, TrKind kind)
{
    int parent;
    int id;
    if (!ide) return;
    parent = parent_for(ide, kind);
    if (parent < 0) {
        ide_set_status(ide, "that widget cannot be placed in the selection");
        return;
    }
    id = tr_doc_add_widget(ide->doc, parent, kind, NULL);
    if (id < 0) {
        ide_set_status(ide, "could not add the widget");
        return;
    }
    ide_select(ide, id);
    ide->dirty = 1;
    ide_set_status(ide, "widget added");
}

void
ide_delete(Ide *ide)
{
    if (!ide || ide->selected <= 0) return;
    if (tr_doc_delete(ide->doc, ide->selected) != 0) {
        ide_set_status(ide, "the root window stays");
        return;
    }
    ide->selected = ide->doc->widgets[0].id;
    ide->body_id = 0;
    ide->dirty = 1;
}

void
ide_move(Ide *ide, int dir)
{
    if (!ide) return;
    if (tr_doc_move(ide->doc, ide->selected, dir) != 0)
        ide_set_status(ide, "cannot move that widget further");
    else ide->dirty = 1;
}

void
ide_duplicate(Ide *ide)
{
    int id;
    if (!ide) return;
    id = tr_doc_duplicate(ide->doc, ide->selected);
    if (id < 0) {
        ide_set_status(ide, "cannot duplicate that widget");
        return;
    }
    ide_select(ide, id);
    ide->dirty = 1;
}

void
ide_undo(Ide *ide)
{
    char *cur;
    if (!ide || ide->undo_n <= 0) return;
    cur = snap(ide->doc);
    if (cur && ide->redo_n < IDE_UNDO) ide->redo_s[ide->redo_n++] = cur;
    else free(cur);
    cur = ide->undo_s[--ide->undo_n];
    restore(ide, cur);
    free(ide->committed);
    ide->committed = snap(ide->doc);
    free(cur);
    ide->gesture = 0;
    ide->dirty = 1;
    ide_set_status(ide, "undo");
}

void
ide_redo(Ide *ide)
{
    char *cur;
    if (!ide || ide->redo_n <= 0) return;
    cur = snap(ide->doc);
    if (cur) push_undo(ide, cur);
    cur = ide->redo_s[--ide->redo_n];
    restore(ide, cur);
    free(ide->committed);
    ide->committed = snap(ide->doc);
    free(cur);
    ide->gesture = 0;
    ide->dirty = 1;
    ide_set_status(ide, "redo");
}

void
ide_after_frame(Ide *ide, int active)
{
    char *now;
    if (!ide || !ide->doc) return;
    now = snap(ide->doc);
    if (!now) return;
    if (!ide->committed) {
        ide->committed = now;
        return;
    }
    if (strcmp(now, ide->committed) != 0) {
        ide->preview_fresh = 0;
        ide->code_tied = 0;
        ide->code_tie = NULL;
        if (!ide->gesture) {
            push_undo(ide, ide->committed);
            clear_redo(ide);
            ide->committed = NULL;
            ide->gesture = 1;
        } else {
            free(ide->committed);
        }
        ide->committed = now;
        ide->dirty = 1;
    } else {
        free(now);
        if (!active) ide->gesture = 0;
    }
}

static void
header_path(const char *cpath, char *dst, size_t cap)
{
    size_t n;
    if (tr_copy_cap(dst, cap, cpath) != 0) {
        if (cap) dst[0] = '\0';
        return;
    }
    n = strlen(dst);
    if (n >= 2 && dst[n - 2] == '.' && dst[n - 1] == 'c') dst[n - 1] = 'h';
    else if (n + 2 < cap) {
        dst[n] = '.';
        dst[n + 1] = 'h';
        dst[n + 2] = '\0';
    }
}

static void
remember_mtime(Ide *ide)
{
    struct stat st;
    ide->mtime = 0;
    if (ide->path[0] && stat(ide->path, &st) == 0) ide->mtime = st.st_mtime;
}

int
ide_save(Ide *ide)
{
    TrBuf h, c;
    char hpath[TR_PATH_MAX];
    char err[192];
    if (!ide || ide->path[0] == '\0') {
        ide->modal = IDE_MODAL_SAVE;
        return -1;
    }
    if (tr_buf_init(&h) != 0 || tr_buf_init(&c) != 0) return -1;
    err[0] = '\0';
    header_path(ide->path, hpath, sizeof hpath);
    {
        char base[64];
        if (tr_path_basename(hpath, base, sizeof base) != 0) {
            ide_set_status(ide, "header name is not usable");
            tr_buf_free(&h);
            tr_buf_free(&c);
            return -1;
        }
        tr_buf_clear(&h);
        tr_buf_clear(&c);
        if (tr_codegen(ide->doc, &ide->style, base, &h, &c, err, sizeof err) != 0) {
            ide_set_status(ide, err[0] ? err : "save failed");
            tr_buf_free(&h);
            tr_buf_free(&c);
            return -1;
        }
    }
    if (tr_write_file(ide->path, c.data, c.len) != 0 || tr_write_file(hpath, h.data, h.len) != 0) {
        ide_set_status(ide, "could not write the file");
        tr_buf_free(&h);
        tr_buf_free(&c);
        return -1;
    }
    tr_buf_free(&h);
    tr_buf_free(&c);
    ide->dirty = 0;
    remember_mtime(ide);
    ide_set_status(ide, "saved");
    return 0;
}

int
ide_open_path(Ide *ide, const char *path)
{
    char *text = NULL;
    size_t n = 0;
    char err[192];
    char hpath[TR_PATH_MAX];
    if (!ide || !path) return -1;
    if (tr_read_file(path, &text, &n, TR_FILE_MAX) != 0) {
        ide_set_status(ide, "could not read that file");
        return -1;
    }
    err[0] = '\0';
    if (tr_doc_load_text(ide->doc, text, n, err, sizeof err) != 0) {
        ide_set_status(ide, err[0] ? err : "file has no tccrad document");
        free(text);
        return -1;
    }
    free(text);
    header_path(path, hpath, sizeof hpath);
    if (tr_read_file(hpath, &text, &n, TR_FILE_MAX) == 0) {
        tr_doc_overlay_text(ide->doc, text, err, sizeof err);
        free(text);
    }
    if (tr_copy_cap(ide->path, sizeof ide->path, path) != 0) return -1;
    tr_rope_free(ide->code_rope);
    ide->code_rope = NULL;
    ide->code_live = 0;
    ide->code_ext[0] = '\0';
    ide->code_at = 0;
    ide->code_caret = 0;
    ide->code_scroll = 0;
    ide->nfindings = 0;
    ide->checked = 0;
    ide->explore_sel = 0;
    ide->selected = ide->doc->nwidgets ? ide->doc->widgets[0].id : 0;
    ide->body_id = 0;
    ide->dirty = 0;
    remember_mtime(ide);
    {
        char style_dir[TR_PATH_MAX];
        if (tr_path_join(style_dir, sizeof style_dir, ide->home, "style") == 0)
            tr_style_load(&ide->style, style_dir, ide->doc->style_name, err, sizeof err);
    }
    ide_set_status(ide, "opened");
    return 0;
}

void
ide_blank(Ide *ide)
{
    if (!ide) return;
    tr_doc_blank(ide->doc);
    tr_rope_free(ide->code_rope);
    ide->code_rope = NULL;
    ide->code_live = 0;
    ide->code_ext[0] = '\0';
    ide->code_at = 0;
    ide->path[0] = '\0';
    ide->nfindings = 0;
    ide->checked = 0;
    ide->explore_sel = 0;
    ide->selected = 1;
    ide->dirty = 0;
    ide->body_id = 0;
    ide->mtime = 0;
    ide_set_status(ide, "new document");
}

void
ide_set_backend(Ide *ide, TrBackend backend)
{
    if (!ide || !ide->doc) return;
    ide->doc->backend = backend;
    ide->dirty = 1;
}

static int
out_dir_for(const Ide *ide, char *dst, size_t cap)
{
    char parent[TR_PATH_MAX];
    if (ide->path[0] == '\0') {
        int n = snprintf(dst, cap, "/tmp/tccrad-%ld", (long)getpid());
        return (n < 0 || (size_t)n >= cap) ? -1 : 0;
    }
    if (tr_path_dirname(ide->path, parent, sizeof parent) != 0) return -1;
    return tr_path_join(dst, cap, parent, "out");
}

void
ide_build(Ide *ide, int run)
{
    char out[TR_PATH_MAX];
    char runtime[TR_PATH_MAX];
    char third[TR_PATH_MAX];
    if (!ide) return;
    if (out_dir_for(ide, out, sizeof out) != 0) {
        ide_set_status(ide, "build directory path is not usable");
        return;
    }
    if (tr_path_join(runtime, sizeof runtime, ide->home, "runtime") != 0) return;
    if (tr_path_join(third, sizeof third, ide->home, "third_party") != 0) return;
    ide->log[0] = '\0';
    if (ide->child > 1) ide_stop(ide);
    if (tr_project_build(ide->doc, &ide->style, out, runtime, third, "tcc",
            ide->log, sizeof ide->log, run, &ide->child) != 0) {
        ide_set_status(ide, "build failed");
        return;
    }
    ide_set_status(ide, run ? "built and started" : "built");
}

void
ide_stop(Ide *ide)
{
    int status = 0;
    if (!ide || ide->child <= 1) return;
    if (waitpid(ide->child, &status, WNOHANG) == 0) kill(ide->child, SIGTERM);
    waitpid(ide->child, &status, WNOHANG);
    ide->child = 0;
    ide_set_status(ide, "stopped");
}

void
ide_poll_child(Ide *ide)
{
    int status = 0;
    if (!ide || ide->child <= 1) return;
    if (waitpid(ide->child, &status, WNOHANG) == ide->child) {
        ide->child = 0;
        ide_set_status(ide, "program exited");
    }
}

void
ide_note_disk(Ide *ide)
{
    struct stat st;
    if (!ide || ide->path[0] == '\0' || ide->mtime == 0) return;
    if (stat(ide->path, &st) != 0) return;
    if (st.st_mtime == ide->mtime) return;
    if (!ide->dirty) {
        if (ide_open_path(ide, ide->path) == 0)
            ide_set_status(ide, "reloaded changes from disk");
    } else {
        ide_set_status(ide, "file changed on disk; save will overwrite it");
        ide->mtime = st.st_mtime;
    }
}

static void
sort_listing(Ide *ide)
{
    int i;
    int j;
    for (i = 1; i < ide->nnames; i++) {
        char tmp[128];
        int dir = ide->name_dir[i];
        tr_copy_cap(tmp, sizeof tmp, ide->names[i]);
        j = i;
        while (j > 0) {
            int cmp = 0;
            if (ide->name_dir[j - 1] != dir) cmp = ide->name_dir[j - 1] ? -1 : 1;
            else cmp = strcmp(ide->names[j - 1], tmp);
            if (cmp <= 0) break;
            tr_copy_cap(ide->names[j], 128, ide->names[j - 1]);
            ide->name_dir[j] = ide->name_dir[j - 1];
            j--;
        }
        tr_copy_cap(ide->names[j], 128, tmp);
        ide->name_dir[j] = dir;
    }
}

void
ide_browse(Ide *ide, const char *dir)
{
    DIR *d;
    struct dirent *ent;
    char joined[TR_PATH_MAX];
    if (!ide || !dir) return;
    if (tr_copy_cap(ide->browse, sizeof ide->browse, dir) != 0) return;
    ide->nnames = 0;
    d = opendir(dir);
    if (!d) {
        ide_set_status(ide, "cannot open that directory");
        return;
    }
    while ((ent = readdir(d)) != NULL && ide->nnames < 96) {
        struct stat st;
        int n;
        if (strcmp(ent->d_name, ".") == 0) continue;
        if (strlen(ent->d_name) >= 128) continue;
        n = snprintf(joined, sizeof joined, "%s/%s", dir, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof joined) continue;
        if (stat(joined, &st) != 0) continue;
        tr_copy_cap(ide->names[ide->nnames], 128, ent->d_name);
        ide->name_dir[ide->nnames] = S_ISDIR(st.st_mode) ? 1 : 0;
        ide->nnames++;
    }
    closedir(d);
    sort_listing(ide);
}

int
ide_visible(const Ide *ide, const TrWidget *w)
{
    if (!ide || !w) return 0;
    if (ide->show_all_cfg || w->config[0] == '\0') return 1;
    return strcmp(w->config, ide->doc->active_config) == 0;
}

void
ide_set_scene(Ide *ide, const char *name)
{
    int i;
    char msg[64];
    if (!ide || !ide->doc || !name || name[0] == '\0') return;
    for (i = 0; i < ide->doc->nconfigs; i++) {
        if (strcmp(ide->doc->configs[i], name) != 0) continue;
        if (strcmp(ide->doc->active_config, name) != 0) {
            tr_copy_cap(ide->doc->active_config, sizeof ide->doc->active_config, name);
            ide->dirty = 1;
        }
        ide->show_all_cfg = 0;
        snprintf(msg, sizeof msg, "scene %s", name);
        ide_set_status(ide, msg);
        return;
    }
}

void
ide_show_all_scenes(Ide *ide)
{
    if (!ide) return;
    ide->show_all_cfg = 1;
    ide_set_status(ide, "scene all");
}
