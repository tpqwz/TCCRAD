#define _POSIX_C_SOURCE 200809L
#include "ide.h"
#include "codegen.h"
#include "rope.h"
#include "nk_glue.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int nhits;
static struct { int id; struct nk_rect r; } hits[TR_WIDGETS_MAX];
static int g_pct = 100;
static int style_ready;
static int style_scheme = -1;
static struct nk_style style_base;

static void run_check(Ide *ide);
static int explore_procs(Ide *ide);
static void draw_code_info(struct nk_context *ctx, Ide *ide, const char *text, size_t n,
    int caret, int visible, char names[][48], const int *rlines, int nrout);

/* Paper chrome. Order matches enum nk_style_colors. */
static const struct nk_color light_table[NK_COLOR_COUNT] = {
    {32, 32, 32, 255},
    {246, 246, 246, 255},
    {232, 232, 232, 255},
    {196, 196, 196, 255},
    {232, 232, 232, 255},
    {214, 224, 238, 255},
    {196, 210, 230, 255},
    {176, 176, 176, 255},
    {156, 168, 188, 255},
    {40, 90, 160, 255},
    {214, 228, 246, 255},
    {186, 208, 236, 255},
    {220, 220, 220, 255},
    {70, 110, 170, 255},
    {50, 90, 155, 255},
    {36, 72, 140, 255},
    {255, 255, 255, 255},
    {255, 255, 255, 255},
    {30, 30, 30, 255},
    {255, 255, 255, 255},
    {210, 210, 210, 255},
    {70, 110, 170, 255},
    {180, 40, 40, 255},
    {236, 236, 236, 255},
    {180, 180, 180, 255},
    {150, 150, 150, 255},
    {120, 120, 120, 255},
    {228, 228, 228, 255},
    {232, 232, 232, 255},
    {70, 110, 170, 255},
    {50, 90, 155, 255},
    {36, 72, 140, 255}
};
_Static_assert(sizeof light_table / sizeof light_table[0] == 32, "light table");
_Static_assert((int)NK_COLOR_COUNT == 32, "nuklear colors");

static int
px(int v)
{
    long s;
    if (v <= 0) return v;
    s = (long)v * (long)g_pct;
    if (s / (long)v != (long)g_pct) return v;
    s /= 100L;
    if (s < 1) s = 1;
    if (s > 100000L) s = 100000L;
    return (int)s;
}

static float
pxf(int v)
{
    return (float)px(v);
}

static float
sc(float v, int pct)
{
    return v * (float)pct / 100.0f;
}

static struct nk_vec2
sc2(struct nk_vec2 v, int pct)
{
    v.x = sc(v.x, pct);
    v.y = sc(v.y, pct);
    return v;
}

static void
scale_button(struct nk_style_button *b, int pct)
{
    b->border = sc(b->border, pct);
    b->rounding = sc(b->rounding, pct);
    b->padding = sc2(b->padding, pct);
    b->image_padding = sc2(b->image_padding, pct);
    b->touch_padding = sc2(b->touch_padding, pct);
}

static void
scale_toggle(struct nk_style_toggle *t, int pct)
{
    t->padding = sc2(t->padding, pct);
    t->touch_padding = sc2(t->touch_padding, pct);
    t->spacing = sc(t->spacing, pct);
    t->border = sc(t->border, pct);
}

static void
scale_scroll(struct nk_style_scrollbar *s, int pct)
{
    s->border = sc(s->border, pct);
    s->rounding = sc(s->rounding, pct);
    s->border_cursor = sc(s->border_cursor, pct);
    s->rounding_cursor = sc(s->rounding_cursor, pct);
    s->padding = sc2(s->padding, pct);
    scale_button(&s->inc_button, pct);
    scale_button(&s->dec_button, pct);
}

static void
scale_edit(struct nk_style_edit *e, int pct)
{
    e->border = sc(e->border, pct);
    e->rounding = sc(e->rounding, pct);
    e->cursor_size = sc(e->cursor_size, pct);
    e->scrollbar_size = sc2(e->scrollbar_size, pct);
    e->padding = sc2(e->padding, pct);
    e->row_padding = sc(e->row_padding, pct);
    scale_scroll(&e->scrollbar, pct);
}

static void
scale_style(struct nk_style *s, int pct)
{
    struct nk_style_window *w = &s->window;
    if (pct == 100) return;
    s->text.padding = sc2(s->text.padding, pct);
    s->link.underline_thickness = sc(s->link.underline_thickness, pct);
    s->link.padding = sc2(s->link.padding, pct);
    s->link.touch_padding = sc2(s->link.touch_padding, pct);
    scale_button(&s->button, pct);
    scale_button(&s->contextual_button, pct);
    scale_button(&s->menu_button, pct);
    scale_toggle(&s->option, pct);
    scale_toggle(&s->checkbox, pct);
    s->selectable.rounding = sc(s->selectable.rounding, pct);
    s->selectable.padding = sc2(s->selectable.padding, pct);
    s->selectable.touch_padding = sc2(s->selectable.touch_padding, pct);
    s->selectable.image_padding = sc2(s->selectable.image_padding, pct);
    s->slider.border = sc(s->slider.border, pct);
    s->slider.rounding = sc(s->slider.rounding, pct);
    s->slider.bar_height = sc(s->slider.bar_height, pct);
    s->slider.padding = sc2(s->slider.padding, pct);
    s->slider.spacing = sc2(s->slider.spacing, pct);
    s->slider.cursor_size = sc2(s->slider.cursor_size, pct);
    scale_button(&s->slider.inc_button, pct);
    scale_button(&s->slider.dec_button, pct);
    s->knob.border = sc(s->knob.border, pct);
    s->knob.knob_border = sc(s->knob.knob_border, pct);
    s->knob.padding = sc2(s->knob.padding, pct);
    s->knob.spacing = sc2(s->knob.spacing, pct);
    s->knob.cursor_width = sc(s->knob.cursor_width, pct);
    s->progress.rounding = sc(s->progress.rounding, pct);
    s->progress.border = sc(s->progress.border, pct);
    s->progress.cursor_border = sc(s->progress.cursor_border, pct);
    s->progress.cursor_rounding = sc(s->progress.cursor_rounding, pct);
    s->progress.padding = sc2(s->progress.padding, pct);
    s->property.border = sc(s->property.border, pct);
    s->property.rounding = sc(s->property.rounding, pct);
    s->property.padding = sc2(s->property.padding, pct);
    scale_edit(&s->property.edit, pct);
    scale_button(&s->property.inc_button, pct);
    scale_button(&s->property.dec_button, pct);
    scale_edit(&s->edit, pct);
    s->chart.border = sc(s->chart.border, pct);
    s->chart.rounding = sc(s->chart.rounding, pct);
    s->chart.padding = sc2(s->chart.padding, pct);
    scale_scroll(&s->scrollh, pct);
    scale_scroll(&s->scrollv, pct);
    s->tab.border = sc(s->tab.border, pct);
    s->tab.rounding = sc(s->tab.rounding, pct);
    s->tab.indent = sc(s->tab.indent, pct);
    s->tab.padding = sc2(s->tab.padding, pct);
    s->tab.spacing = sc2(s->tab.spacing, pct);
    scale_button(&s->tab.tab_maximize_button, pct);
    scale_button(&s->tab.tab_minimize_button, pct);
    scale_button(&s->tab.node_maximize_button, pct);
    scale_button(&s->tab.node_minimize_button, pct);
    s->combo.border = sc(s->combo.border, pct);
    s->combo.rounding = sc(s->combo.rounding, pct);
    s->combo.content_padding = sc2(s->combo.content_padding, pct);
    s->combo.button_padding = sc2(s->combo.button_padding, pct);
    s->combo.spacing = sc2(s->combo.spacing, pct);
    scale_button(&s->combo.button, pct);
    w->border = sc(w->border, pct);
    w->combo_border = sc(w->combo_border, pct);
    w->contextual_border = sc(w->contextual_border, pct);
    w->menu_border = sc(w->menu_border, pct);
    w->group_border = sc(w->group_border, pct);
    w->tooltip_border = sc(w->tooltip_border, pct);
    w->popup_border = sc(w->popup_border, pct);
    w->min_row_height_padding = sc(w->min_row_height_padding, pct);
    w->rounding = sc(w->rounding, pct);
    w->spacing = sc2(w->spacing, pct);
    w->scrollbar_size = sc2(w->scrollbar_size, pct);
    w->min_size = sc2(w->min_size, pct);
    w->padding = sc2(w->padding, pct);
    w->group_padding = sc2(w->group_padding, pct);
    w->popup_padding = sc2(w->popup_padding, pct);
    w->combo_padding = sc2(w->combo_padding, pct);
    w->contextual_padding = sc2(w->contextual_padding, pct);
    w->menu_padding = sc2(w->menu_padding, pct);
    w->tooltip_padding = sc2(w->tooltip_padding, pct);
    w->header.padding = sc2(w->header.padding, pct);
    w->header.label_padding = sc2(w->header.label_padding, pct);
    w->header.spacing = sc2(w->header.spacing, pct);
    scale_button(&w->header.close_button, pct);
    scale_button(&w->header.minimize_button, pct);
}

static void
prepare_style(struct nk_context *ctx, Ide *ide)
{
    const struct nk_user_font *font;
    int pct = ide && ide->ui_pct > 0 ? ide_ui_clamp(ide->ui_pct) : 100;
    int scheme = ide && ide->scheme == IDE_SCHEME_LIGHT ? IDE_SCHEME_LIGHT : IDE_SCHEME_DARK;
    font = ctx->style.font;
    if (!style_ready || style_scheme != scheme) {
        nk_style_from_table(ctx, scheme == IDE_SCHEME_LIGHT ? light_table : NULL);
        style_base = ctx->style;
        style_ready = 1;
        style_scheme = scheme;
    }
    ctx->style = style_base;
    scale_style(&ctx->style, pct);
    ctx->style.font = font;
    g_pct = pct;
}

static void
note_hit(struct nk_context *ctx, int id)
{
    if (nhits >= TR_WIDGETS_MAX) return;
    hits[nhits].id = id;
    hits[nhits].r = nk_widget_bounds(ctx);
    nhits++;
}

static int
split_list(const char *s, char parts[][48], int max)
{
    int n = 0;
    if (!s || max < 1) return 0;
    while (*s && n < max) {
        const char *semi = strchr(s, ';');
        size_t len = semi ? (size_t)(semi - s) : strlen(s);
        if (len >= 48) len = 47;
        memcpy(parts[n], s, len);
        parts[n][len] = '\0';
        if (parts[n][0] == '\0') tr_copy_cap(parts[n], 48, " ");
        n++;
        if (!semi) break;
        s = semi + 1;
    }
    return n;
}

static const char *
shown(const TrWidget *w, const char *key, char *buf, size_t cap)
{
    const TrProp *p = tr_prop(w, key);
    int n;
    if (!p) return "";
    if (p->bind[0] != '\0') {
        n = snprintf(buf, cap, "{%s}", p->bind);
        if (n < 0 || (size_t)n >= cap) return p->bind;
        return buf;
    }
    return p->value;
}

static int
field_init_int(const TrDoc *doc, const char *name, int *out)
{
    const TrField *f;
    char *end;
    long v;
    if (!doc || !name || !out || !tr_is_ident(name)) return 0;
    f = tr_field_find(doc, name);
    if (!f || f->init[0] == '\0') return 0;
    errno = 0;
    v = strtol(f->init, &end, 10);
    if (errno != 0 || end == f->init || *end != '\0') return 0;
    if (v > INT_MAX || v < INT_MIN) return 0;
    *out = (int)v;
    return 1;
}

static int
preview_int(const Ide *ide, const TrWidget *w, const char *key, int def)
{
    const TrProp *p = tr_prop(w, key);
    int bound;
    if (p && p->bind[0] != '\0' && ide && field_init_int(ide->doc, p->bind, &bound))
        return bound;
    return tr_prop_int(w, key, def);
}

static void preview_children(struct nk_context *ctx, Ide *ide, int parent);

static int
double_click(Ide *ide, int id)
{
    struct timespec ts;
    long long ms;
    int again;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    ms = (long long)ts.tv_sec * 1000LL;
    ms += (long long)(ts.tv_nsec / 1000000L);
    again = id == ide->click_id && ide->click_ms > 0 &&
        ms >= ide->click_ms && ms - ide->click_ms < 450;
    ide->click_id = id;
    ide->click_ms = ms;
    return again;
}

static struct { int id; int page; } design_pages[64];
static int design_npages;

static int
page_of(int id, int n, int fallback)
{
    int i;
    if (n < 1) return 0;
    if (fallback < 0 || fallback >= n) fallback = 0;
    for (i = 0; i < design_npages; i++) {
        if (design_pages[i].id != id) continue;
        if (design_pages[i].page < 0 || design_pages[i].page >= n)
            design_pages[i].page = fallback;
        return design_pages[i].page;
    }
    if (design_npages < 64) {
        design_pages[design_npages].id = id;
        design_pages[design_npages].page = fallback;
        design_npages++;
    }
    return fallback;
}

static void
page_set(int id, int page)
{
    int i;
    for (i = 0; i < design_npages; i++) {
        if (design_pages[i].id != id) continue;
        design_pages[i].page = page;
        return;
    }
    if (design_npages < 64) {
        design_pages[design_npages].id = id;
        design_pages[design_npages].page = page;
        design_npages++;
    }
}

static void
stroke_selected(struct nk_context *ctx, const Ide *ide, int id, struct nk_rect r)
{
    struct nk_color c;
    if (!ide || ide->selected != id || r.w < 1.0f || r.h < 1.0f) return;
    c = ide->scheme == IDE_SCHEME_LIGHT ? nk_rgb(20, 90, 180) : nk_rgb(140, 190, 255);
    nk_stroke_rect(nk_window_get_canvas(ctx), r, 2.0f, 2.0f, c);
}

static const char *
titled(const TrWidget *w, const char *key, char *buf, size_t cap)
{
    const char *s = shown(w, key, buf, cap);
    return s[0] ? s : w->name;
}

static int
open_frame(struct nk_context *ctx, const TrWidget *w, const char *title)
{
    return nk_group_begin_titled(ctx, w->name, title,
        NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR) ? 1 : 0;
}

static void preview_children(struct nk_context *ctx, Ide *ide, int parent);

static void
preview_one(struct nk_context *ctx, Ide *ide, const TrWidget *w)
{
    char buf[TR_TEXT_MAX + 8];
    struct nk_rect slot;
    if (!ide_visible(ide, w)) return;
    switch (w->kind) {
    case TR_KIND_MENUBAR: {
        int idxs[TR_WIDGETS_MAX];
        int n = tr_child_indices(ide->doc, w->id, idxs, TR_WIDGETS_MAX);
        int i;
        if (n < 1) {
            nk_label(ctx, w->name, NK_TEXT_LEFT);
            break;
        }
        nk_layout_row_begin(ctx, NK_STATIC, pxf(28), n);
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        for (i = 0; i < n; i++) {
            const TrWidget *menu = &ide->doc->widgets[idxs[i]];
            nk_layout_row_push(ctx, pxf(110));
            note_hit(ctx, menu->id);
            nk_label(ctx, titled(menu, "label", buf, sizeof buf), NK_TEXT_LEFT);
        }
        nk_layout_row_end(ctx);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_POPUP:
    case TR_KIND_MODAL: {
        int hh = w->h > 48 ? w->h : 160;
        nk_layout_row_dynamic(ctx, (float)hh * (float)g_pct / 100.0f, 1);
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (open_frame(ctx, w, titled(w, "title", buf, sizeof buf))) {
            preview_children(ctx, ide, w->id);
            nk_group_end(ctx);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_LABEL:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, shown(w, "text", buf, sizeof buf), NK_TEXT_LEFT);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    case TR_KIND_BUTTON:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_button_label(ctx, titled(w, "label", buf, sizeof buf));
        stroke_selected(ctx, ide, w->id, slot);
        break;
    case TR_KIND_CHECKBOX: {
        nk_bool on = preview_int(ide, w, "checked", 0) ? nk_true : nk_false;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_checkbox_label(ctx, titled(w, "label", buf, sizeof buf), &on);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_RADIO: {
        char parts[8][48];
        int nopt;
        int sel;
        int oi;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nopt = split_list(shown(w, "options", buf, sizeof buf), parts, 8);
        if (nopt < 1) {
            tr_copy_cap(parts[0], 48, " ");
            nopt = 1;
        }
        sel = preview_int(ide, w, "selected", 0);
        if (sel < 0 || sel >= nopt) sel = 0;
        if (nk_group_begin(ctx, w->name, NK_WINDOW_NO_SCROLLBAR)) {
            for (oi = 0; oi < nopt; oi++) {
                nk_bool on = oi == sel ? nk_true : nk_false;
                nk_layout_row_dynamic(ctx, pxf(20), 1);
                nk_option_label(ctx, parts[oi], on);
            }
            nk_group_end(ctx);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_INPUT: {
        int flags = NK_EDIT_FIELD | NK_EDIT_READ_ONLY;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (tr_prop_int(w, "password", 0)) {
            size_t i;
            size_t n = strlen(shown(w, "text", buf, sizeof buf));
            if (n > 12) n = 12;
            for (i = 0; i < n; i++) buf[i] = '*';
            buf[n] = '\0';
        } else {
            shown(w, "text", buf, sizeof buf);
        }
        if (buf[0] == '\0') tr_copy_cap(buf, sizeof buf, " ");
        nk_edit_string_zero_terminated(ctx, (nk_flags)flags, buf, (int)sizeof buf, nk_filter_default);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_SLIDER: {
        int v = preview_int(ide, w, "value", 0);
        int mn = preview_int(ide, w, "min", 0);
        int mx = preview_int(ide, w, "max", 100);
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (mx <= mn) mx = mn + 1;
        if (v < mn) v = mn;
        if (v > mx) v = mx;
        nk_slider_int(ctx, mn, &v, mx, 1);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_COMBO: {
        char parts[8][48];
        const char *items[8];
        int nopt;
        int sel;
        int oi;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nopt = split_list(shown(w, "items", buf, sizeof buf), parts, 8);
        if (nopt < 1) {
            tr_copy_cap(parts[0], 48, " ");
            nopt = 1;
        }
        for (oi = 0; oi < nopt; oi++) items[oi] = parts[oi];
        sel = preview_int(ide, w, "selected", 0);
        if (sel < 0 || sel >= nopt) sel = 0;
        nk_combo(ctx, items, nopt, sel, px(22), nk_vec2(pxf(200), pxf(20 * nopt + 12)));
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_PROGRESS: {
        int mx = preview_int(ide, w, "max", 100);
        nk_size cur;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (mx < 1) mx = 1;
        {
            int pv = preview_int(ide, w, "value", 0);
            if (pv < 0) pv = 0;
            cur = (nk_size)pv;
        }
        if (cur > (nk_size)mx) cur = (nk_size)mx;
        nk_progress(ctx, &cur, (nk_size)mx, nk_false);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_COLOR: {
        struct nk_color col = nk_rgb(tr_prop_int(w, "r", 80), tr_prop_int(w, "g", 140),
            tr_prop_int(w, "b", 220));
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_button_color(ctx, col);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_SEPARATOR: {
        struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
        struct nk_color ink = ide->scheme == IDE_SCHEME_LIGHT ? nk_rgb(170, 170, 170)
                                                               : nk_rgb(90, 90, 90);
        float y;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (nk_widget(&slot, ctx)) {
            y = slot.y + slot.h * 0.5f;
            nk_stroke_line(cv, slot.x + 4.0f, y, slot.x + slot.w - 4.0f, y, 1.0f, ink);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_SPACER:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, " ", NK_TEXT_LEFT);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    case TR_KIND_IMAGE: {
        const char *path;
        const char *base;
        struct nk_command_buffer *cv;
        struct nk_color edge = ide->scheme == IDE_SCHEME_LIGHT ? nk_rgb(150, 150, 160)
                                                                : nk_rgb(110, 110, 120);
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        path = titled(w, "path", buf, sizeof buf);
        base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (base[0] == '\0') base = "image";
        if (nk_widget(&slot, ctx)) {
            cv = nk_window_get_canvas(ctx);
            nk_stroke_rect(cv, slot, 2.0f, 1.0f, edge);
            if (ctx->style.font)
                nk_draw_text(cv, nk_rect(slot.x + 6.0f, slot.y + 4.0f, slot.w - 8.0f, slot.h),
                    base, (int)strlen(base), ctx->style.font, nk_rgba(0, 0, 0, 0), edge);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_CUSTOM:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, titled(w, "func", buf, sizeof buf), NK_TEXT_LEFT);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    case TR_KIND_MENUITEM:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, titled(w, "label", buf, sizeof buf), NK_TEXT_LEFT);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    case TR_KIND_TABS: {
        int idxs[TR_WIDGETS_MAX];
        int n = tr_child_indices(ide->doc, w->id, idxs, TR_WIDGETS_MAX);
        int page;
        int i;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (n < 0) n = 0;
        page = page_of(w->id, n, tr_prop_int(w, "selected", 0));
        if (open_frame(ctx, w, " ")) {
            if (n > 0) nk_layout_row_dynamic(ctx, pxf(26), n);
            for (i = 0; i < n; i++) {
                const TrWidget *tab = &ide->doc->widgets[idxs[i]];
                int on = i == page;
                note_hit(ctx, tab->id);
                if (nk_selectable_label(ctx, titled(tab, "title", buf, sizeof buf),
                        NK_TEXT_CENTERED, &on) && on)
                    page_set(w->id, i);
            }
            if (n > 0 && page >= 0 && page < n)
                preview_children(ctx, ide, ide->doc->widgets[idxs[page]].id);
            nk_group_end(ctx);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_SPLITTER:
    case TR_KIND_DOCK: {
        int idxs[TR_WIDGETS_MAX];
        int n = tr_flow_indices(ide->doc, w->id, idxs, TR_WIDGETS_MAX);
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (n < 0) n = 0;
        if (open_frame(ctx, w, w->name)) {
            nk_layout_row_dynamic(ctx, pxf(160), n > 1 ? 2 : 1);
            if (n > 0) preview_one(ctx, ide, &ide->doc->widgets[idxs[0]]);
            if (n > 1) preview_one(ctx, ide, &ide->doc->widgets[idxs[1]]);
            nk_group_end(ctx);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_TABLE: {
        const char *cols = shown(w, "columns", buf, sizeof buf);
        char parts[6][48];
        int nc = 0;
        const char *p = cols;
        int row;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        while (*p && nc < 6) {
            const char *semi = strchr(p, ';');
            size_t len = semi ? (size_t)(semi - p) : strlen(p);
            if (len >= sizeof parts[0]) len = sizeof parts[0] - 1;
            memcpy(parts[nc], p, len);
            parts[nc][len] = '\0';
            nc++;
            if (!semi) break;
            p = semi + 1;
        }
        if (nc < 1) {
            memcpy(parts[0], "Value", 6);
            nc = 1;
        }
        if (open_frame(ctx, w, w->name)) {
            int nrows = tr_prop_int(w, "rows", 3);
            if (nrows < 1) nrows = 1;
            if (nrows > 4) nrows = 4;
            nk_layout_row_dynamic(ctx, pxf(22), nc);
            for (row = 0; row < nc; row++) nk_label(ctx, parts[row], NK_TEXT_LEFT);
            for (row = 0; row < nrows; row++) {
                int c;
                char cell[16];
                nk_layout_row_dynamic(ctx, pxf(22), nc);
                for (c = 0; c < nc; c++) {
                    snprintf(cell, sizeof cell, "%d", row + 1);
                    nk_label(ctx, c == 0 ? cell : "·", NK_TEXT_LEFT);
                }
            }
            preview_children(ctx, ide, w->id);
            nk_group_end(ctx);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_COLLAPSE:
    case TR_KIND_TREE: {
        const char *title = titled(w, "title", buf, sizeof buf);
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (nk_tree_push_id(ctx, w->kind == TR_KIND_TREE ? NK_TREE_NODE : NK_TREE_TAB,
                title, NK_MAXIMIZED, w->id)) {
            preview_children(ctx, ide, w->id);
            nk_tree_pop(ctx);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_GROUP:
    case TR_KIND_CHILD:
    case TR_KIND_TAB:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (w->kind == TR_KIND_TAB) {
            preview_children(ctx, ide, w->id);
        } else if (open_frame(ctx, w, titled(w, "title", buf, sizeof buf))) {
            preview_children(ctx, ide, w->id);
            nk_group_end(ctx);
        }
        stroke_selected(ctx, ide, w->id, slot);
        break;
    default:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, w->name, NK_TEXT_LEFT);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
}

static int
design_row_h(const TrDoc *doc, const TrRow *row)
{
    int h;
    int i;
    if (!row) return 28;
    h = row->height;
    if (h < 0) return 96;
    if (h < 28) h = 28;
    if (!doc) return h;
    for (i = 0; i < row->n; i++) {
        const TrWidget *w = &doc->widgets[row->at[i]];
        int need = 0;
        if (w->kind == TR_KIND_TABS)
            need = 560;
        else if (w->kind == TR_KIND_RADIO) {
            const TrProp *opt = tr_prop(w, "options");
            const char *s = opt ? opt->value : "";
            int count = 1;
            while (*s) {
                if (*s == ';') count++;
                s++;
            }
            if (count > 8) count = 8;
            need = count * 22 + 4;
        }
        else if (w->kind == TR_KIND_SPLITTER || w->kind == TR_KIND_DOCK ||
            w->kind == TR_KIND_TABLE)
            need = 220;
        else if ((w->kind == TR_KIND_GROUP || w->kind == TR_KIND_CHILD ||
                w->kind == TR_KIND_COLLAPSE || w->kind == TR_KIND_TREE) && w->h < 80)
            need = 140;
        if (need > h) h = need;
    }
    return h;
}

static void
preview_children(struct nk_context *ctx, Ide *ide, int parent)
{
    int idxs[TR_WIDGETS_MAX];
    TrRow rows[TR_WIDGETS_MAX];
    int n;
    int nr;
    int r;
    int c;
    int i;
    n = tr_child_indices(ide->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n < 0) return;
    for (i = 0; i < n; i++) {
        if (ide->doc->widgets[idxs[i]].kind == TR_KIND_MENUBAR)
            preview_one(ctx, ide, &ide->doc->widgets[idxs[i]]);
    }
    n = tr_flow_indices(ide->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n > 0) {
        nr = tr_pack_rows(ide->doc, idxs, n, rows, TR_WIDGETS_MAX);
        if (nr > 0) {
            for (r = 0; r < nr; r++) {
                float h = (float)design_row_h(ide->doc, &rows[r]);
                h = h * (float)g_pct / 100.0f;
                if (h < 1.0f) h = 1.0f;
                nk_layout_row_dynamic(ctx, h, rows[r].n > 0 ? rows[r].n : 1);
                for (c = 0; c < rows[r].n; c++)
                    preview_one(ctx, ide, &ide->doc->widgets[rows[r].at[c]]);
            }
        }
    }
    n = tr_child_indices(ide->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n < 0) return;
    for (i = 0; i < n; i++) {
        TrKind k = ide->doc->widgets[idxs[i]].kind;
        if (k == TR_KIND_POPUP || k == TR_KIND_MODAL)
            preview_one(ctx, ide, &ide->doc->widgets[idxs[i]]);
    }
}

static void
draw_scenes(struct nk_context *ctx, Ide *ide)
{
    int i;
    TrDoc *doc = ide->doc;
    int all;
    if (!doc) return;
    for (i = 0; i < doc->nconfigs; i++) {
        int on = !ide->show_all_cfg && strcmp(doc->configs[i], doc->active_config) == 0;
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_selectable_label(ctx, doc->configs[i], NK_TEXT_LEFT, &on))
            ide_set_scene(ide, doc->configs[i]);
    }
    all = ide->show_all_cfg;
    nk_layout_row_dynamic(ctx, pxf(22), 1);
    if (nk_selectable_label(ctx, "All", NK_TEXT_LEFT, &all)) ide_show_all_scenes(ide);
}

static void
draw_tree(struct nk_context *ctx, Ide *ide, int parent, int depth)
{
    int idxs[TR_WIDGETS_MAX];
    int n = tr_child_indices(ide->doc, parent, idxs, TR_WIDGETS_MAX);
    int i;
    if (n < 0) return;
    for (i = 0; i < n; i++) {
        const TrWidget *w = &ide->doc->widgets[idxs[i]];
        char line[96];
        int pad = depth * 2;
        if (!ide_visible(ide, w)) continue;
        if (pad > 12) pad = 12;
        snprintf(line, sizeof line, "%*s%s %s", pad, "", tr_kind_name(w->kind), w->name);
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        {
            int on = w->id == ide->selected;
            if (nk_selectable_label(ctx, line, NK_TEXT_LEFT, &on)) {
                if (double_click(ide, w->id)) ide_open_code(ide, w->id);
                else ide_select(ide, w->id);
            }
        }
        if (tr_kind_is_container(w->kind)) draw_tree(ctx, ide, w->id, depth + 1);
    }
}

static void
prop_editor(struct nk_context *ctx, Ide *ide, TrWidget *w)
{
    int n = 0;
    int i;
    const TrPropDef *defs = tr_prop_defs(w->kind, &n);
    nk_layout_row_dynamic(ctx, pxf(22), 1);
    nk_label(ctx, tr_kind_name(w->kind), NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, pxf(24), 2);
    nk_label(ctx, "name", NK_TEXT_LEFT);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, w->name, TR_NAME_MAX, nk_filter_default);
    nk_layout_row_dynamic(ctx, pxf(24), 2);
    nk_label(ctx, "newline", NK_TEXT_LEFT);
    nk_checkbox_label(ctx, "", &w->newline);
    nk_layout_row_dynamic(ctx, pxf(24), 1);
    w->stretch = nk_propertyi(ctx, "stretch", 0, w->stretch, 20, 1, 1.0f);
    nk_layout_row_dynamic(ctx, pxf(24), 2);
    w->x = nk_propertyi(ctx, "x", -2000, w->x, 4000, 1, 1.0f);
    w->y = nk_propertyi(ctx, "y", -2000, w->y, 4000, 1, 1.0f);
    w->w = nk_propertyi(ctx, "w", -2000, w->w, 4000, 1, 1.0f);
    w->h = nk_propertyi(ctx, "h", -2000, w->h, 4000, 1, 1.0f);
    nk_layout_row_dynamic(ctx, pxf(22), 1);
    nk_checkbox_label(ctx, "overlay", &w->overlay);
    w->anchor = nk_propertyi(ctx, "anchor 0-3", 0, w->anchor, 3, 1, 1.0f);
    nk_layout_row_dynamic(ctx, pxf(24), 2);
    nk_label(ctx, "config", NK_TEXT_LEFT);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, w->config, TR_NAME_MAX, nk_filter_default);
    for (i = 0; i < n; i++) {
        TrProp *p = tr_prop_mut(w, defs[i].key);
        int as_int;
        if (!p) continue;
        nk_layout_row_dynamic(ctx, pxf(18), 1);
        nk_label(ctx, defs[i].key, NK_TEXT_LEFT);
        if (defs[i].val == TR_VAL_BOOL) {
            int on = tr_prop_int(w, defs[i].key, 0) ? 1 : 0;
            int before = on;
            nk_layout_row_dynamic(ctx, pxf(22), 1);
            nk_checkbox_label(ctx, "on", &on);
            if (on != before) tr_prop_set(w, defs[i].key, on ? "1" : "0");
        } else if (defs[i].val == TR_VAL_INT) {
            as_int = tr_prop_int(w, defs[i].key, 0);
            nk_layout_row_dynamic(ctx, pxf(24), 1);
            as_int = nk_propertyi(ctx, "#", -100000, as_int, 100000, 1, 1.0f);
            {
                char tmp[32];
                snprintf(tmp, sizeof tmp, "%d", as_int);
                if (strcmp(tmp, p->value) != 0) tr_prop_set(w, defs[i].key, tmp);
            }
        } else {
            nk_layout_row_dynamic(ctx, pxf(24), 1);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, p->value, TR_TEXT_MAX, nk_filter_default);
        }
        if (defs[i].bindable) {
            nk_layout_row_dynamic(ctx, pxf(22), 2);
            nk_label(ctx, "bind", NK_TEXT_LEFT);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, p->bind, TR_EXPR_MAX, nk_filter_default);
            if (defs[i].twoway_ok) nk_checkbox_label(ctx, "two-way", &p->twoway);
        }
    }
    if (w->parent < 0) {
        static const char *const kinds[] = {"window", "main", "popup", "modal", "activity"};
        static const TrKind values[] = {
            TR_KIND_WINDOW, TR_KIND_MAIN, TR_KIND_POPUP, TR_KIND_MODAL, TR_KIND_ACTIVITY
        };
        int cur = 0;
        int k;
        for (k = 0; k < 5; k++) if (values[k] == w->kind) cur = k;
        nk_layout_row_dynamic(ctx, pxf(24), 1);
        cur = nk_combo(ctx, kinds, 5, cur, px(22), nk_vec2((float)px(180), (float)px(140)));
        if (cur >= 0 && cur < 5 && values[cur] != w->kind) tr_doc_set_root_kind(ide->doc, values[cur]);
    }
}

static int ev_pick_id;
static int ev_pick_i = -1;

static const char *
event_title(const char *name)
{
    if (!name) return "";
    if (strcmp(name, "click") == 0) return "OnClick";
    if (strcmp(name, "dblclick") == 0) return "OnDblClick";
    if (strcmp(name, "down") == 0) return "OnMouseDown";
    if (strcmp(name, "up") == 0) return "OnMouseUp";
    if (strcmp(name, "move") == 0) return "OnMouseMove";
    if (strcmp(name, "enter") == 0) return "OnMouseEnter";
    if (strcmp(name, "leave") == 0) return "OnMouseLeave";
    if (strcmp(name, "focus") == 0) return "OnEnter";
    if (strcmp(name, "blur") == 0) return "OnExit";
    if (strcmp(name, "keydown") == 0) return "OnKeyDown";
    if (strcmp(name, "keyup") == 0) return "OnKeyUp";
    if (strcmp(name, "change") == 0) return "OnChange";
    if (strcmp(name, "toggle") == 0) return "OnToggle";
    if (strcmp(name, "select") == 0) return "OnSelect";
    if (strcmp(name, "appear") == 0) return "OnShow";
    if (strcmp(name, "close") == 0) return "OnClose";
    if (strcmp(name, "resize") == 0) return "OnResize";
    if (strcmp(name, "paint") == 0) return "OnPaint";
    if (strcmp(name, "draw") == 0) return "OnPaint";
    return name;
}

static void
event_editor(struct nk_context *ctx, Ide *ide, TrWidget *w)
{
    const char *const *defs;
    int n = 0;
    int i;
    int picked = -1;
    char head[64];
    defs = tr_event_defs(w->kind, &n);
    snprintf(head, sizeof head, "%s", tr_kind_name(w->kind));
    nk_layout_row_dynamic(ctx, pxf(20), 1);
    nk_label(ctx, head, NK_TEXT_LEFT);
    if (!defs || n <= 0) {
        nk_label(ctx, "This component has no events.", NK_TEXT_LEFT);
        return;
    }
    nk_layout_row_template_begin(ctx, pxf(20));
    nk_layout_row_template_push_static(ctx, pxf(108));
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, "event", NK_TEXT_LEFT);
    nk_label(ctx, "handler", NK_TEXT_LEFT);
    if (ev_pick_id != w->id) ev_pick_i = -1;
    for (i = 0; i < n; i++) {
        int slot = -1;
        int e;
        int on;
        char row[96];
        const char *hn = "";
        const char *title = event_title(defs[i]);
        for (e = 0; e < w->nevents; e++) {
            if (strcmp(w->events[e].name, defs[i]) == 0) {
                slot = e;
                if (w->events[e].handler[0] != '\0') hn = w->events[e].handler;
                break;
            }
        }
        snprintf(row, sizeof row, "%-14s %s", title, hn);
        on = ev_pick_i == i;
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_selectable_label(ctx, row, NK_TEXT_LEFT, &on)) {
            ev_pick_id = w->id;
            ev_pick_i = i;
            if (double_click(ide, 500000 + w->id * 32 + i))
                ide_open_named_event(ide, w->id, defs[i]);
            else if (slot >= 0) {
                if (ide->body_ev != slot) ide->body_id = 0;
                ide->body_ev = slot;
            }
        }
        if (ev_pick_i == i) picked = slot;
    }
    nk_layout_row_dynamic(ctx, pxf(18), 1);
    nk_label(ctx, "Double-click a row to create and open that handler.", NK_TEXT_LEFT);
    if (ev_pick_i < 0 || ev_pick_i >= n) return;
    if (picked < 0 || picked >= w->nevents) return;
    nk_layout_row_dynamic(ctx, pxf(22), 1);
    nk_label(ctx, "handler name", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, pxf(24), 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, w->events[picked].handler,
        TR_NAME_MAX, nk_filter_default);
}

static int
panel(struct nk_context *ctx, const char *name, struct nk_rect r, nk_flags flags)
{
    nk_window_set_bounds(ctx, name, r);
    return nk_begin(ctx, name, r, flags) ? 1 : 0;
}

static void
draw_toolbar(struct nk_context *ctx, Ide *ide)
{
    char zoom[16];
    int zn;
    nk_menubar_begin(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, (float)px(22), 11);
    nk_layout_row_push(ctx, (float)px(50));
    if (nk_menu_begin_label(ctx, "File", NK_TEXT_LEFT, nk_vec2((float)px(160), (float)px(200)))) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_menu_item_label(ctx, "New", NK_TEXT_LEFT)) ide_blank(ide);
        if (nk_menu_item_label(ctx, "Open", NK_TEXT_LEFT)) {
            ide->modal = IDE_MODAL_OPEN;
            ide_browse(ide, ide->browse);
        }
        if (nk_menu_item_label(ctx, "Save", NK_TEXT_LEFT)) {
            if (ide->path[0] == '\0') {
                ide->modal = IDE_MODAL_SAVE;
                ide_browse(ide, ide->browse);
            } else ide_save(ide);
        }
        if (nk_menu_item_label(ctx, "Quit", NK_TEXT_LEFT)) ide->modal = -1;
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, (float)px(50));
    if (nk_menu_begin_label(ctx, "Edit", NK_TEXT_LEFT, nk_vec2((float)px(160), (float)px(220)))) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_menu_item_label(ctx, "Undo", NK_TEXT_LEFT)) ide_undo(ide);
        if (nk_menu_item_label(ctx, "Redo", NK_TEXT_LEFT)) ide_redo(ide);
        if (nk_menu_item_label(ctx, "Duplicate", NK_TEXT_LEFT)) ide_duplicate(ide);
        if (nk_menu_item_label(ctx, "Delete", NK_TEXT_LEFT)) ide_delete(ide);
        if (nk_menu_item_label(ctx, "Move up", NK_TEXT_LEFT)) ide_move(ide, -1);
        if (nk_menu_item_label(ctx, "Move down", NK_TEXT_LEFT)) ide_move(ide, 1);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, (float)px(70));
    if (nk_menu_begin_label(ctx, "Layout", NK_TEXT_LEFT, nk_vec2((float)px(180), (float)px(160)))) {
        TrWidget *w = tr_doc_widget(ide->doc, ide->selected);
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (w && nk_menu_item_label(ctx, "Same row", NK_TEXT_LEFT)) w->newline = 0;
        if (w && nk_menu_item_label(ctx, "New row", NK_TEXT_LEFT)) w->newline = 1;
        if (w && nk_menu_item_label(ctx, "Stretch", NK_TEXT_LEFT)) w->stretch = w->stretch ? 0 : 1;
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, (float)px(60));
    if (nk_menu_begin_label(ctx, "View", NK_TEXT_LEFT, nk_vec2((float)px(180), (float)px(420)))) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_menu_item_label(ctx, "Design", NK_TEXT_LEFT)) ide->view = 0;
        if (nk_menu_item_label(ctx, "Code", NK_TEXT_LEFT)) ide->view = 1;
        if (nk_menu_item_label(ctx, "Explore", NK_TEXT_LEFT)) ide->view = 2;
        if (nk_menu_item_label(ctx, "Fields", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_FIELDS;
        if (nk_menu_item_label(ctx, "Configs", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_CONFIG;
        if (nk_menu_item_label(ctx, "Style", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_STYLE;
        if (nk_menu_item_label(ctx, "All configs", NK_TEXT_LEFT)) ide->show_all_cfg = !ide->show_all_cfg;
        if (nk_menu_item_label(ctx, "Spaced C", NK_TEXT_LEFT) && ide->doc)
            ide->doc->spaced = !ide->doc->spaced;
        if (nk_menu_item_symbol_label(ctx,
                ide->scheme == IDE_SCHEME_DARK ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE,
                "Dark", NK_TEXT_LEFT))
            ide_scheme_set(ide, IDE_SCHEME_DARK);
        if (nk_menu_item_symbol_label(ctx,
                ide->scheme == IDE_SCHEME_LIGHT ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE,
                "Light", NK_TEXT_LEFT))
            ide_scheme_set(ide, IDE_SCHEME_LIGHT);
        if (nk_menu_item_label(ctx, "Smaller", NK_TEXT_LEFT)) ide_ui_bump(ide, -25);
        if (nk_menu_item_label(ctx, "Larger", NK_TEXT_LEFT)) ide_ui_bump(ide, 25);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, (float)px(70));
    if (nk_menu_begin_label(ctx, "Build", NK_TEXT_LEFT, nk_vec2((float)px(180), (float)px(280)))) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_menu_item_label(ctx, "Nuklear", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_NUKLEAR);
        if (nk_menu_item_label(ctx, "libui", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_LIBUI);
        if (nk_menu_item_label(ctx, "LVGL", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_LVGL);
        if (nk_menu_item_label(ctx, "Build", NK_TEXT_LEFT)) ide_build(ide, 0);
        if (nk_menu_item_label(ctx, "Build and run", NK_TEXT_LEFT)) ide_build(ide, 1);
        if (nk_menu_item_label(ctx, "Check", NK_TEXT_LEFT)) run_check(ide);
        if (nk_menu_item_label(ctx, "Stop", NK_TEXT_LEFT)) ide_stop(ide);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, (float)px(60));
    if (nk_menu_begin_label(ctx, "Help", NK_TEXT_LEFT, nk_vec2((float)px(140), (float)px(80)))) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_menu_item_label(ctx, "About", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_ABOUT;
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, (float)px(72));
    if (nk_button_label(ctx, "Design")) ide->view = 0;
    nk_layout_row_push(ctx, (float)px(60));
    if (nk_button_label(ctx, "Code")) ide->view = 1;
    nk_layout_row_push(ctx, (float)px(28));
    if (nk_button_label(ctx, "-")) ide_ui_bump(ide, -25);
    nk_layout_row_push(ctx, (float)px(64));
    zn = snprintf(zoom, sizeof zoom, "%d%%", ide->ui_pct > 0 ? ide->ui_pct : g_pct);
    if (zn < 0 || (size_t)zn >= sizeof zoom) zoom[0] = '\0';
    nk_label(ctx, zoom, NK_TEXT_CENTERED);
    nk_layout_row_push(ctx, (float)px(28));
    if (nk_button_label(ctx, "+")) ide_ui_bump(ide, 25);
    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);
    nk_layout_row_dynamic(ctx, pxf(18), 1);
    nk_label(ctx, ide->status, NK_TEXT_LEFT);
}

static const TrKind palette[] = {
    TR_KIND_LABEL, TR_KIND_BUTTON, TR_KIND_CHECKBOX, TR_KIND_RADIO, TR_KIND_INPUT,
    TR_KIND_SLIDER, TR_KIND_COMBO, TR_KIND_PROGRESS, TR_KIND_COLOR, TR_KIND_SEPARATOR,
    TR_KIND_SPACER, TR_KIND_GROUP, TR_KIND_CHILD, TR_KIND_TABS, TR_KIND_TAB,
    TR_KIND_COLLAPSE, TR_KIND_TREE, TR_KIND_TABLE, TR_KIND_SPLITTER, TR_KIND_DOCK,
    TR_KIND_MENUBAR, TR_KIND_MENU, TR_KIND_MENUITEM, TR_KIND_IMAGE, TR_KIND_CUSTOM,
    TR_KIND_POPUP, TR_KIND_MODAL
};

static void
draw_files(struct nk_context *ctx, Ide *ide)
{
    int i;
    int save = ide->modal == IDE_MODAL_SAVE;
    nk_layout_row_dynamic(ctx, pxf(24), 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->pick, (int)sizeof ide->pick, nk_filter_default);
    nk_layout_row_dynamic(ctx, pxf(22), 1);
    for (i = 0; i < ide->nnames; i++) {
        char line[160];
        snprintf(line, sizeof line, "%s%s", ide->name_dir[i] ? "[dir] " : "", ide->names[i]);
        if (nk_button_label(ctx, line)) {
            char next[TR_PATH_MAX];
            if (ide->name_dir[i]) {
                if (strcmp(ide->names[i], "..") == 0) {
                    tr_path_dirname(ide->browse, next, sizeof next);
                } else if (snprintf(next, sizeof next, "%s/%s", ide->browse, ide->names[i]) < 0) {
                    continue;
                }
                ide_browse(ide, next);
            } else {
                tr_copy_cap(ide->pick, sizeof ide->pick, ide->names[i]);
            }
        }
    }
    nk_layout_row_dynamic(ctx, pxf(28), 2);
    if (nk_button_label(ctx, save ? "Save" : "Open")) {
        char full[TR_PATH_MAX];
        int n = snprintf(full, sizeof full, "%s/%s", ide->browse, ide->pick);
        if (n > 0 && (size_t)n < sizeof full && ide->pick[0]) {
            if (save) {
                if (tr_copy_cap(ide->path, sizeof ide->path, full) == 0) ide_save(ide);
            } else ide_open_path(ide, full);
            if (!save || ide->dirty == 0) ide->modal = IDE_MODAL_NONE;
        }
    }
    if (nk_button_label(ctx, "Close")) ide->modal = IDE_MODAL_NONE;
}

static void
draw_fields(struct nk_context *ctx, Ide *ide)
{
    int i;
    nk_layout_row_dynamic(ctx, pxf(22), 3);
    nk_label(ctx, "type", NK_TEXT_LEFT);
    nk_label(ctx, "name", NK_TEXT_LEFT);
    nk_label(ctx, "init", NK_TEXT_LEFT);
    for (i = 0; i < ide->doc->nfields; i++) {
        TrField *f = &ide->doc->fields[i];
        nk_layout_row_dynamic(ctx, pxf(22), 4);
        nk_label(ctx, f->type, NK_TEXT_LEFT);
        nk_label(ctx, f->name, NK_TEXT_LEFT);
        nk_label(ctx, f->init, NK_TEXT_LEFT);
        if (nk_button_label(ctx, "remove")) tr_field_remove(ide->doc, i);
    }
    nk_layout_row_dynamic(ctx, pxf(24), 3);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->field_type, TR_TYPE_MAX, nk_filter_default);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->field_name, TR_NAME_MAX, nk_filter_default);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->field_init, TR_TEXT_MAX, nk_filter_default);
    nk_layout_row_dynamic(ctx, pxf(26), 2);
    if (nk_button_label(ctx, "Add field")) {
        if (ide->field_type[0] == '\0') tr_copy_cap(ide->field_type, sizeof ide->field_type, "int");
        if (tr_field_add(ide->doc, ide->field_type, ide->field_name, ide->field_init) < 0)
            ide_set_status(ide, "field was rejected");
        else ide_set_status(ide, "field added");
    }
    if (nk_button_label(ctx, "Close")) ide->modal = IDE_MODAL_NONE;
}

static void
draw_configs(struct nk_context *ctx, Ide *ide)
{
    int i;
    TrDoc *doc = ide->doc;
    nk_layout_row_dynamic(ctx, pxf(22), 1);
    nk_checkbox_label(ctx, "show every configuration", &ide->show_all_cfg);
    for (i = 0; i < doc->nconfigs; i++) {
        int on = strcmp(doc->configs[i], doc->active_config) == 0;
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_select_label(ctx, doc->configs[i], NK_TEXT_LEFT, on))
            ide_set_scene(ide, doc->configs[i]);
    }
    nk_layout_row_dynamic(ctx, pxf(24), 2);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->config_name, TR_NAME_MAX, nk_filter_default);
    if (nk_button_label(ctx, "Add config") && ide->config_name[0] && doc->nconfigs < TR_CONFIGS_MAX) {
        tr_copy_cap(doc->configs[doc->nconfigs], TR_NAME_MAX, ide->config_name);
        doc->nconfigs++;
        ide->config_name[0] = '\0';
    }
    if (nk_button_label(ctx, "Close")) ide->modal = IDE_MODAL_NONE;
}

static void
draw_style(struct nk_context *ctx, Ide *ide)
{
    int i;
    char dir[TR_PATH_MAX];
    char err[128];
    nk_layout_row_dynamic(ctx, pxf(24), 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->style.name, TR_STYLE_NAME_MAX, nk_filter_default);
    for (i = 0; i < TR_STYLE_COLORS; i++) {
        int r = ide->style.color[i].r;
        int g = ide->style.color[i].g;
        int b = ide->style.color[i].b;
        nk_layout_row_dynamic(ctx, pxf(22), 4);
        nk_label(ctx, tr_style_color_name(i), NK_TEXT_LEFT);
        r = nk_propertyi(ctx, "r", 0, r, 255, 1, 1.0f);
        g = nk_propertyi(ctx, "g", 0, g, 255, 1, 1.0f);
        b = nk_propertyi(ctx, "b", 0, b, 255, 1, 1.0f);
        ide->style.color[i].r = (unsigned char)r;
        ide->style.color[i].g = (unsigned char)g;
        ide->style.color[i].b = (unsigned char)b;
        ide->style.color[i].a = 255;
    }
    nk_layout_row_dynamic(ctx, pxf(26), 2);
    if (nk_button_label(ctx, "Save style")) {
        err[0] = '\0';
        if (tr_path_join(dir, sizeof dir, ide->home, "style") == 0 &&
            tr_style_save(&ide->style, dir, err, sizeof err) == 0) {
            tr_copy_cap(ide->doc->style_name, sizeof ide->doc->style_name, ide->style.name);
            ide_set_status(ide, "style saved");
        } else ide_set_status(ide, err[0] ? err : "style save failed");
    }
    if (nk_button_label(ctx, "Close")) ide->modal = IDE_MODAL_NONE;
}

static void
refresh_preview(Ide *ide)
{
    TrBuf h;
    char err[160];
    if (!ide) return;
    if (ide->preview_fresh && ide->preview.data) return;
    err[0] = '\0';
    tr_buf_clear(&ide->preview);
    if (tr_buf_init(&h) != 0) return;
    if (tr_codegen(ide->doc, &ide->style, "app.h", &h, &ide->preview, err, sizeof err) != 0) {
        tr_buf_clear(&ide->preview);
        tr_buf_append_cstr(&ide->preview, err[0] ? err : "codegen failed");
    }
    tr_buf_free(&h);
    ide->preview_fresh = 1;
    ide->code_tied = 0;
    ide->code_tie = NULL;
    ide->preview_gen++;
}

static int
collect_routines(const char *text, char names[][48], int *lines, int max)
{
    const char *p = text ? text : "";
    int line = 0;
    int n = 0;
    while (*p && n < max && line < 100000) {
        char row[180];
        size_t k = 0;
        while (p[k] && p[k] != '\n' && k + 1 < sizeof row) {
            row[k] = p[k];
            k++;
        }
        row[k] = '\0';
        {
            size_t t = k;
            while (t > 0 && (row[t - 1] == ' ' || row[t - 1] == '\t' || row[t - 1] == '\r'))
                t--;
            if (t > 0 && row[t - 1] == ';') {
                p += k;
                if (*p == '\n') p++;
                line++;
                continue;
            }
        }
        if (ide_routine_name(row, names[n], 48)) {
            lines[n] = line;
            n++;
        }
        p += k;
        if (*p == '\n') p++;
        line++;
    }
    return n;
}

struct CodeInk {
    struct nk_color bg;
    struct nk_color hot;
    struct nk_color plain;
    struct nk_color com;
    struct nk_color strc;
    struct nk_color key;
    struct nk_color num;
    struct nk_color pre;
    struct nk_color caret;
    struct nk_color locked;
    struct nk_color sel;
};

static void
code_ink(int scheme, struct CodeInk *ink)
{
    if (scheme == IDE_SCHEME_LIGHT) {
        ink->bg = nk_rgb(252, 252, 252);
        ink->hot = nk_rgb(204, 221, 244);
        ink->plain = nk_rgb(32, 32, 32);
        ink->com = nk_rgb(0, 128, 0);
        ink->strc = nk_rgb(163, 21, 21);
        ink->key = nk_rgb(0, 0, 180);
        ink->num = nk_rgb(9, 110, 70);
        ink->pre = nk_rgb(121, 94, 160);
        ink->caret = nk_rgb(20, 20, 20);
        ink->locked = nk_rgb(130, 130, 130);
        ink->sel = nk_rgb(180, 210, 245);
        return;
    }
    ink->bg = nk_rgb(30, 30, 30);
    ink->hot = nk_rgb(48, 62, 84);
    ink->plain = nk_rgb(220, 220, 220);
    ink->com = nk_rgb(106, 153, 85);
    ink->strc = nk_rgb(206, 145, 120);
    ink->key = nk_rgb(86, 156, 214);
    ink->num = nk_rgb(181, 206, 168);
    ink->pre = nk_rgb(197, 134, 192);
    ink->caret = nk_rgb(230, 230, 230);
    ink->locked = nk_rgb(120, 120, 120);
    ink->sel = nk_rgb(40, 70, 120);
}

static void
paint_code(struct nk_context *ctx, struct nk_rect box, const char *s, int hot, int caret,
    const struct CodeInk *ink, int locked, int spaced, int sel0, int sel1)
{
    const struct nk_user_font *font = ctx->style.font;
    struct nk_command_buffer *buf = nk_window_get_canvas(ctx);
    struct nk_color bg = hot ? ink->hot : ink->bg;
    struct nk_color plain = locked ? ink->locked : ink->plain;
    struct nk_color com = locked ? ink->locked : ink->com;
    struct nk_color strc = locked ? ink->locked : ink->strc;
    struct nk_color key = locked ? ink->locked : ink->key;
    struct nk_color num = locked ? ink->locked : ink->num;
    struct nk_color pre = locked ? ink->locked : ink->pre;
    float x = box.x + 4.0f;
    float h;
    int at = 0;
    if (!font || !s) return;
    h = font->height;
    nk_fill_rect(buf, box, 0, bg);
    while (s[at] != '\0') {
        int next = at;
        int kind = ide_code_token(s, at, spaced, &next);
        struct nk_color col = plain;
        if (next <= at) next = at + 1;
        if (kind == 1) col = com;
        else if (kind == 2) col = strc;
        else if (kind == 3) col = num;
        else if (kind == 4 || kind == 5) col = key;
        else if (kind == 6) col = pre;
        while (at < next) {
            int chunk = next;
            int ins;
            int len;
            float w;
            struct nk_color tbg;
            if (sel1 > sel0) {
                if (at < sel0 && chunk > sel0) chunk = sel0;
                else if (at >= sel0 && at < sel1 && chunk > sel1) chunk = sel1;
            }
            ins = sel1 > sel0 && at >= sel0 && at < sel1;
            tbg = ins ? ink->sel : bg;
            len = chunk - at;
            w = font->width(font->userdata, h, s + at, len);
            nk_draw_text(buf, nk_rect(x, box.y, w + 2.0f, box.h), s + at, len, font, tbg, col);
            x += w;
            at = chunk;
        }
    }
    if (caret >= 0) {
        int n = caret;
        float cw;
        int sl = (int)strlen(s);
        if (n > sl) n = sl;
        if (n < 0) n = 0;
        cw = n > 0 ? font->width(font->userdata, h, s, n) : 0.0f;
        nk_fill_rect(buf, nk_rect(box.x + 4.0f + cw, box.y, 2.0f, box.h), 0, ink->caret);
    }
}

static int code_goal = -1;
static int code_hskip;
static int code_entered;
static int sel_on;
static size_t sel_anchor;
static int drag_on;
static char *clip_buf;
static size_t clip_n;

extern void nk_xlib_copy(nk_handle handle, const char *str, int len);

static size_t
line_byte(const char *text, int line)
{
    size_t i = 0;
    int n = 0;
    if (!text || line <= 0) return 0;
    while (text[i] != '\0' && n < line) {
        if (text[i] == '\n') n++;
        i++;
    }
    return i;
}

static void
seat_line(Ide *ide, const char *text, int line)
{
    size_t origin;
    size_t byte;
    size_t end;
    if (!ide || !text || line < 0) return;
    origin = line_byte(text, line);
    byte = origin;
    end = byte;
    while (text[end] != '\0' && text[end] != '\n') end++;
    while (byte < end && (text[byte] == ' ' || text[byte] == '\t')) byte++;
    ide->code_caret = line;
    ide->code_at = byte;
    code_goal = (int)(byte - origin);
    sel_on = 0;
    sel_anchor = byte;
}

static int
seek_region(const char *text, size_t n, const char *marker, size_t *at_out, int *line_out)
{
    size_t i = 0;
    int line = 0;
    size_t mlen;
    if (!text || !marker || !at_out || !line_out) return 0;
    mlen = strlen(marker);
    if (mlen == 0) return 0;
    while (i < n && line < 100000) {
        size_t eol = i;
        size_t k;
        while (eol < n && text[eol] != '\n') eol++;
        for (k = i; k + mlen <= eol; k++) {
            size_t body;
            int bl;
            if (memcmp(text + k, marker, mlen) != 0) continue;
            if (eol >= n) return 0;
            body = eol + 1;
            bl = line + 1;
            while (body < n && (text[body] == ' ' || text[body] == '\t')) body++;
            *at_out = body;
            *line_out = bl;
            return 1;
        }
        if (eol >= n) break;
        i = eol + 1;
        line++;
    }
    return 0;
}

static void
land_user(Ide *ide, const char *text, size_t n, int visible)
{
    TrWidget *w;
    char marker[96];
    size_t at = 0;
    int line = 0;
    int ev = 0;
    int top;
    int found = 0;
    if (!ide || !text) return;
    w = tr_doc_widget(ide->doc, ide->selected);
    if (w && w->nevents > 0) {
        int nw;
        if (ide->body_ev >= 0 && ide->body_ev < w->nevents) ev = ide->body_ev;
        nw = snprintf(marker, sizeof marker, "/* @tccrad user event %d %s */",
            w->id, w->events[ev].name);
        if (nw > 0 && (size_t)nw < sizeof marker)
            found = seek_region(text, n, marker, &at, &line);
    }
    if (!found) found = seek_region(text, n, "/* @tccrad user ", &at, &line);
    if (!found) return;
    ide->code_at = at;
    ide->code_caret = line;
    code_goal = 0;
    top = line - (visible > 0 ? visible / 3 : 0);
    if (top < 0) top = 0;
    ide->code_scroll = top;
}

static void
sync_rope(Ide *ide, const char *text, size_t n)
{
    int rebuild = 1;
    if (!ide || !text) return;
    if (ide->code_live && ide->code_rope) return;
    if (ide->code_tied && ide->code_rope && text == ide->code_tie) return;
    if (ide->code_rope && tr_rope_len(ide->code_rope) == n) {
        char *cur = tr_rope_flat(ide->code_rope, NULL);
        if (cur && memcmp(cur, text, n) == 0) rebuild = 0;
        free(cur);
    }
    if (!rebuild) {
        ide->code_tied = 1;
        ide->code_tie = text;
        return;
    }
    tr_rope_free(ide->code_rope);
    ide->code_rope = tr_rope_new(text, n);
    if (!ide->code_rope) {
        ide->code_tied = 0;
        ide->code_tie = NULL;
        return;
    }
    ide->code_tied = 1;
    ide->code_tie = text;
    ide->code_at = line_byte(text, ide->code_caret);
}

static size_t
rope_line_start(const struct TrRope *r, size_t at)
{
    while (at > 0 && tr_rope_at(r, at - 1) != '\n') at--;
    return at;
}

static size_t
rope_line_end(const struct TrRope *r, size_t at)
{
    size_t n = tr_rope_len(r);
    while (at < n && tr_rope_at(r, at) != '\n') at++;
    return at;
}

static int
rope_col(const struct TrRope *r, size_t at)
{
    size_t s = at;
    int col = 0;
    while (s > 0 && tr_rope_at(r, s - 1) != '\n') {
        s--;
        if (col < 100000) col++;
    }
    return col;
}

static size_t
rope_at_col(const struct TrRope *r, size_t origin, int col)
{
    size_t n = tr_rope_len(r);
    int i = 0;
    if (col < 0) col = 0;
    while (origin < n && tr_rope_at(r, origin) != '\n' && i < col) {
        origin++;
        i++;
    }
    return origin;
}

static size_t
rope_vert(const struct TrRope *r, size_t at, int dir, int col)
{
    size_t start;
    size_t end;
    size_t n;
    if (!r) return at;
    start = rope_line_start(r, at);
    n = tr_rope_len(r);
    if (dir < 0) {
        if (start == 0) return rope_at_col(r, 0, col);
        return rope_at_col(r, rope_line_start(r, start - 1), col);
    }
    end = rope_line_end(r, at);
    if (end >= n) return end;
    return rope_at_col(r, end + 1, col);
}

static int
rope_ident_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_';
}

static size_t
rope_word(const struct TrRope *r, size_t at, int dir)
{
    size_t n = tr_rope_len(r);
    int c;
    if (!r) return at;
    if (dir < 0) {
        if (at == 0) return 0;
        at--;
        while (at > 0) {
            c = tr_rope_at(r, at);
            if (c != ' ' && c != '\t') break;
            at--;
        }
        c = tr_rope_at(r, at);
        if (rope_ident_char(c)) {
            while (at > 0 && rope_ident_char(tr_rope_at(r, at - 1))) at--;
        } else if (c != '\n') {
            while (at > 0) {
                int p = tr_rope_at(r, at - 1);
                if (p == '\n' || rope_ident_char(p) || p == ' ' || p == '\t') break;
                at--;
            }
        }
        return at;
    }
    while (at < n && (tr_rope_at(r, at) == ' ' || tr_rope_at(r, at) == '\t')) at++;
    if (at >= n) return n;
    c = tr_rope_at(r, at);
    if (rope_ident_char(c)) {
        while (at < n && rope_ident_char(tr_rope_at(r, at))) at++;
    } else if (c != '\n') {
        while (at < n) {
            int q = tr_rope_at(r, at);
            if (q == '\n' || rope_ident_char(q) || q == ' ' || q == '\t') break;
            at++;
        }
    }
    return at;
}

static void
move_caret(Ide *ide, size_t at, int shift)
{
    if (!ide) return;
    if (shift) {
        if (!sel_on) sel_anchor = ide->code_at;
        sel_on = sel_anchor != at;
    } else {
        sel_on = 0;
        sel_anchor = at;
    }
    ide->code_at = at;
}

static int
sel_bounds(size_t at, size_t *lo, size_t *hi)
{
    if (!sel_on || sel_anchor == at) return 0;
    if (sel_anchor < at) {
        *lo = sel_anchor;
        *hi = at;
    } else {
        *lo = at;
        *hi = sel_anchor;
    }
    return 1;
}

static int
drop_sel(Ide *ide, const char *file)
{
    size_t lo;
    size_t hi;
    if (!ide || !ide->code_rope || !sel_bounds(ide->code_at, &lo, &hi)) return 0;
    if (!ide_span_writable(file, lo, hi)) return -1;
    if (tr_rope_delete(&ide->code_rope, lo, hi - lo) != 0) return -1;
    ide->code_at = lo;
    sel_anchor = lo;
    sel_on = 0;
    return 1;
}

static int
clip_bad(const char *s, size_t n)
{
    size_t i;
    if (!s) return 1;
    for (i = 0; i < n; i++) {
        if (s[i] == '\0') return 1;
        if (i + 10 <= n && memcmp(s + i, "/* @tccrad", 10) == 0) return 1;
        if (i + 12 <= n && memcmp(s + i, "/* @end user", 12) == 0) return 1;
    }
    return 0;
}

static int
copy_sel(Ide *ide)
{
    size_t lo;
    size_t hi;
    size_t n;
    size_t i;
    char *b;
    if (!ide || !ide->code_rope || !sel_bounds(ide->code_at, &lo, &hi)) return 0;
    n = hi - lo;
    if (n > 262144) return -1;
    b = (char *)malloc(n + 1);
    if (!b) return -1;
    for (i = 0; i < n; i++) {
        int c = tr_rope_at(ide->code_rope, lo + i);
        if (c < 0) {
            free(b);
            return -1;
        }
        b[i] = (char)c;
    }
    b[n] = '\0';
    if (clip_bad(b, n)) {
        free(b);
        return -1;
    }
    free(clip_buf);
    clip_buf = b;
    clip_n = n;
    nk_xlib_copy(nk_handle_id(0), clip_buf, (int)n);
    return 1;
}

static int
paste_clip(Ide *ide, const char *file)
{
    int dropped;
    if (!ide || !clip_buf || clip_n == 0) return 0;
    if (clip_bad(clip_buf, clip_n)) return -1;
    if (sel_on) {
        dropped = drop_sel(ide, file);
        if (dropped < 0) return -1;
    }
    if (!ide_user_writable(file, ide->code_at, 0)) return -1;
    if (tr_rope_insert(&ide->code_rope, ide->code_at, clip_buf, clip_n) != 0) return -1;
    ide->code_at += clip_n;
    sel_on = 0;
    sel_anchor = ide->code_at;
    return 1;
}

static int
indent_sel(Ide *ide, const char *file, int out)
{
    size_t lo;
    size_t hi;
    size_t starts[128];
    int cut[128];
    int ns = 0;
    int more = 0;
    int k;
    size_t scan;
    if (!ide || !file || !ide->code_rope) return 0;
    if (!sel_bounds(ide->code_at, &lo, &hi)) return 0;
    scan = lo;
    while (scan > 0 && file[scan - 1] != '\n') scan--;
    while (scan < hi && file[scan] != '\0') {
        int nsp = 0;
        if (ns >= 128) {
            more = 1;
            break;
        }
        starts[ns] = scan;
        if (out) {
            while (nsp < 4) {
                char c = file[scan + (size_t)nsp];
                if (c != ' ' && c != '\t') break;
                nsp++;
            }
            if (nsp > 0 && !ide_span_writable(file, scan, scan + (size_t)nsp)) return -1;
        } else if (!ide_span_writable(file, scan, scan)) {
            return -1;
        }
        cut[ns] = nsp;
        ns++;
        while (scan < hi && file[scan] != '\0' && file[scan] != '\n') scan++;
        if (scan < hi && file[scan] == '\n') scan++;
        else break;
    }
    if (more || ns == 0) return more ? -1 : 0;
    if (!out) {
        for (k = ns - 1; k >= 0; k--) {
            if (tr_rope_insert(&ide->code_rope, starts[k], "    ", 4) != 0) return -1;
        }
        for (k = 0; k < ns; k++) {
            if (starts[k] <= sel_anchor) sel_anchor += 4;
            if (starts[k] <= ide->code_at) ide->code_at += 4;
        }
        return 1;
    }
    {
        int any = 0;
        for (k = 0; k < ns; k++) {
            if (cut[k] > 0) any = 1;
        }
        if (!any) return -1;
        for (k = ns - 1; k >= 0; k--) {
            if (cut[k] <= 0) continue;
            if (tr_rope_delete(&ide->code_rope, starts[k], (size_t)cut[k]) != 0) return -1;
        }
        for (k = ns - 1; k >= 0; k--) {
            size_t nsp = (size_t)cut[k];
            if (nsp == 0) continue;
            if (sel_anchor > starts[k]) {
                size_t room = sel_anchor - starts[k];
                sel_anchor -= room > nsp ? nsp : room;
            }
            if (ide->code_at > starts[k]) {
                size_t room = ide->code_at - starts[k];
                ide->code_at -= room > nsp ? nsp : room;
            }
        }
        if (sel_anchor == ide->code_at) sel_on = 0;
        return 1;
    }
}

static void
select_region(Ide *ide, const char *file)
{
    const char *p;
    size_t at;
    int any = 0;
    if (!ide || !file) return;
    at = ide->code_at;
    p = file;
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
        begin = (size_t)(nl + 1 - file);
        end = (size_t)(stop - file);
        if (at >= begin && at <= end) {
            sel_anchor = begin;
            ide->code_at = end;
            sel_on = begin != end;
            return;
        }
        p = stop + 14;
    }
    if (any) return;
    sel_anchor = 0;
    ide->code_at = strlen(file);
    sel_on = ide->code_at != 0;
}

static void
edit_keys(struct nk_context *ctx, Ide *ide, int visible, const char *file)
{
    int changed = 0;
    int rejected = 0;
    int moved = 0;
    int ctrl;
    int shift;
    struct TrRope *rope;
    if (!ide->code_focus || !ide->code_rope) return;
    rope = ide->code_rope;
    ctrl = nk_input_is_key_down(&ctx->input, NK_KEY_CTRL);
    shift = nk_input_is_key_down(&ctx->input, NK_KEY_SHIFT);
    if (code_goal < 0) code_goal = rope_col(rope, ide->code_at);
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_COPY)) {
        if (copy_sel(ide) == 1) ide_set_status(ide, "copied");
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_CUT)) {
        if (copy_sel(ide) == 1 && drop_sel(ide, file) == 1) {
            changed = 1;
            ide_set_status(ide, "cut");
        } else if (sel_on) {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_PASTE)) {
        int pasted = paste_clip(ide, file);
        if (pasted == 1) changed = 1;
        else if (pasted < 0) rejected = 1;
        else ide_set_status(ide, "clipboard is empty");
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_TEXT_SELECT_ALL)) {
        select_region(ide, file);
        moved = 1;
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_BACKSPACE)) {
        int dropped = drop_sel(ide, file);
        if (dropped == 1) changed = 1;
        else if (dropped < 0) rejected = 1;
        else if (ide->code_at > 0 && ide_user_writable(file, ide->code_at - 1, 1)) {
            tr_rope_delete(&ide->code_rope, ide->code_at - 1, 1);
            ide->code_at--;
            sel_anchor = ide->code_at;
            changed = 1;
        } else if (ide->code_at > 0) {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DEL)) {
        int dropped = drop_sel(ide, file);
        if (dropped == 1) changed = 1;
        else if (dropped < 0) rejected = 1;
        else if (ide->code_at < tr_rope_len(ide->code_rope) &&
            ide_user_writable(file, ide->code_at, 1)) {
            tr_rope_delete(&ide->code_rope, ide->code_at, 1);
            changed = 1;
        } else if (ide->code_at < tr_rope_len(ide->code_rope)) {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER)) {
        if (sel_on && drop_sel(ide, file) < 0) rejected = 1;
        else if (ide_user_writable(file, ide->code_at, 0)) {
            size_t ls = rope_line_start(ide->code_rope, ide->code_at);
            char ind[48];
            int ni = 0;
            size_t i = ls;
            while (i < ide->code_at && ni < (int)sizeof ind) {
                int c = tr_rope_at(ide->code_rope, i);
                if (c != ' ' && c != '\t') break;
                ind[ni++] = (char)c;
                i++;
            }
            tr_rope_insert(&ide->code_rope, ide->code_at, "\n", 1);
            ide->code_at++;
            if (ni > 0) {
                tr_rope_insert(&ide->code_rope, ide->code_at, ind, (size_t)ni);
                ide->code_at += (size_t)ni;
            }
            changed = 1;
        } else {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_TAB)) {
        if (sel_on) {
            int ind = indent_sel(ide, file, shift);
            if (ind == 1) changed = 1;
            else rejected = 1;
        } else if (shift) {
            size_t ls = rope_line_start(ide->code_rope, ide->code_at);
            int nsp = 0;
            size_t i = ls;
            while (i < ide->code_at && nsp < 4) {
                int c = tr_rope_at(ide->code_rope, i);
                if (c != ' ' && c != '\t') break;
                nsp++;
                i++;
            }
            if (nsp > 0 && ide_user_writable(file, ls, 1)) {
                tr_rope_delete(&ide->code_rope, ls, (size_t)nsp);
                if (ide->code_at >= ls + (size_t)nsp) ide->code_at -= (size_t)nsp;
                else ide->code_at = ls;
                sel_anchor = ide->code_at;
                changed = 1;
            } else if (nsp == 0) {
                rejected = 1;
            }
        } else if (ide_user_writable(file, ide->code_at, 0)) {
            tr_rope_insert(&ide->code_rope, ide->code_at, "    ", 4);
            ide->code_at += 4;
            sel_anchor = ide->code_at;
            changed = 1;
        } else {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_TEXT_WORD_LEFT)) {
        move_caret(ide, rope_word(ide->code_rope, ide->code_at, -1), shift);
        moved = 1;
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_TEXT_WORD_RIGHT)) {
        move_caret(ide, rope_word(ide->code_rope, ide->code_at, 1), shift);
        moved = 1;
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_TEXT_START)) {
        size_t ls = rope_line_start(ide->code_rope, ide->code_at);
        size_t ns = ls;
        size_t end = rope_line_end(ide->code_rope, ide->code_at);
        while (ns < end) {
            int c = tr_rope_at(ide->code_rope, ns);
            if (c != ' ' && c != '\t') break;
            ns++;
        }
        move_caret(ide, (ide->code_at == ns) ? ls : ns, shift);
        moved = 1;
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_TEXT_END)) {
        move_caret(ide, rope_line_end(ide->code_rope, ide->code_at), shift);
        moved = 1;
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_LEFT) && !ctrl && ide->code_at > 0) {
        move_caret(ide, ide->code_at - 1, shift);
        moved = 1;
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_RIGHT) && !ctrl &&
        ide->code_at < tr_rope_len(ide->code_rope)) {
        move_caret(ide, ide->code_at + 1, shift);
        moved = 1;
    }
    if (ctx->input.keyboard.text_len > 0 && !ctrl) {
        int n = ctx->input.keyboard.text_len;
        int printable = 1;
        int t;
        int dropped = 0;
        for (t = 0; t < n; t++) {
            unsigned char c = (unsigned char)ctx->input.keyboard.text[t];
            if (c < 32 || c == 127) printable = 0;
        }
        if (printable && sel_on) {
            dropped = drop_sel(ide, file);
            if (dropped < 0) {
                rejected = 1;
                printable = 0;
            }
        }
        if (!printable) {
            /* Tab and other controls are handled as keys. */
        } else if (ide_user_writable(file, ide->code_at, 0)) {
            tr_rope_insert(&ide->code_rope, ide->code_at, ctx->input.keyboard.text, (size_t)n);
            ide->code_at += (size_t)n;
            sel_anchor = ide->code_at;
            changed = 1;
        } else {
            rejected = 1;
        }
    }
    if (rejected && !changed)
        ide_set_status(ide, "Generated lines stay fixed. Type in the handler.");
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_UP)) {
        move_caret(ide, rope_vert(ide->code_rope, ide->code_at, -1, code_goal), shift);
        moved = 1;
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN)) {
        move_caret(ide, rope_vert(ide->code_rope, ide->code_at, 1, code_goal), shift);
        moved = 1;
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_UP) ||
        nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_DOWN)) {
        int dir = nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_UP) ? -1 : 1;
        int step = visible > 1 ? visible - 1 : 1;
        int s;
        size_t at = ide->code_at;
        for (s = 0; s < step; s++)
            at = rope_vert(ide->code_rope, at, dir, code_goal);
        move_caret(ide, at, shift);
        moved = 1;
    } else if (moved || changed) {
        code_goal = rope_col(ide->code_rope, ide->code_at);
    }
    if (!changed && !moved) return;
    {
        size_t i;
        int ln = 0;
        for (i = 0; i < ide->code_at; i++) {
            if (tr_rope_at(ide->code_rope, i) == '\n') ln++;
        }
        ide->code_caret = ln;
        if (ide->code_caret < ide->code_scroll) ide->code_scroll = ide->code_caret;
        if (ide->code_caret >= ide->code_scroll + visible)
            ide->code_scroll = ide->code_caret - visible + 1;
        if (ide->code_scroll < 0) ide->code_scroll = 0;
    }
    if (changed) {
        ide->code_live = 1;
        ide->code_applied = 0;
    }
}

static int
idch(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_';
}

static int
line_extern(const char *row, char names[][48], int nrout, char *dst, size_t cap)
{
    const char *s = row;
    char defn[64];
    if (!row || !dst || cap < 2) return 0;
    dst[0] = '\0';
    if (ide_routine_name(row, defn, sizeof defn)) return 0;
    while (*s) {
        const char *b;
        size_t n;
        int k;
        if (!idch(*s)) {
            s++;
            continue;
        }
        b = s;
        while (idch(*s)) s++;
        n = (size_t)(s - b);
        if (*s != '(' || n == 0 || n + 1 > cap) continue;
        if ((n == 2 && memcmp(b, "if", 2) == 0) ||
            (n == 3 && memcmp(b, "for", 3) == 0) ||
            (n == 5 && memcmp(b, "while", 5) == 0) ||
            (n == 6 && (memcmp(b, "switch", 6) == 0 || memcmp(b, "return", 6) == 0 ||
                memcmp(b, "sizeof", 6) == 0)))
            continue;
        memcpy(dst, b, n);
        dst[n] = '\0';
        for (k = 0; k < nrout; k++) {
            if (strcmp(names[k], dst) == 0) {
                dst[0] = '\0';
                break;
            }
        }
        if (dst[0] != '\0') return 1;
    }
    return 0;
}

static char *
read_whole(const char *path)
{
    int fd;
    char *buf;
    size_t n = 0;
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return NULL;
    buf = malloc(524288);
    if (!buf) {
        close(fd);
        return NULL;
    }
    while (n + 1 < 524288) {
        ssize_t r = read(fd, buf + n, 524288 - 1 - n);
        if (r < 0) {
            free(buf);
            close(fd);
            return NULL;
        }
        if (r == 0) break;
        n += (size_t)r;
    }
    close(fd);
    buf[n] = '\0';
    return buf;
}

static int
line_count_n(const char *s, size_t n)
{
    size_t i = 0;
    int total = 0;
    if (!s || n == 0) return 1;
    while (i < n && total < 200000) {
        if (s[i] == '\n') total++;
        i++;
    }
    if (s[n - 1] != '\n') total++;
    if (total < 1) total = 1;
    return total;
}

static size_t
shown_len(const Ide *ide, const char *p, const char *owned)
{
    if (!p) return 0;
    if (ide && p == ide->code_lens) return ide->code_lens_n;
    if (ide && ide->preview.data && p == ide->preview.data) return ide->preview.len;
    if (owned && p == owned) return strlen(owned);
    return strlen(p);
}

static int
col_at_x(const struct nk_user_font *font, const char *disp, float rel)
{
    float acc = 0.0f;
    float h;
    int col = 0;
    if (!disp || rel <= 0.0f) return 0;
    h = font && font->height > 0.0f ? font->height : 16.0f;
    while (disp[col] != '\0') {
        float cw = 8.0f;
        if (font && font->width)
            cw = font->width(font->userdata, h, disp + col, 1);
        if (cw < 1.0f) cw = 1.0f;
        if (acc + cw * 0.5f >= rel) break;
        acc += cw;
        col++;
        if (col > 10000) break;
    }
    return col;
}

static void
select_word(Ide *ide, const char *text, size_t n, size_t at)
{
    size_t a;
    size_t b;
    if (!ide || !text) return;
    if (at > n) at = n;
    if (at == n || !idch(text[at])) {
        if (at > 0 && idch(text[at - 1])) at--;
        else {
            sel_on = 0;
            sel_anchor = at;
            ide->code_at = at;
            return;
        }
    }
    a = at;
    while (a > 0 && idch(text[a - 1])) a--;
    b = at;
    while (b < n && text[b] != '\n' && idch(text[b])) b++;
    sel_anchor = a;
    ide->code_at = b;
    sel_on = a != b;
}

static void
draw_code(struct nk_context *ctx, Ide *ide, int body)
{
    char names[48][48];
    int lines[48];
    int nrout;
    int current;
    int i;
    const char *p;
    const char *file;
    int line = 0;
    int row_h;
    int visible;
    char *owned = NULL;
    size_t text_n = 0;
    struct CodeInk ink;
    if (!nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) drag_on = 0;
    if (ide->code_live && ide->code_rope)
        owned = tr_rope_flat(ide->code_rope, NULL);
    else if (ide->code_ext[0] != '\0') {
        owned = read_whole(ide->code_ext);
        if (!owned) ide->code_ext[0] = '\0';
    }
    if (!owned) refresh_preview(ide);
    if (body < px(160)) body = px(160);
    row_h = px(18);
    if (row_h < 1) row_h = 1;
    visible = body / row_h;
    if (visible < 1) visible = 1;
    code_ink(ide->scheme, &ink);
    p = owned ? owned : (ide->preview.data ? ide->preview.data : "");
    text_n = shown_len(ide, p, owned);
    if (ide->code_spaced && !ide->code_live) {
        const char *shown = p;
        if (ide_code_lens(ide, p, text_n, &shown) == 0 && shown) {
            p = shown;
            text_n = shown_len(ide, p, owned);
        }
    }
    file = p;
    sync_rope(ide, p, text_n);
    if (ide->jump[0] != '\0') {
        const TrWidget *jw = tr_doc_widget(ide->doc, ide->selected);
        const char *evname = NULL;
        int at;
        int sig;
        if (jw && ide->body_ev >= 0 && ide->body_ev < jw->nevents)
            evname = jw->events[ide->body_ev].name;
        at = ide_code_jump_line(p, ide->jump, jw ? jw->id : 0, evname);
        sig = ide_def_line(p, ide->jump);
        if (at >= 0) {
            size_t byte = line_byte(p, at);
            size_t end = byte;
            ide->code_scroll = (sig >= 0 && sig <= at) ? sig : at;
            ide->code_caret = at;
            while (p[end] != '\0' && p[end] != '\n') end++;
            while (byte < end && (p[byte] == ' ' || p[byte] == '\t')) byte++;
            ide->code_at = byte;
            ide->code_hold = 1;
        } else {
            ide->code_hold = 0;
        }
        ide->jump[0] = '\0';
    }
    if (ide->code_hold) {
        size_t byte = line_byte(p, ide->code_caret);
        size_t end = byte;
        while (p[end] != '\0' && p[end] != '\n') end++;
        while (byte < end && (p[byte] == ' ' || p[byte] == '\t')) byte++;
        ide->code_at = byte;
    }
    if (!code_entered) {
        code_entered = 1;
        if (!ide->code_live && !ide_user_writable(p, ide->code_at, 0))
            land_user(ide, p, text_n, visible);
    }
    nrout = collect_routines(p, names, lines, 48);
    current = ide_routine_index(lines, nrout, ide->code_caret);
    {
        const char *items[49];
        int pick = current < 0 ? 0 : current + 1;
        int got;
        int nitems = nrout + 1;
        if (nitems > 49) nitems = 49;
        items[0] = "(declarations)";
        for (i = 0; i < nitems - 1; i++) items[i + 1] = names[i];
        nk_layout_row_begin(ctx, NK_STATIC, pxf(24), 2);
        nk_layout_row_push(ctx, pxf(64));
        {
            int on = !ide->code_spaced;
            int switched = 0;
            if (nk_selectable_label(ctx, "C", NK_TEXT_CENTERED, &on) && ide->code_spaced)
                switched = ide_code_set_spaced(ide, 0, p, text_n) == 0;
            nk_layout_row_push(ctx, pxf(96));
            on = ide->code_spaced;
            if (!switched && nk_selectable_label(ctx, "Spaced", NK_TEXT_CENTERED, &on) &&
                !ide->code_spaced)
                switched = ide_code_set_spaced(ide, 1, p, text_n) == 0;
            nk_layout_row_end(ctx);
            if (switched) {
                ide->code_hold = 0;
                free(owned);
                return;
            }
        }
        nk_layout_row_dynamic(ctx, pxf(24), 1);
        got = nk_combo(ctx, items, nitems, pick, px(22), nk_vec2((float)px(360), (float)px(280)));
        if (!ide->code_hold && got != pick) {
            int dest = 0;
            int top;
            if (got > 0 && got - 1 < nrout) dest = lines[got - 1];
            top = dest - visible / 3;
            if (top < 0) top = 0;
            seat_line(ide, p, dest);
            ide->code_scroll = top;
            current = ide_routine_index(lines, nrout, ide->code_caret);
        }
    }
    if (ide->code_ext[0] != '\0') {
        nk_layout_row_dynamic(ctx, pxf(22), 2);
        nk_label(ctx, ide->code_ext, NK_TEXT_LEFT);
        if (nk_button_label(ctx, "This program")) {
            ide->code_ext[0] = '\0';
            ide->code_scroll = 0;
            ide->code_caret = 0;
        }
    }
    nk_layout_row_dynamic(ctx, pxf(24), 2);
    ide->code_focus = 1;
    {
        nk_flags ef = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->code_find,
            (int)sizeof ide->code_find, nk_filter_default);
        if (ef & NK_EDIT_ACTIVE) ide->code_focus = 0;
    }
    if (nk_button_label(ctx, "Find next")) {
        int at = ide_find_line(p, ide->code_find, ide->code_caret + 1);
        if (at < 0) at = ide_find_line(p, ide->code_find, 0);
        if (at >= 0) {
            int top = at - visible / 3;
            if (top < 0) top = 0;
            seat_line(ide, p, at);
            ide->code_scroll = top;
        }
        else ide_set_status(ide, "not found");
    }
    {
        int total = line_count_n(p, text_n);
        if (ide->code_scroll > total - visible) ide->code_scroll = total - visible;
        if (ide->code_scroll < 0) ide->code_scroll = 0;
    }
    nk_layout_row_template_begin(ctx, (float)body);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, (float)px(240));
    nk_layout_row_template_end(ctx);
    if (nk_group_begin(ctx, "source", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_rect canvas;
        struct nk_rect area;
        if (ctx->input.mouse.scroll_delta.y != 0.0f && nk_window_is_hovered(ctx)) {
            ide->code_scroll -= (int)ctx->input.mouse.scroll_delta.y * 3;
            if (ide->code_scroll < 0) ide->code_scroll = 0;
        }
        area = nk_window_get_content_region(ctx);
        nk_layout_row_dynamic(ctx, area.h > 1.0f ? area.h : pxf(18), 1);
        if (nk_widget(&canvas, ctx) != NK_WIDGET_INVALID) {
            struct nk_command_buffer *cbuf = nk_window_get_canvas(ctx);
            const struct nk_user_font *font = ctx->style.font;
            const char *origin = file;
            float rh = pxf(18);
            float g_ext = pxf(18);
            float g_bar = pxf(96);
            float g_num = pxf(46);
            float text_x = canvas.x + g_ext + g_bar + g_num;
            float text_w;
            int vis = visible;
            int fit;
            int total_lines;
            int depth = 0;
            int shown = 0;
            int hit = 0;
            int have_first = 0;
            int have_last = 0;
            int first_line = 0;
            int last_line_no = 0;
            size_t off = 0;
            size_t first_at = 0;
            size_t last_at = 0;
            int shift = nk_input_is_key_down(&ctx->input, NK_KEY_SHIFT);
            int down = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT);
            int click = nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, canvas);
            if (rh < 1.0f) rh = 1.0f;
            fit = (int)(canvas.h / rh);
            if (fit >= 1) vis = fit;
            total_lines = line_count_n(p, text_n);
            if (drag_on && down) {
                if (ctx->input.mouse.pos.y < canvas.y && ide->code_scroll > 0)
                    ide->code_scroll--;
                else if (ctx->input.mouse.pos.y >= canvas.y + rh * (float)vis)
                    ide->code_scroll++;
            }
            if (ide->code_scroll > total_lines - vis) ide->code_scroll = total_lines - vis;
            if (ide->code_scroll < 0) ide->code_scroll = 0;
            visible = vis;
            if (text_x > canvas.x + canvas.w - 8.0f) text_x = canvas.x + canvas.w - 8.0f;
            text_w = canvas.x + canvas.w - text_x;
            if (text_w < 8.0f) text_w = 8.0f;
            nk_fill_rect(cbuf, canvas, 0, ink.bg);
            p = file;
            line = 0;
            while (off < text_n && shown < vis && line < 200000) {
                char row[240];
                char barsrc[240];
                char bars[48];
                char gutter[16];
                const char *ls = p + off;
                size_t full = 0;
                size_t take;
                size_t gk;
                size_t start;
                int skip = code_hskip;
                int was_hot;
                while (off + full < text_n && p[off + full] != '\n') full++;
                start = (size_t)(ls - origin);
                gk = full < sizeof barsrc - 1 ? full : sizeof barsrc - 1;
                if (gk > 0) memcpy(barsrc, ls, gk);
                barsrc[gk] = '\0';
                ide_scope_gutter_n(ls, full, &depth, bars, sizeof bars);
                was_hot = line == ide->code_caret;
                if (line >= ide->code_scroll && shown < vis) {
                    int hot;
                    int ext;
                    int locked;
                    int nw;
                    char call[64];
                    float y = canvas.y + (float)shown * rh;
                    int in_line;
                    struct nk_color rowbg;
                    if (was_hot) {
                        int col = 0;
                        if (ide->code_at >= start) {
                            size_t delta = ide->code_at - start;
                            col = delta > 1000000u ? 1000000 : (int)delta;
                        }
                        if (col < code_hskip) code_hskip = col;
                        if (col > code_hskip + 72) code_hskip = col - 48;
                        if (code_hskip < 0) code_hskip = 0;
                        skip = code_hskip;
                    }
                    if (skip < 0) skip = 0;
                    if ((size_t)skip > full)
                        skip = full > 1000000u ? 1000000 : (int)full;
                    take = full - (size_t)skip;
                    if (take > sizeof row - 1) take = sizeof row - 1;
                    if (take > 0) memcpy(row, ls + skip, take);
                    row[take] = '\0';
                    ext = line_extern(barsrc, names, nrout, call, sizeof call);
                    in_line = ctx->input.mouse.pos.y >= y &&
                        ctx->input.mouse.pos.y < y + rh &&
                        ctx->input.mouse.pos.x >= canvas.x &&
                        ctx->input.mouse.pos.x < canvas.x + canvas.w;
                    if (!ide->code_hold && click && in_line &&
                        ctx->input.mouse.pos.x < canvas.x + g_ext) {
                        ide->code_focus = 1;
                        if (ext && double_click(ide, 300000 + line)) {
                            char found[TR_PATH_MAX];
                            int ln = 0;
                            if (ide_find_def(ide->home, ide->path, call, found, sizeof found,
                                    &ln)) {
                                tr_copy_cap(ide->code_ext, sizeof ide->code_ext, found);
                                seat_line(ide, p, ln);
                                ide->code_scroll = ln > visible / 3 ? ln - visible / 3 : 0;
                                ide_set_status(ide, found);
                            } else {
                                ide_set_status(ide, "definition not in the project");
                            }
                        } else {
                            move_caret(ide, start, 0);
                            ide->code_caret = line;
                            drag_on = 1;
                            if (ide->code_rope)
                                code_goal = rope_col(ide->code_rope, ide->code_at);
                        }
                        hit = 1;
                    } else if (!ide->code_hold && click && in_line &&
                        ctx->input.mouse.pos.x >= text_x) {
                        int col = col_at_x(font, row, ctx->input.mouse.pos.x - (text_x + 4.0f));
                        size_t at = start + (size_t)skip + (size_t)col;
                        ide->code_focus = 1;
                        ide->code_caret = line;
                        move_caret(ide, at, shift);
                        drag_on = 1;
                        if (!shift && double_click(ide, 410000 + line))
                            select_word(ide, origin, text_n, ide->code_at);
                        if (ide->code_rope)
                            code_goal = rope_col(ide->code_rope, ide->code_at);
                        hit = 1;
                    } else if (!ide->code_hold && click && in_line) {
                        ide->code_focus = 1;
                        ide->code_caret = line;
                        if (shift) move_caret(ide, start + full, 1);
                        else {
                            sel_anchor = start;
                            ide->code_at = start + full;
                            sel_on = full > 0;
                        }
                        drag_on = 1;
                        if (ide->code_rope)
                            code_goal = rope_col(ide->code_rope, ide->code_at);
                        hit = 1;
                    } else if (!ide->code_hold && drag_on && down && in_line) {
                        size_t at;
                        if (ctx->input.mouse.pos.x < text_x) at = start;
                        else {
                            int col = col_at_x(font, row,
                                ctx->input.mouse.pos.x - (text_x + 4.0f));
                            at = start + (size_t)skip + (size_t)col;
                        }
                        ide->code_caret = line;
                        ide->code_at = at;
                        sel_on = sel_anchor != ide->code_at;
                        if (ide->code_rope)
                            code_goal = rope_col(ide->code_rope, ide->code_at);
                        hit = 1;
                    }
                    hot = line == ide->code_caret;
                    locked = !ide_user_writable(origin, start, 0);
                    rowbg = hot ? ink.hot : ink.bg;
                    nk_fill_rect(cbuf, nk_rect(canvas.x, y, canvas.w, rh), 0, rowbg);
                    if (font && ext) {
                        nk_draw_text(cbuf, nk_rect(canvas.x, y, g_ext, rh), ">", 1, font,
                            rowbg, ink.key);
                    }
                    if (font && bars[0] != '\0') {
                        int blen = (int)strlen(bars);
                        nk_draw_text(cbuf, nk_rect(canvas.x + g_ext, y, g_bar, rh), bars, blen,
                            font, rowbg, ink.plain);
                    }
                    nw = snprintf(gutter, sizeof gutter, "%4d", line + 1);
                    if (nw < 0) gutter[0] = '\0';
                    if (font && gutter[0] != '\0') {
                        int glen = (int)strlen(gutter);
                        float tw = font->width ?
                            font->width(font->userdata, font->height, gutter, glen) :
                            (float)glen * 8.0f;
                        float tx = canvas.x + g_ext + g_bar + g_num - tw - 2.0f;
                        struct nk_color numcol = locked ? ink.locked : ink.plain;
                        float num_x = canvas.x + g_ext + g_bar;
                        if (tx < num_x) tx = num_x;
                        nk_draw_text(cbuf, nk_rect(tx, y, tw + 2.0f, rh), gutter, glen, font,
                            rowbg, numcol);
                    }
                    {
                        int col = -1;
                        int sel0 = -1;
                        int sel1 = -1;
                        struct nk_rect tbox = nk_rect(text_x, y, text_w, rh);
                        if (hot && ide->code_at >= start + (size_t)skip) {
                            size_t delta = ide->code_at - (start + (size_t)skip);
                            col = delta > 1000000u ? 1000000 : (int)delta;
                        }
                        if (sel_on) {
                            size_t lo;
                            size_t hi;
                            if (sel_bounds(ide->code_at, &lo, &hi) &&
                                start < hi && start + full > lo) {
                                int a = 0;
                                int b = 0;
                                int span = full > 1000000u ? 1000000 : (int)full;
                                if (lo > start) {
                                    size_t d = lo - start;
                                    a = d > 1000000u ? 1000000 : (int)d;
                                }
                                if (hi >= start + full) b = span;
                                else if (hi > start) {
                                    size_t d = hi - start;
                                    b = d > 1000000u ? 1000000 : (int)d;
                                }
                                a -= skip;
                                b -= skip;
                                if (a < 0) a = 0;
                                if (b > (int)take) b = (int)take;
                                if (b > a) {
                                    sel0 = a;
                                    sel1 = b;
                                }
                            }
                        }
                        paint_code(ctx, tbox, row, hot, col, &ink, locked, ide->code_spaced,
                            sel0, sel1);
                    }
                    if (shown == 0) {
                        have_first = 1;
                        first_line = line;
                        first_at = start;
                    }
                    have_last = 1;
                    last_line_no = line;
                    last_at = start + full;
                    shown++;
                }
                off += full;
                if (off < text_n && p[off] == '\n') off++;
                line++;
            }
            if (!hit && !ide->code_hold && click &&
                ctx->input.mouse.pos.x >= canvas.x &&
                ctx->input.mouse.pos.x < canvas.x + canvas.w &&
                ctx->input.mouse.pos.y >= canvas.y && off >= text_n) {
                int el = line;
                if (text_n > 0 && p[text_n - 1] != '\n' && el > 0) el--;
                ide->code_focus = 1;
                ide->code_caret = el;
                move_caret(ide, text_n, shift);
                drag_on = 1;
                if (ide->code_rope) code_goal = rope_col(ide->code_rope, ide->code_at);
            } else if (!hit && !ide->code_hold && drag_on && down && have_first &&
                ctx->input.mouse.pos.x >= canvas.x &&
                ctx->input.mouse.pos.x < canvas.x + canvas.w &&
                ctx->input.mouse.pos.y < canvas.y) {
                ide->code_caret = first_line;
                ide->code_at = first_at;
                sel_on = sel_anchor != ide->code_at;
                if (ide->code_rope) code_goal = rope_col(ide->code_rope, ide->code_at);
            } else if (!hit && !ide->code_hold && drag_on && down && have_last &&
                ctx->input.mouse.pos.x >= canvas.x &&
                ctx->input.mouse.pos.x < canvas.x + canvas.w &&
                ctx->input.mouse.pos.y >= canvas.y + rh * (float)shown) {
                ide->code_caret = last_line_no;
                ide->code_at = off >= text_n ? text_n : last_at;
                if (off >= text_n && text_n > 0 && p[text_n - 1] != '\n' && line > 0)
                    ide->code_caret = line - 1;
                else if (off >= text_n)
                    ide->code_caret = line;
                sel_on = sel_anchor != ide->code_at;
                if (ide->code_rope) code_goal = rope_col(ide->code_rope, ide->code_at);
            }
        }
        nk_group_end(ctx);
    }
    if (nk_group_begin_titled(ctx, "code-info", "Scope", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        draw_code_info(ctx, ide, file, text_n, ide->code_caret, visible, names, lines, nrout);
        nk_group_end(ctx);
    }
    {
        int total = line_count_n(file, text_n);
        int max_scroll;
        max_scroll = total - visible;
        if (max_scroll < 0) max_scroll = 0;
        if (ide->code_scroll > max_scroll) ide->code_scroll = max_scroll;
        nk_slider_int(ctx, 0, &ide->code_scroll, max_scroll, 1);
    }
    nk_layout_row_dynamic(ctx, pxf(24), 3);
    if (nk_button_label(ctx, "Up") && ide->code_caret > 0) {
        seat_line(ide, file, ide->code_caret - 1);
        if (ide->code_scroll > ide->code_caret) ide->code_scroll = ide->code_caret;
    }
    if (nk_button_label(ctx, "Down")) {
        seat_line(ide, file, ide->code_caret + 1);
        if (ide->code_caret >= ide->code_scroll + visible)
            ide->code_scroll = ide->code_caret - visible + 1;
    }
    if (nk_button_label(ctx, "Design")) {
        ide->view = 0;
        ide->modal = IDE_MODAL_NONE;
    }
    if (ide->code_focus) {
        nk_flags find = 0;
        (void)find;
        edit_keys(ctx, ide, visible, file);
    }
    ide->code_hold = 0;
    free(owned);
}

enum { EX_FORM = 1, EX_WIDGET, EX_PROC, EX_FIND };

static const char *
sev_name(int severity)
{
    if (severity >= 3) return "high";
    if (severity == 2) return "medium";
    return "low";
}

static const char *
preview_view(Ide *ide, char **owned)
{
    const char *raw = "";
    int b = TR_BACKEND_NUKLEAR;
    *owned = NULL;
    if (!ide) return raw;
    if (ide->preview.data) raw = ide->preview.data;
    if (ide->doc) b = (int)ide->doc->backend;
    *owned = tr_backend_view(raw, b);
    if (*owned) return *owned;
    return raw;
}

static void
run_check(Ide *ide)
{
    char msg[48];
    char *owned = NULL;
    const char *text;
    int n;
    if (!ide) return;
    refresh_preview(ide);
    text = preview_view(ide, &owned);
    n = tr_check(text, ide->findings, TR_FIND_MAX);
    free(owned);
    if (n < 0) n = 0;
    ide->nfindings = n;
    ide->checked = 1;
    ide->explore_hold = 1;
    ide->explore_filter[0] = '\0';
    ide->view = 2;
    ide->explore_sel = 0;
    ide->modal = IDE_MODAL_NONE;
    snprintf(msg, sizeof msg, "%d findings", n);
    ide_set_status(ide, msg);
}

static void
open_explore(Ide *ide, int kind, int id, int line)
{
    if (kind == EX_PROC || kind == EX_FIND) {
        int at = line < 0 ? 0 : line;
        ide->code_ext[0] = '\0';
        tr_rope_free(ide->code_rope);
        ide->code_rope = NULL;
        ide->code_live = 0;
        ide->code_caret = at;
        ide->code_scroll = at > 2 ? at - 2 : 0;
        ide->jump[0] = '\0';
        ide->code_hold = 1;
        ide->code_focus = 1;
        ide->view = 1;
        ide->modal = IDE_MODAL_NONE;
        return;
    }
    if (id > 0) ide_select(ide, id);
    ide->view = 0;
    ide->modal = IDE_MODAL_NONE;
}

enum { EX_ROWS = 240 };

static void
order_procs(const TrExplore *procs, int *order, int n)
{
    int i;
    for (i = 0; i < n; i++) order[i] = i;
    for (i = 1; i < n; i++) {
        int key = order[i];
        int j = i;
        while (j > 0) {
            const TrExplore *a = &procs[order[j - 1]];
            const TrExplore *b = &procs[key];
            int before = b->user > a->user;
            if (!before && b->user == a->user && b->complexity > a->complexity) before = 1;
            if (!before && b->user == a->user && b->complexity == a->complexity &&
                b->line < a->line)
                before = 1;
            if (!before) break;
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
}

static TrExplore ex_procs[TR_EXPLORE_MAX];
static int ex_nproc = -1;
static unsigned ex_gen;
static int ex_backend = -1;

static int
shown_routine(const char *text, size_t n, const char *name)
{
    size_t i = 0;
    int line = 0;
    char got[48];
    if (!text || !name || name[0] == '\0') return -1;
    while (i < n && line < 100000) {
        char row[180];
        size_t k = 0;
        while (i + k < n && text[i + k] != '\n' && k + 1 < sizeof row) {
            row[k] = text[i + k];
            k++;
        }
        row[k] = '\0';
        if (ide_routine_name(row, got, sizeof got) && strcmp(got, name) == 0) return line;
        i += k;
        if (i < n && text[i] == '\n') i++;
        line++;
    }
    return -1;
}

static void
code_visit(Ide *ide, const char *text, size_t n, int line, int visible)
{
    size_t off = 0;
    int i = 0;
    int top;
    if (!ide || !text || line < 0) return;
    while (i < line && off < n) {
        if (text[off] == '\n') i++;
        off++;
    }
    ide->code_caret = line;
    top = line - visible / 3;
    if (top < 0) top = 0;
    ide->code_scroll = top;
    ide->code_at = off;
    ide->code_focus = 1;
}

static int
code_kw(const char *s)
{
    static const char *const w[] = {
        "if", "for", "while", "switch", "return", "sizeof", "else", "case",
        "break", "continue", "do", "goto", "int", "char", "void", "const",
        "static", "struct", "unsigned", "long", "short", "float", "double",
        "signed", "typedef", "enum", "union", "extern", NULL
    };
    int i;
    for (i = 0; w[i]; i++) {
        if (strcmp(s, w[i]) == 0) return 1;
    }
    return 0;
}

static char use_name[48];
static const char *use_text;
static size_t use_n;
static unsigned use_gen;
static int use_lines[32];
static int use_count = -1;

static int
uses_cached(const Ide *ide, const char *text, size_t n, const char *name)
{
    unsigned gen = ide ? ide->preview_gen : 0;
    if (use_count >= 0 && use_text == text && use_n == n && use_gen == gen &&
        strcmp(use_name, name) == 0)
        return use_count;
    use_count = ide_ident_uses(text, n, name, use_lines, 32);
    tr_copy_cap(use_name, sizeof use_name, name);
    use_text = text;
    use_n = n;
    use_gen = gen;
    return use_count;
}

static int
use_step(int caret, int dir)
{
    int i;
    if (use_count < 1) return -1;
    if (dir > 0) {
        for (i = 0; i < use_count; i++) {
            if (use_lines[i] > caret) return use_lines[i];
        }
        return use_lines[0];
    }
    for (i = use_count - 1; i >= 0; i--) {
        if (use_lines[i] < caret) return use_lines[i];
    }
    return use_lines[use_count - 1];
}

static void
draw_code_info(struct nk_context *ctx, Ide *ide, const char *text, size_t n,
    int caret, int visible, char names[][48], const int *rlines, int nrout)
{
    int ri;
    int pi = -1;
    int nproc;
    int i;
    int host;
    int nuse = 0;
    char ident[48];
    const TrExplore *fn = NULL;
    if (!ide || !text) return;
    host = !ide->code_spaced && !ide->code_live;
    ri = ide_routine_index(rlines, nrout, caret);
    nproc = explore_procs(ide);
    if (ri >= 0) {
        for (i = 0; i < nproc; i++) {
            if (strcmp(ex_procs[i].name, names[ri]) == 0) {
                pi = i;
                break;
            }
        }
    }
    if (pi >= 0) fn = &ex_procs[pi];
    nk_layout_row_dynamic(ctx, pxf(18), 1);
    nk_label(ctx, ri >= 0 ? names[ri] : "(declarations)", NK_TEXT_LEFT);
    if (fn) {
        char met[80];
        snprintf(met, sizeof met, "c%d  in%d out%d  %d lines",
            fn->complexity, fn->fan_in, fn->fan_out, fn->lines);
        nk_layout_row_dynamic(ctx, pxf(18), 1);
        nk_label(ctx, met, NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, pxf(18), 1);
        nk_label(ctx, "Callers", NK_TEXT_LEFT);
        if (fn->ncallers < 1) {
            nk_layout_row_dynamic(ctx, pxf(18), 1);
            nk_label(ctx, "none", NK_TEXT_LEFT);
        }
        for (i = 0; i < fn->ncallers && i < 6; i++) {
            int id = fn->callers[i];
            char lab[64];
            const char *nm;
            if (id < 0 || id >= nproc) continue;
            nm = ex_procs[id].name;
            snprintf(lab, sizeof lab, "in  %s", nm);
            nk_layout_row_dynamic(ctx, pxf(20), 1);
            if (nk_button_label(ctx, lab)) {
                int dest = host ? fn->caller_line[i] : shown_routine(text, n, nm);
                code_visit(ide, text, n, dest, visible);
            }
        }
        nk_layout_row_dynamic(ctx, pxf(18), 1);
        nk_label(ctx, "Callees", NK_TEXT_LEFT);
        if (fn->ncallees < 1) {
            nk_layout_row_dynamic(ctx, pxf(18), 1);
            nk_label(ctx, "none", NK_TEXT_LEFT);
        }
        for (i = 0; i < fn->ncallees && i < 6; i++) {
            int id = fn->callees[i];
            char lab[64];
            const char *nm;
            int dest;
            if (id < 0 || id >= nproc) continue;
            nm = ex_procs[id].name;
            snprintf(lab, sizeof lab, "out  %s", nm);
            nk_layout_row_dynamic(ctx, pxf(20), 1);
            if (!nk_button_label(ctx, lab)) continue;
            dest = shown_routine(text, n, nm);
            if (dest < 0 && host) dest = ex_procs[id].line;
            code_visit(ide, text, n, dest, visible);
        }
        if (host && fn->ndecide > 0) {
            int shown_d = fn->ndecide < 4 ? fn->ndecide : 4;
            nk_layout_row_dynamic(ctx, pxf(18), 1);
            nk_label(ctx, "Decisions", NK_TEXT_LEFT);
            for (i = 0; i < shown_d; i++) {
                char lab[48];
                snprintf(lab, sizeof lab, "%s  L%d", fn->decide[i], fn->decide_line[i] + 1);
                nk_layout_row_dynamic(ctx, pxf(20), 1);
                if (nk_button_label(ctx, lab))
                    code_visit(ide, text, n, fn->decide_line[i], visible);
            }
        }
    }
    ident[0] = '\0';
    if (ide->code_at > n || ide_ident_at(text, n, ide->code_at, ident, sizeof ident) != 1)
        ident[0] = '\0';
    nk_layout_row_dynamic(ctx, pxf(18), 1);
    nk_label(ctx, "Name", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, pxf(18), 1);
    if (ident[0] == '\0') {
        nk_label(ctx, "(none)", NK_TEXT_LEFT);
        return;
    }
    if (code_kw(ident)) {
        char lab[64];
        snprintf(lab, sizeof lab, "keyword  %s", ident);
        nk_label(ctx, lab, NK_TEXT_LEFT);
        return;
    }
    nk_label(ctx, ident, NK_TEXT_LEFT);
    nuse = uses_cached(ide, text, n, ident);
    nk_layout_row_dynamic(ctx, pxf(22), 2);
    if (nk_button_label(ctx, "Prev") && nuse > 0)
        code_visit(ide, text, n, use_step(caret, -1), visible);
    if (nk_button_label(ctx, "Next") && nuse > 0)
        code_visit(ide, text, n, use_step(caret, 1), visible);
    for (i = 0; i < nuse && i < 8; i++) {
        char lab[48];
        char row[72];
        if (tr_line_text(text, use_lines[i], row, sizeof row) != 0) row[0] = '\0';
        snprintf(lab, sizeof lab, "L%d  %.24s", use_lines[i] + 1, row[0] ? row : ident);
        nk_layout_row_dynamic(ctx, pxf(20), 1);
        if (nk_button_label(ctx, lab))
            code_visit(ide, text, n, use_lines[i], visible);
    }
}

static int
explore_procs(Ide *ide)
{
    int b;
    char *owned = NULL;
    const char *text;
    int n;
    if (!ide) return 0;
    refresh_preview(ide);
    b = ide->doc ? (int)ide->doc->backend : (int)TR_BACKEND_NUKLEAR;
    if (ex_nproc >= 0 && ex_gen == ide->preview_gen && ex_backend == b) return ex_nproc;
    text = preview_view(ide, &owned);
    n = tr_explore(text, ex_procs, TR_EXPLORE_MAX);
    free(owned);
    if (n < 0) n = 0;
    ex_nproc = n;
    ex_gen = ide->preview_gen;
    ex_backend = b;
    return n;
}

static void
draw_explore(struct nk_context *ctx, Ide *ide, int body)
{
    const TrExplore *procs;
    int order[TR_EXPLORE_MAX];
    int nproc;
    int row_of[TR_EXPLORE_MAX];
    int ids[EX_ROWS];
    int kinds[EX_ROWS];
    int lns[EX_ROWS];
    char labels[EX_ROWS][96];
    int n = 0;
    int i;
    int sev;
    int list_h;
    int chrome;
    const char *filt = ide->explore_filter;
    const TrWidget *root;
    nproc = explore_procs(ide);
    procs = ex_procs;
    order_procs(procs, order, nproc);
    for (i = 0; i < TR_EXPLORE_MAX; i++) row_of[i] = -1;
    for (sev = 3; sev >= 1 && n < EX_ROWS; sev--) {
        for (i = 0; i < ide->nfindings && n < EX_ROWS; i++) {
            char label[96];
            const TrFinding *f = &ide->findings[i];
            int low = f->severity < 2;
            if (sev > 1 && f->severity != sev) continue;
            if (sev == 1 && !low) continue;
            if (tr_find_row(f, ide->preview.data, label, sizeof label) != 0) continue;
            if (!ide_text_has(label, filt)) continue;
            snprintf(labels[n], sizeof labels[n], "%s", label);
            kinds[n] = EX_FIND;
            ids[n] = i;
            lns[n] = f->line;
            n++;
        }
    }
    for (i = 0; i < nproc && n < EX_ROWS; i++) {
        char label[96];
        int pi = order[i];
        snprintf(label, sizeof label, "%s  %s  c%d  in%d out%d",
            procs[pi].user ? "hand" : "proc", procs[pi].name,
            procs[pi].complexity, procs[pi].fan_in, procs[pi].fan_out);
        if (!ide_text_has(label, filt)) continue;
        snprintf(labels[n], sizeof labels[n], "%s", label);
        kinds[n] = EX_PROC;
        ids[n] = pi;
        lns[n] = procs[pi].line;
        row_of[pi] = n;
        n++;
    }
    root = ide->doc && ide->doc->nwidgets > 0 ? &ide->doc->widgets[0] : NULL;
    if (root && n < EX_ROWS) {
        char label[96];
        snprintf(label, sizeof label, "form  %s", root->name);
        if (ide_text_has(label, filt)) {
            snprintf(labels[n], sizeof labels[n], "%s", label);
            kinds[n] = EX_FORM;
            ids[n] = root->id;
            lns[n] = 0;
            n++;
        }
    }
    if (ide->doc) {
        for (i = 1; i < ide->doc->nwidgets && n < EX_ROWS; i++) {
            const TrWidget *w = &ide->doc->widgets[i];
            char label[96];
            snprintf(label, sizeof label, "%s  %s", tr_kind_name(w->kind), w->name);
            if (!ide_text_has(label, filt)) continue;
            snprintf(labels[n], sizeof labels[n], "%s", label);
            kinds[n] = EX_WIDGET;
            ids[n] = w->id;
            lns[n] = 0;
            n++;
        }
    }
    if (ide->explore_sel >= n) ide->explore_sel = n > 0 ? 0 : -1;
    nk_layout_row_dynamic(ctx, pxf(24), 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->explore_filter,
        (int)sizeof ide->explore_filter, nk_filter_default);
    if (ide->checked) {
        char banner[64];
        snprintf(banner, sizeof banner, "Report  %d findings", ide->nfindings);
        nk_layout_row_dynamic(ctx, pxf(18), 1);
        nk_label(ctx, banner, NK_TEXT_LEFT);
    }
    chrome = px(24) + px(26) + px(12);
    if (ide->checked) chrome += px(18);
    list_h = body - chrome;
    if (list_h < px(160)) list_h = px(160);
    nk_layout_row_dynamic(ctx, (float)list_h, 2);
    if (nk_group_begin(ctx, "explore-index", NK_WINDOW_BORDER)) {
        if (n == 0) {
            nk_layout_row_dynamic(ctx, pxf(20), 1);
            nk_label(ctx, "Nothing matches.", NK_TEXT_LEFT);
        }
        for (i = 0; i < n; i++) {
            int on = i == ide->explore_sel;
            int before = on;
            nk_layout_row_dynamic(ctx, pxf(20), 1);
            if (nk_selectable_label(ctx, labels[i], NK_TEXT_LEFT, &on)) {
                if (before && !ide->explore_hold) open_explore(ide, kinds[i], ids[i], lns[i]);
                else ide->explore_sel = i;
            }
        }
        nk_group_end(ctx);
    }
    if (nk_group_begin(ctx, "explore-detail", NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, pxf(18), 1);
        if (ide->explore_sel < 0 || ide->explore_sel >= n) {
            nk_label(ctx, "Select a routine, a finding, or a widget.", NK_TEXT_LEFT);
        } else if (kinds[ide->explore_sel] == EX_FIND) {
            const TrFinding *f = &ide->findings[ids[ide->explore_sel]];
            char line[120];
            nk_label(ctx, f->checker, NK_TEXT_LEFT);
            snprintf(line, sizeof line, "%s  line %d", sev_name(f->severity), f->line + 1);
            nk_label(ctx, line, NK_TEXT_LEFT);
            nk_label(ctx, f->message, NK_TEXT_LEFT);
            if (tr_line_text(ide->preview.data, f->line, line, sizeof line) == 0 && line[0] != '\0')
                nk_label(ctx, line, NK_TEXT_LEFT);
            nk_label(ctx, "Click again to open this line.", NK_TEXT_LEFT);
        } else if (kinds[ide->explore_sel] == EX_PROC) {
            const TrExplore *e = &procs[ids[ide->explore_sel]];
            char line[120];
            int c;
            snprintf(line, sizeof line, "%d lines    complexity %d    depth %d",
                e->lines, e->complexity, e->depth);
            nk_label(ctx, e->name, NK_TEXT_LEFT);
            nk_label(ctx, line, NK_TEXT_LEFT);
            snprintf(line, sizeof line, "%d in    %d out    line %d",
                e->fan_in, e->fan_out, e->line + 1);
            nk_label(ctx, line, NK_TEXT_LEFT);
            for (c = 0; c < e->ndecide; c++) {
                int on = 0;
                int pad = e->decide_depth[c];
                if (pad < 0) pad = 0;
                if (pad > 6) pad = 6;
                snprintf(line, sizeof line, "%*s%s  %d", pad * 2, "", e->decide[c],
                    e->decide_line[c] + 1);
                nk_layout_row_dynamic(ctx, pxf(20), 1);
                if (nk_selectable_label(ctx, line, NK_TEXT_LEFT, &on) && !ide->explore_hold)
                    open_explore(ide, EX_PROC, ids[ide->explore_sel], e->decide_line[c]);
            }
            if (e->ndecide_all > e->ndecide) {
                snprintf(line, sizeof line, "%d more decisions", e->ndecide_all - e->ndecide);
                nk_layout_row_dynamic(ctx, pxf(18), 1);
                nk_label(ctx, line, NK_TEXT_LEFT);
            }
            for (c = 0; c < e->ncallers; c++) {
                int who = e->callers[c];
                int on = 0;
                if (who < 0 || who >= nproc) continue;
                snprintf(line, sizeof line, "called by %s  %d", procs[who].name,
                    e->caller_line[c] + 1);
                nk_layout_row_dynamic(ctx, pxf(20), 1);
                if (nk_selectable_label(ctx, line, NK_TEXT_LEFT, &on) && row_of[who] >= 0)
                    ide->explore_sel = row_of[who];
            }
            for (c = 0; c < e->ncallees; c++) {
                int who = e->callees[c];
                int on = 0;
                if (who < 0 || who >= nproc) continue;
                snprintf(line, sizeof line, "calls %s  %d", procs[who].name,
                    e->callee_line[c] + 1);
                nk_layout_row_dynamic(ctx, pxf(20), 1);
                if (nk_selectable_label(ctx, line, NK_TEXT_LEFT, &on) && row_of[who] >= 0)
                    ide->explore_sel = row_of[who];
            }
            nk_label(ctx, "Click again to open this line.", NK_TEXT_LEFT);
        } else {
            const TrWidget *w = tr_doc_widget(ide->doc, ids[ide->explore_sel]);
            char line[120];
            int e;
            if (!w) {
                nk_label(ctx, "That widget is gone.", NK_TEXT_LEFT);
            } else {
                const TrWidget *parent = w->parent > 0 ? tr_doc_widget(ide->doc, w->parent) : NULL;
                nk_label(ctx, w->name, NK_TEXT_LEFT);
                snprintf(line, sizeof line, "%s", tr_kind_name(w->kind));
                nk_label(ctx, line, NK_TEXT_LEFT);
                if (parent) {
                    snprintf(line, sizeof line, "parent %s", parent->name);
                    nk_label(ctx, line, NK_TEXT_LEFT);
                }
                for (e = 0; e < w->nevents; e++) {
                    if (w->events[e].handler[0] == '\0') continue;
                    snprintf(line, sizeof line, "%s = %s", w->events[e].name, w->events[e].handler);
                    nk_label(ctx, line, NK_TEXT_LEFT);
                }
                nk_label(ctx, "Click again to show it on the form.", NK_TEXT_LEFT);
            }
        }
        nk_group_end(ctx);
    }
    nk_layout_row_dynamic(ctx, pxf(26), 2);
    if (nk_button_label(ctx, "Open") && ide->explore_sel >= 0 && ide->explore_sel < n)
        open_explore(ide, kinds[ide->explore_sel], ids[ide->explore_sel], lns[ide->explore_sel]);
    if (nk_button_label(ctx, "Design")) {
        ide->view = 0;
        ide->modal = IDE_MODAL_NONE;
    }
    ide->explore_hold = 0;
}

static void
draw_about(struct nk_context *ctx, Ide *ide)
{
    nk_layout_row_dynamic(ctx, pxf(18), 1);
    nk_label(ctx, "TCCRAD 0.1.0-alpha.1", NK_TEXT_LEFT);
    nk_label(ctx, "A C RAD studio for Nuklear, libui, and LVGL.", NK_TEXT_LEFT);
    nk_label(ctx, "Designs round-trip through generated C. TinyCC builds and runs them.", NK_TEXT_LEFT);
    nk_label(ctx, "Bounded copies, checked allocation, posix_spawn. No system().", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, pxf(26), 1);
    if (nk_button_label(ctx, "Close")) ide->modal = IDE_MODAL_NONE;
}

static void
draw_modal(struct nk_context *ctx, Ide *ide, int w, int h)
{
    struct nk_rect r;
    const char *title = "Dialog";
    if (ide->modal == IDE_MODAL_NONE || ide->modal < 0) return;
    r = nk_rect((float)px(80), (float)px(60), (float)w - (float)px(160), (float)h - (float)px(120));
    if (ide->modal == IDE_MODAL_OPEN) title = "Open";
    if (ide->modal == IDE_MODAL_SAVE) title = "Save";
    if (ide->modal == IDE_MODAL_CODE) title = "Code";
    if (ide->modal == IDE_MODAL_EXPLORE) title = "Explore";
    if (ide->modal == IDE_MODAL_FIELDS) title = "Struct wizard";
    if (ide->modal == IDE_MODAL_CONFIG) title = "Configurations";
    if (ide->modal == IDE_MODAL_STYLE) title = "Style";
    if (ide->modal == IDE_MODAL_ABOUT) title = "About";
    nk_window_set_bounds(ctx, title, r);
    if (!nk_begin(ctx, title, r, NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        nk_end(ctx);
        return;
    }
    if (ide->modal == IDE_MODAL_OPEN || ide->modal == IDE_MODAL_SAVE) draw_files(ctx, ide);
    else if (ide->modal == IDE_MODAL_CODE) draw_code(ctx, ide, px(320));
    else if (ide->modal == IDE_MODAL_EXPLORE) draw_explore(ctx, ide, px(320));
    else if (ide->modal == IDE_MODAL_FIELDS) draw_fields(ctx, ide);
    else if (ide->modal == IDE_MODAL_CONFIG) draw_configs(ctx, ide);
    else if (ide->modal == IDE_MODAL_STYLE) draw_style(ctx, ide);
    else draw_about(ctx, ide);
    nk_end(ctx);
    if (nk_window_is_hidden(ctx, title)) ide->modal = IDE_MODAL_NONE;
}

void
ide_frame(struct nk_context *ctx, Ide *ide, int width, int height)
{
    struct nk_rect r;
    nk_flags box = NK_WINDOW_BORDER | NK_WINDOW_TITLE;
    int left;
    int right;
    int top;
    int bottom;
    int gap;
    TrWidget *sel;
    int i;
    prepare_style(ctx, ide);
    if (ide->view != 1) {
        code_entered = 0;
        drag_on = 0;
        ide_apply_code(ide);
    }
    left = px(230);
    right = px(320);
    top = px(78);
    bottom = px(150);
    gap = px(160);
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (left + right + gap > width) {
        int room = width - gap;
        if (room < 2) room = 2;
        left = room * 230 / (230 + 320);
        if (left < 1) left = 1;
        right = room - left;
        if (right < 1) right = 1;
    }
    if (top + bottom + px(120) > height) {
        int room = height - px(120);
        if (room < 2) room = 2;
        top = room * 78 / (78 + 150);
        if (top < 1) top = 1;
        bottom = room - top;
        if (bottom < 1) bottom = 1;
    }
    nhits = 0;
    r = nk_rect(0, 0, (float)width, (float)top);
    if (panel(ctx, "tccrad", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) draw_toolbar(ctx, ide);
    nk_end(ctx);

    if (ide->modal == IDE_MODAL_CODE) {
        ide->view = 1;
        ide->modal = IDE_MODAL_NONE;
    }
    if (ide->modal == IDE_MODAL_EXPLORE) {
        ide->view = 2;
        ide->modal = IDE_MODAL_NONE;
    }
    if (ide->view == 1 || ide->view == 2) {
        int body = height - top - bottom - px(70);
        const char *page = ide->view == 1 ? "Code" : "Explore";
        if (body < px(160)) body = px(160);
        r = nk_rect(0, (float)top, (float)width, (float)height - (float)top - (float)bottom);
        if (panel(ctx, page, r, box)) {
            if (ide->view == 1) draw_code(ctx, ide, body);
            else draw_explore(ctx, ide, body);
        }
        nk_end(ctx);
        r = nk_rect(0, (float)height - (float)bottom, (float)width, (float)bottom);
        if (panel(ctx, "Output", r, box)) {
            char line[TR_PATH_MAX + 8];
            const char *p = ide->log;
            int shown_lines = 0;
            nk_layout_row_dynamic(ctx, pxf(16), 1);
            snprintf(line, sizeof line, "%s%s", ide->dirty ? "* " : "", ide->path[0] ? ide->path : "(unsaved)");
            nk_label(ctx, line, NK_TEXT_LEFT);
            while (*p && shown_lines < 6) {
                size_t n = 0;
                while (p[n] && p[n] != '\n' && n + 1 < sizeof line) {
                    line[n] = p[n];
                    n++;
                }
                line[n] = '\0';
                nk_label(ctx, line, NK_TEXT_LEFT);
                shown_lines++;
                p += n;
                if (*p == '\n') p++;
            }
        }
        nk_end(ctx);
        draw_modal(ctx, ide, width, height);
        return;
    }

    {
        int avail = height - top - bottom;
        int pal_h;
        int scene_h = 0;
        int ncfg = ide->doc ? ide->doc->nconfigs : 0;
        if (avail < 2) avail = 2;
        if (ncfg > 0) {
            scene_h = px(40) + px(26) * (ncfg + 1);
            if (scene_h > avail / 3) scene_h = avail / 3;
            if (scene_h < px(72)) scene_h = px(72);
        }
        avail -= scene_h;
        if (avail < 2) avail = 2;
        pal_h = avail / 2;
        if (pal_h < px(180) && px(180) + px(140) <= avail) pal_h = px(180);
        if (avail - pal_h < px(140) && avail > px(140)) pal_h = avail - px(140);
        if (pal_h < 1) pal_h = 1;
        if (scene_h > 0) {
            r = nk_rect(0, (float)top, (float)left, (float)scene_h);
            if (panel(ctx, "Scenes", r, box)) draw_scenes(ctx, ide);
            nk_end(ctx);
        }
        r = nk_rect(0, (float)top + (float)scene_h, (float)left, (float)pal_h);
        if (panel(ctx, "Palette", r, box)) {
            nk_layout_row_dynamic(ctx, pxf(22), 2);
            for (i = 0; i < (int)(sizeof palette / sizeof palette[0]); i++) {
                if (nk_button_label(ctx, tr_kind_name(palette[i]))) ide_add(ide, palette[i]);
            }
        }
        nk_end(ctx);
        r = nk_rect(0, (float)top + (float)scene_h + (float)pal_h, (float)left,
            (float)height - (float)top - (float)bottom - (float)scene_h - (float)pal_h);
        if (panel(ctx, "Tree", r, box)) {
            if (ide->doc->nwidgets > 0) {
                const TrWidget *root = &ide->doc->widgets[0];
                char line[96];
                snprintf(line, sizeof line, "%s %s", tr_kind_name(root->kind), root->name);
                nk_layout_row_dynamic(ctx, pxf(22), 1);
                {
                    int on = root->id == ide->selected;
                    if (nk_selectable_label(ctx, line, NK_TEXT_LEFT, &on)) {
                        if (double_click(ide, root->id)) ide_open_code(ide, root->id);
                        else ide_select(ide, root->id);
                    }
                }
                draw_tree(ctx, ide, root->id, 1);
            }
        }
        nk_end(ctx);
    }

    r = nk_rect((float)width - (float)right, (float)top, (float)right,
        ((float)height - (float)top - (float)bottom) * 0.40f);
    if (panel(ctx, "Properties", r, box)) {
        sel = tr_doc_widget(ide->doc, ide->selected);
        if (sel) prop_editor(ctx, ide, sel);
    }
    nk_end(ctx);

    r.y = r.y + r.h;
    r.h = (float)height - (float)bottom - r.y;
    if (panel(ctx, "Events", r, box)) {
        sel = tr_doc_widget(ide->doc, ide->selected);
        if (sel) event_editor(ctx, ide, sel);
    }
    nk_end(ctx);

    r = nk_rect((float)left, (float)top, (float)width - (float)left - (float)right,
        (float)height - (float)top - (float)bottom);
    if (panel(ctx, "Canvas", r, box | NK_WINDOW_SCROLL_AUTO_HIDE)) {
        const TrWidget *root = ide->doc->nwidgets ? &ide->doc->widgets[0] : NULL;
        char title[TR_TEXT_MAX + 8];
        if (root) {
            nk_layout_row_dynamic(ctx, pxf(22), 1);
            {
                const char *scene = ide->show_all_cfg ? "all" : ide->doc->active_config;
                char head[TR_TEXT_MAX + 48];
                if (scene && scene[0] != '\0')
                    snprintf(head, sizeof head, "%s    %s", titled(root, "title", title, sizeof title),
                        scene);
                else
                    snprintf(head, sizeof head, "%s", titled(root, "title", title, sizeof title));
                nk_label(ctx, head, NK_TEXT_LEFT);
            }
            preview_children(ctx, ide, root->id);
            if (nk_window_is_hovered(ctx) && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
                float x = ctx->input.mouse.pos.x;
                float y = ctx->input.mouse.pos.y;
                int best = 0;
                float best_a = 1.0e30f;
                for (i = 0; i < nhits; i++) {
                    struct nk_rect hr = hits[i].r;
                    float area;
                    if (x < hr.x || y < hr.y || x > hr.x + hr.w || y > hr.y + hr.h) continue;
                    area = hr.w * hr.h;
                    if (area > 0.0f && area <= best_a) {
                        best_a = area;
                        best = hits[i].id;
                    }
                }
                if (best > 0) {
                    if (double_click(ide, best)) ide_open_code(ide, best);
                    else ide_select(ide, best);
                }
            }
        }
    }
    nk_end(ctx);

    r = nk_rect(0, (float)height - (float)bottom, (float)width, (float)bottom);
    if (panel(ctx, "Output", r, box)) {
        char line[TR_PATH_MAX + 8];
        const char *p = ide->log;
        int shown_lines = 0;
        nk_layout_row_dynamic(ctx, pxf(16), 1);
        snprintf(line, sizeof line, "%s%s", ide->dirty ? "* " : "", ide->path[0] ? ide->path : "(unsaved)");
        nk_label(ctx, line, NK_TEXT_LEFT);
        while (*p && shown_lines < 6) {
            size_t n = 0;
            while (p[n] && p[n] != '\n' && n + 1 < sizeof line) {
                line[n] = p[n];
                n++;
            }
            line[n] = '\0';
            nk_label(ctx, line, NK_TEXT_LEFT);
            shown_lines++;
            p += n;
            if (*p == '\n') p++;
        }
    }
    nk_end(ctx);
    draw_modal(ctx, ide, width, height);
}
