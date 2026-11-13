#define _POSIX_C_SOURCE 200809L
#include "ide.h"
#include "codegen.h"
#include "rope.h"
#include "nk_glue.h"

#include <fcntl.h>
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
        nk_bool on = tr_prop_int(w, "checked", 0) ? nk_true : nk_false;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_checkbox_label(ctx, titled(w, "label", buf, sizeof buf), &on);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_RADIO: {
        nk_bool on = nk_true;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_radio_label(ctx, titled(w, "options", buf, sizeof buf), &on);
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
        int v = tr_prop_int(w, "value", 0);
        int mn = tr_prop_int(w, "min", 0);
        int mx = tr_prop_int(w, "max", 100);
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
        const char *items[1];
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        items[0] = titled(w, "items", buf, sizeof buf);
        nk_combo(ctx, items, 1, 0, px(22), nk_vec2(pxf(200), pxf(80)));
        stroke_selected(ctx, ide, w->id, slot);
        break;
    }
    case TR_KIND_PROGRESS: {
        int mx = tr_prop_int(w, "max", 100);
        nk_size cur;
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        if (mx < 1) mx = 1;
        cur = (nk_size)tr_prop_int(w, "value", 0);
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
    case TR_KIND_SEPARATOR:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, "--------", NK_TEXT_CENTERED);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    case TR_KIND_SPACER:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, " ", NK_TEXT_LEFT);
        stroke_selected(ctx, ide, w->id, slot);
        break;
    case TR_KIND_IMAGE:
        slot = nk_widget_bounds(ctx);
        note_hit(ctx, w->id);
        nk_label(ctx, titled(w, "path", buf, sizeof buf), NK_TEXT_LEFT);
        stroke_selected(ctx, ide, w->id, slot);
        break;
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
            nk_layout_row_dynamic(ctx, pxf(22), nc);
            for (row = 0; row < nc; row++) nk_label(ctx, parts[row], NK_TEXT_LEFT);
            for (row = 0; row < 3; row++) {
                int c;
                nk_layout_row_dynamic(ctx, pxf(22), nc);
                for (c = 0; c < nc; c++) nk_label(ctx, " ", NK_TEXT_LEFT);
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

static void
event_editor(struct nk_context *ctx, Ide *ide, TrWidget *w)
{
    const char *const *defs;
    int n = 0;
    int i;
    defs = tr_event_defs(w->kind, &n);
    if (!defs || n <= 0) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        nk_label(ctx, "no events", NK_TEXT_LEFT);
        return;
    }
    for (i = 0; i < n; i++) {
        int slot = -1;
        int e;
        int on;
        char row[96];
        const char *hn = "";
        for (e = 0; e < w->nevents; e++) {
            if (strcmp(w->events[e].name, defs[i]) == 0) {
                slot = e;
                if (w->events[e].handler[0] != '\0') hn = w->events[e].handler;
                break;
            }
        }
        snprintf(row, sizeof row, "%s    %s", defs[i], hn[0] ? hn : "—");
        on = slot >= 0 && slot == ide->body_ev;
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_selectable_label(ctx, row, NK_TEXT_LEFT, &on)) {
            if (double_click(ide, 500000 + w->id * 8 + i))
                ide_open_named_event(ide, w->id, defs[i]);
            else if (slot >= 0) {
                if (ide->body_ev != slot) ide->body_id = 0;
                ide->body_ev = slot;
            }
        }
    }
    if (ide->body_ev < 0 || ide->body_ev >= w->nevents) return;
    nk_layout_row_dynamic(ctx, pxf(24), 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, w->events[ide->body_ev].handler,
        TR_NAME_MAX, nk_filter_default);
    if (ide->body_id != w->id || ide->body_ev < 0) {
        const char *t = tr_span_ptr(ide->doc, w->events[ide->body_ev].user);
        tr_copy_cap(ide->body, sizeof ide->body, t);
        ide->body_len = (int)strlen(ide->body);
        ide->body_id = w->id;
    }
    nk_layout_row_dynamic(ctx, pxf(22), 1);
    nk_checkbox_label(ctx, "Spaced C", &ide->doc->spaced);
    nk_layout_row_dynamic(ctx, pxf(120), 1);
    nk_edit_string(ctx, NK_EDIT_BOX, ide->body, &ide->body_len, IDE_BODY, nk_filter_default);
    if (ide->body_len < 0) ide->body_len = 0;
    if (ide->body_len >= IDE_BODY) ide->body_len = IDE_BODY - 1;
    ide->body[ide->body_len] = '\0';
    tr_span_set(ide->doc, &w->events[ide->body_ev].user, ide->body, (size_t)ide->body_len);
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
    err[0] = '\0';
    tr_buf_clear(&ide->preview);
    if (tr_buf_init(&h) != 0) return;
    if (tr_codegen(ide->doc, &ide->style, "app.h", &h, &ide->preview, err, sizeof err) != 0) {
        tr_buf_clear(&ide->preview);
        tr_buf_append_cstr(&ide->preview, err[0] ? err : "codegen failed");
    }
    tr_buf_free(&h);
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
}

static void
paint_code(struct nk_context *ctx, struct nk_rect box, const char *s, int hot, int caret,
    const struct CodeInk *ink, int locked, int spaced)
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
        int len;
        float w;
        if (next <= at) next = at + 1;
        if (kind == 1) col = com;
        else if (kind == 2) col = strc;
        else if (kind == 3) col = num;
        else if (kind == 4 || kind == 5) col = key;
        else if (kind == 6) col = pre;
        len = next - at;
        w = font->width(font->userdata, h, s + at, len);
        nk_draw_text(buf, nk_rect(x, box.y, w + 2.0f, box.h), s + at, len, font, bg, col);
        x += w;
        at = next;
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
sync_rope(Ide *ide, const char *text)
{
    size_t n;
    int rebuild = 1;
    if (!ide || !text) return;
    if (ide->code_live && ide->code_rope) return;
    n = strlen(text);
    if (ide->code_rope && tr_rope_len(ide->code_rope) == n) {
        char *cur = tr_rope_flat(ide->code_rope, NULL);
        if (cur && memcmp(cur, text, n) == 0) rebuild = 0;
        free(cur);
    }
    if (!rebuild) return;
    tr_rope_free(ide->code_rope);
    ide->code_rope = tr_rope_new(text, n);
    ide->code_at = line_byte(text, ide->code_caret);
}

static void
edit_keys(struct nk_context *ctx, Ide *ide, int visible, const char *file)
{
    int changed = 0;
    int rejected = 0;
    if (!ide->code_focus || !ide->code_rope) return;
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_BACKSPACE) && ide->code_at > 0) {
        if (ide_user_writable(file, ide->code_at - 1, 1)) {
            tr_rope_delete(&ide->code_rope, ide->code_at - 1, 1);
            ide->code_at--;
            changed = 1;
        } else {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DEL) &&
        ide->code_at < tr_rope_len(ide->code_rope)) {
        if (ide_user_writable(file, ide->code_at, 1)) {
            tr_rope_delete(&ide->code_rope, ide->code_at, 1);
            changed = 1;
        } else {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER)) {
        if (ide_user_writable(file, ide->code_at, 0)) {
            tr_rope_insert(&ide->code_rope, ide->code_at, "\n", 1);
            ide->code_at++;
            changed = 1;
        } else {
            rejected = 1;
        }
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_LEFT) && ide->code_at > 0)
        ide->code_at--;
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_RIGHT) &&
        ide->code_at < tr_rope_len(ide->code_rope))
        ide->code_at++;
    if (ctx->input.keyboard.text_len > 0) {
        int n = ctx->input.keyboard.text_len;
        if (ide_user_writable(file, ide->code_at, 0)) {
            tr_rope_insert(&ide->code_rope, ide->code_at, ctx->input.keyboard.text, (size_t)n);
            ide->code_at += (size_t)n;
            changed = 1;
        } else {
            rejected = 1;
        }
    }
    if (rejected && !changed)
        ide_set_status(ide, "Generated code is read-only. Edit a user region.");
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_UP) && ide->code_at > 0) {
        size_t i = ide->code_at;
        if (i > 0) i--;
        while (i > 0 && tr_rope_at(ide->code_rope, i - 1) != '\n') i--;
        ide->code_at = i;
    }
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN)) {
        size_t n = tr_rope_len(ide->code_rope);
        size_t i = ide->code_at;
        while (i < n && tr_rope_at(ide->code_rope, i) != '\n') i++;
        if (i < n) i++;
        ide->code_at = i;
    }
    if (!changed && !nk_input_is_key_pressed(&ctx->input, NK_KEY_LEFT) &&
        !nk_input_is_key_pressed(&ctx->input, NK_KEY_RIGHT) &&
        !nk_input_is_key_pressed(&ctx->input, NK_KEY_UP) &&
        !nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN))
        return;
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
    if (changed) ide->code_live = 1;
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
    struct CodeInk ink;
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
    if (ide->code_spaced && !ide->code_live) {
        const char *shown = p;
        if (ide_code_lens(ide, p, strlen(p), &shown) == 0 && shown) p = shown;
    }
    file = p;
    sync_rope(ide, p);
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
        }
        ide->jump[0] = '\0';
        ide->code_hold = 1;
    }
    if (ide->code_hold) {
        size_t byte = line_byte(p, ide->code_caret);
        size_t end = byte;
        while (p[end] != '\0' && p[end] != '\n') end++;
        while (byte < end && (p[byte] == ' ' || p[byte] == '\t')) byte++;
        ide->code_at = byte;
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
            if (nk_selectable_label(ctx, "C", NK_TEXT_CENTERED, &on) && ide->code_spaced)
                ide_code_set_spaced(ide, 0, p, strlen(p));
        }
        nk_layout_row_push(ctx, pxf(96));
        {
            int on = ide->code_spaced;
            if (nk_selectable_label(ctx, "Spaced", NK_TEXT_CENTERED, &on) && !ide->code_spaced)
                ide_code_set_spaced(ide, 1, p, strlen(p));
        }
        nk_layout_row_end(ctx);
        nk_layout_row_dynamic(ctx, pxf(24), 1);
        got = nk_combo(ctx, items, nitems, pick, px(22), nk_vec2((float)px(360), (float)px(280)));
        if (!ide->code_hold && got != pick) {
            int dest = 0;
            int top;
            if (got > 0 && got - 1 < nrout) dest = lines[got - 1];
            top = dest - visible / 3;
            if (top < 0) top = 0;
            ide->code_caret = dest;
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
            ide->code_caret = at;
            ide->code_scroll = top;
        }
        else ide_set_status(ide, "not found");
    }
    {
        int total = 0;
        const char *scan = p;
        while (*scan && total < 200000) {
            while (*scan && *scan != '\n') scan++;
            total++;
            if (*scan == '\n') scan++;
        }
        if (total < 1) total = 1;
        if (ide->code_scroll > total - visible) ide->code_scroll = total - visible;
        if (ide->code_scroll < 0) ide->code_scroll = 0;
    }
    nk_layout_row_template_begin(ctx, (float)body);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, (float)px(18));
    nk_layout_row_template_end(ctx);
    if (nk_group_begin(ctx, "source", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        if (ctx->input.mouse.scroll_delta.y != 0.0f && nk_window_is_hovered(ctx)) {
            ide->code_scroll -= (int)ctx->input.mouse.scroll_delta.y * 3;
            if (ide->code_scroll < 0) ide->code_scroll = 0;
        }
        p = owned ? owned : (ide->preview.data ? ide->preview.data : "");
        line = 0;
        {
            const char *origin = p;
            int depth = 0;
            int shown = 0;
            while (*p && shown < visible && line < 200000) {
                char row[140];
                char bars[48];
                char gutter[180];
                const char *ls = p;
                size_t k = 0;
                int nw;
                while (p[k] && p[k] != '\n' && k + 1 < sizeof row) {
                    row[k] = p[k];
                    k++;
                }
                row[k] = '\0';
                ide_scope_gutter(row, &depth, bars, sizeof bars);
                if (line >= ide->code_scroll && shown < visible) {
                    int hot = line == ide->code_caret;
                    struct nk_rect box;
                    char call[64];
                    int ext = line_extern(row, names, nrout, call, sizeof call);
                    nw = snprintf(gutter, sizeof gutter, "%4d", line + 1);
                    if (nw < 0) gutter[0] = '\0';
                    nk_layout_row_template_begin(ctx, pxf(18));
                    nk_layout_row_template_push_static(ctx, (float)px(18));
                    nk_layout_row_template_push_static(ctx, (float)px(96));
                    nk_layout_row_template_push_static(ctx, (float)px(46));
                    nk_layout_row_template_push_dynamic(ctx);
                    nk_layout_row_template_end(ctx);
                    box = nk_widget_bounds(ctx);
                    if (hot) nk_fill_rect(nk_window_get_canvas(ctx), box, 0, ink.hot);
                    nk_label(ctx, ext ? ">" : "", NK_TEXT_CENTERED);
                    if (ext && nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, box) &&
                        double_click(ide, 300000 + line)) {
                        char found[TR_PATH_MAX];
                        int ln = 0;
                        if (ide_find_def(ide->home, ide->path, call, found, sizeof found, &ln)) {
                            tr_copy_cap(ide->code_ext, sizeof ide->code_ext, found);
                            ide->code_caret = ln;
                            ide->code_scroll = ln > visible / 3 ? ln - visible / 3 : 0;
                            ide_set_status(ide, found);
                        } else {
                            ide_set_status(ide, "definition not in the project");
                        }
                    }
                    box = nk_widget_bounds(ctx);
                    if (hot) nk_fill_rect(nk_window_get_canvas(ctx), box, 0, ink.hot);
                    nk_label(ctx, bars, NK_TEXT_LEFT);
                    box = nk_widget_bounds(ctx);
                    if (hot) nk_fill_rect(nk_window_get_canvas(ctx), box, 0, ink.hot);
                    nk_label(ctx, gutter, NK_TEXT_RIGHT);
                    box = nk_widget_bounds(ctx);
                    if (!ide->code_hold &&
                        nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, box)) {
                        const struct nk_user_font *font = ctx->style.font;
                        float rel = ctx->input.mouse.pos.x - box.x;
                        float acc = 0.0f;
                        int col = 0;
                        size_t start = (size_t)(ls - origin);
                        ide->code_focus = 1;
                        ide->code_caret = line;
                        while (row[col] != '\0' && acc < rel) {
                            float cw = font->width(font->userdata, font->height, row + col, 1);
                            acc += cw;
                            col++;
                        }
                        ide->code_at = start + (size_t)col;
                    }
                    {
                        int col = -1;
                        size_t start = (size_t)(ls - origin);
                        int locked = !ide_user_writable(origin, start, 0);
                        if (hot && ide->code_at >= start) col = (int)(ide->code_at - start);
                        if (nk_widget(&box, ctx) != NK_WIDGET_INVALID)
                            paint_code(ctx, box, row, hot, col, &ink, locked, ide->code_spaced);
                    }
                    shown++;
                }
                p += k;
                if (*p == '\n') p++;
                line++;
            }
        }
        nk_group_end(ctx);
    }
    {
        int total = 0;
        const char *scan = file;
        int max_scroll;
        while (*scan && total < 200000) {
            while (*scan && *scan != '\n') scan++;
            total++;
            if (*scan == '\n') scan++;
        }
        max_scroll = total - visible;
        if (max_scroll < 0) max_scroll = 0;
        if (ide->code_scroll > max_scroll) ide->code_scroll = max_scroll;
        nk_slider_int(ctx, 0, &ide->code_scroll, max_scroll, 1);
    }
    nk_layout_row_dynamic(ctx, pxf(24), 3);
    if (nk_button_label(ctx, "Up") && ide->code_caret > 0) {
        ide->code_caret--;
        if (ide->code_scroll > ide->code_caret) ide->code_scroll = ide->code_caret;
    }
    if (nk_button_label(ctx, "Down")) {
        ide->code_caret++;
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

static void
draw_explore(struct nk_context *ctx, Ide *ide, int body)
{
    TrExplore procs[TR_EXPLORE_MAX];
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
    const char *text;
    char *owned = NULL;
    const TrWidget *root;
    refresh_preview(ide);
    text = preview_view(ide, &owned);
    nproc = tr_explore(text, procs, TR_EXPLORE_MAX);
    free(owned);
    if (nproc < 0) nproc = 0;
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
    if (ide->view != 1) ide_apply_code(ide);
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
        ((float)height - (float)top - (float)bottom) * 0.62f);
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
