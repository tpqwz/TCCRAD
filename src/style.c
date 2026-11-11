#define _POSIX_C_SOURCE 200809L

#include "style.h"

#include "safe.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const color_names[TR_STYLE_COLORS] = {
    "TEXT", "WINDOW", "HEADER", "BORDER",
    "BUTTON", "BUTTON_HOVER", "BUTTON_ACTIVE",
    "TOGGLE", "TOGGLE_HOVER", "TOGGLE_CURSOR",
    "SELECT", "SELECT_ACTIVE",
    "SLIDER", "SLIDER_CURSOR", "SLIDER_CURSOR_HOVER", "SLIDER_CURSOR_ACTIVE",
    "PROPERTY", "EDIT", "EDIT_CURSOR", "COMBO",
    "CHART", "CHART_COLOR", "CHART_COLOR_HIGHLIGHT",
    "SCROLLBAR", "SCROLLBAR_CURSOR", "SCROLLBAR_CURSOR_HOVER", "SCROLLBAR_CURSOR_ACTIVE",
    "TAB_HEADER",
    "KNOB", "KNOB_CURSOR", "KNOB_CURSOR_HOVER", "KNOB_CURSOR_ACTIVE"
};

static const unsigned char default_rgba[TR_STYLE_COLORS][4] = {
    {175, 175, 175, 255}, {45, 45, 45, 255}, {40, 40, 40, 255}, {65, 65, 65, 255},
    {50, 50, 50, 255}, {40, 40, 40, 255}, {35, 35, 35, 255},
    {100, 100, 100, 255}, {120, 120, 120, 255}, {45, 45, 45, 255},
    {45, 45, 45, 255}, {35, 35, 35, 255},
    {38, 38, 38, 255}, {100, 100, 100, 255}, {120, 120, 120, 255}, {150, 150, 150, 255},
    {38, 38, 38, 255}, {38, 38, 38, 255}, {175, 175, 175, 255}, {45, 45, 45, 255},
    {120, 120, 120, 255}, {45, 45, 45, 255}, {255, 0, 0, 255},
    {40, 40, 40, 255}, {100, 100, 100, 255}, {120, 120, 120, 255}, {150, 150, 150, 255},
    {40, 40, 40, 255},
    {38, 38, 38, 255}, {100, 100, 100, 255}, {120, 120, 120, 255}, {150, 150, 150, 255}
};

const char *
tr_style_color_name(int index)
{
    if (index < 0 || index >= TR_STYLE_COLORS) return "";
    return color_names[index];
}

void
tr_style_defaults(TrStyle *st, const char *name)
{
    int i;
    if (!st) return;
    memset(st, 0, sizeof *st);
    if (tr_copy_cap(st->name, sizeof st->name, name && name[0] ? name : "default") != 0)
        st->name[0] = '\0';
    st->font_px = 14;
    for (i = 0; i < TR_STYLE_COLORS; i++) {
        st->color[i].r = default_rgba[i][0];
        st->color[i].g = default_rgba[i][1];
        st->color[i].b = default_rgba[i][2];
        st->color[i].a = default_rgba[i][3];
    }
}

int
tr_style_name_ok(const char *name)
{
    size_t i;
    if (!name || name[0] == '\0') return 0;
    for (i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (i >= 40) return 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static void
style_err(char *err, size_t cap, const char *msg)
{
    if (err && cap && err[0] == '\0') tr_copy_cap(err, cap, msg);
}

int
tr_style_load(TrStyle *st, const char *dir, const char *name, char *err, size_t errcap)
{
    char path[TR_PATH_MAX];
    char file[64];
    char *text = NULL;
    size_t len = 0;
    char line[256];
    const char *p;
    const char *end;
    int saw[TR_STYLE_COLORS];
    int i;
    if (err && errcap) err[0] = '\0';
    if (!st || !dir || !tr_style_name_ok(name)) {
        style_err(err, errcap, "style name is not a single safe token");
        return -1;
    }
    if (snprintf(file, sizeof file, "%s.ini", name) < 0) return -1;
    if (tr_path_join(path, sizeof path, dir, file) != 0) {
        style_err(err, errcap, "style path is too long");
        return -1;
    }
    tr_style_defaults(st, name);
    if (tr_read_file(path, &text, &len, 64 * 1024) != 0) {
        style_err(err, errcap, "could not read style file");
        return -1;
    }
    memset(saw, 0, sizeof saw);
    p = text;
    end = text + len;
    while (p < end) {
        size_t n = 0;
        int idx, r, g, b, a;
        char *ep;
        while (p < end && *p != '\n') {
            if (n + 1 < sizeof line) line[n++] = *p;
            p++;
        }
        if (p < end && *p == '\n') p++;
        line[n] = '\0';
        if (n && line[n - 1] == '\r') line[--n] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        if (strncmp(line, "font_px ", 8) == 0) {
            long v = strtol(line + 8, &ep, 10);
            if (*ep != '\0' || v < 8 || v > 48) {
                free(text);
                style_err(err, errcap, "font_px is out of range");
                return -1;
            }
            st->font_px = (int)v;
            continue;
        }
        if (strncmp(line, "color ", 6) != 0) continue;
        idx = (int)strtol(line + 6, &ep, 10);
        if (ep == line + 6 || *ep != ' ' || idx < 0 || idx >= TR_STYLE_COLORS) {
            free(text);
            style_err(err, errcap, "color index is invalid");
            return -1;
        }
        r = (int)strtol(ep + 1, &ep, 10);
        if (*ep != ' ') { free(text); style_err(err, errcap, "color line is short"); return -1; }
        g = (int)strtol(ep + 1, &ep, 10);
        if (*ep != ' ') { free(text); style_err(err, errcap, "color line is short"); return -1; }
        b = (int)strtol(ep + 1, &ep, 10);
        if (*ep != ' ') { free(text); style_err(err, errcap, "color line is short"); return -1; }
        a = (int)strtol(ep + 1, &ep, 10);
        if (*ep != '\0' || r < 0 || r > 255 || g < 0 || g > 255 ||
            b < 0 || b > 255 || a < 0 || a > 255) {
            free(text);
            style_err(err, errcap, "color channel is out of range");
            return -1;
        }
        st->color[idx].r = (unsigned char)r;
        st->color[idx].g = (unsigned char)g;
        st->color[idx].b = (unsigned char)b;
        st->color[idx].a = (unsigned char)a;
        saw[idx] = 1;
    }
    free(text);
    for (i = 0; i < TR_STYLE_COLORS; i++) {
        if (!saw[i]) {
            style_err(err, errcap, "style file is missing a color");
            return -1;
        }
    }
    return 0;
}

int
tr_style_save(const TrStyle *st, const char *dir, char *err, size_t errcap)
{
    char file[64];
    TrBuf buf;
    int dirfd = -1;
    int fd = -1;
    int i;
    ssize_t wr;
    size_t off;
    if (err && errcap) err[0] = '\0';
    if (!st || !dir || !tr_style_name_ok(st->name)) {
        style_err(err, errcap, "style name is not a single safe token");
        return -1;
    }
    if (snprintf(file, sizeof file, "%s.ini", st->name) < 0) return -1;
    if (tr_buf_init(&buf) != 0) return -1;
    if (tr_buf_printf(&buf, "font_px %d\n", st->font_px) != 0) {
        tr_buf_free(&buf);
        return -1;
    }
    for (i = 0; i < TR_STYLE_COLORS; i++) {
        if (tr_buf_printf(&buf, "color %d %u %u %u %u\n", i,
                (unsigned)st->color[i].r, (unsigned)st->color[i].g,
                (unsigned)st->color[i].b, (unsigned)st->color[i].a) != 0) {
            tr_buf_free(&buf);
            return -1;
        }
    }
    dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0) {
        tr_buf_free(&buf);
        style_err(err, errcap, "style directory cannot be opened");
        return -1;
    }
    fd = openat(dirfd, file, O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        close(dirfd);
        tr_buf_free(&buf);
        style_err(err, errcap, "style file cannot be opened");
        return -1;
    }
    off = 0;
    while (off < buf.len) {
        wr = write(fd, buf.data + off, buf.len - off);
        if (wr < 0) {
            close(fd);
            close(dirfd);
            tr_buf_free(&buf);
            style_err(err, errcap, "style file write failed");
            return -1;
        }
        off += (size_t)wr;
    }
    if (close(fd) != 0) {
        close(dirfd);
        tr_buf_free(&buf);
        return -1;
    }
    close(dirfd);
    tr_buf_free(&buf);
    return 0;
}
