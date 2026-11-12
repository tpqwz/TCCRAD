#define _POSIX_C_SOURCE 200809L

#include "ide.h"
#include "codegen.h"

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
ide_primary_event(const TrWidget *w)
{
    static const char *const order[] = {
        "click", "change", "toggle", "select", "draw", "appear", "close", NULL
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

void
ide_open_code(Ide *ide, int id)
{
    TrWidget *w;
    char name[TR_NAME_MAX];
    char msg[96];
    int ev;
    int n;
    if (!ide) return;
    ide_select(ide, id);
    w = tr_doc_widget(ide->doc, id);
    ev = ide_primary_event(w);
    if (ev >= 0) ide->body_ev = ev;
    ide->body_id = 0;
    ide->jump[0] = '\0';
    if (ide_handler_name(ide->doc, w, name, sizeof name) == 0)
        tr_copy_cap(ide->jump, sizeof ide->jump, name);
    ide->view = 1;
    if (ide->modal == IDE_MODAL_CODE || ide->modal == IDE_MODAL_EXPLORE)
        ide->modal = IDE_MODAL_NONE;
    n = snprintf(msg, sizeof msg, "Code %s", ide->jump[0] ? ide->jump : "handler");
    if (n > 0 && (size_t)n < sizeof msg) ide_set_status(ide, msg);
}

void
ide_ui_load(Ide *ide, int screen_h, int dpi)
{
    int saved;
    if (!ide) return;
    saved = ui_scale_read();
    if (saved > 0) ide->ui_pct = ide_ui_clamp(saved);
    else ide->ui_pct = ide_ui_default_pct(screen_h, dpi);
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
    ide_set_status(ide, "New App. Add widgets, then Build & Run with TinyCC.");
    return ide;
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
    ide->path[0] = '\0';
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
