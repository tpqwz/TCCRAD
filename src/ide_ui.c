#define _POSIX_C_SOURCE 200809L
#include "ide.h"
#include "codegen.h"
#include "nk_glue.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int nhits;
static struct { int id; struct nk_rect r; } hits[TR_WIDGETS_MAX];
static int g_pct = 100;
static int style_ready;
static struct nk_style style_base;

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
    if (!style_ready) {
        style_base = ctx->style;
        style_ready = 1;
    }
    font = ctx->style.font;
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

static void
preview_one(struct nk_context *ctx, Ide *ide, const TrWidget *w)
{
    char buf[TR_TEXT_MAX + 8];
    if (!ide_visible(ide, w)) return;
    switch (w->kind) {
    case TR_KIND_LABEL:
        nk_label(ctx, shown(w, "text", buf, sizeof buf), NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_BUTTON:
        nk_button_label(ctx, shown(w, "label", buf, sizeof buf));
        note_hit(ctx, w->id);
        break;
    case TR_KIND_CHECKBOX:
        nk_label(ctx, shown(w, "label", buf, sizeof buf), NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_INPUT: {
        const char *text = shown(w, "text", buf, sizeof buf);
        nk_label(ctx, text[0] ? text : "[input]", NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    }
    case TR_KIND_SPACER:
        nk_label(ctx, " ", NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_SEPARATOR:
        nk_label(ctx, "--------", NK_TEXT_CENTERED);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_SLIDER:
        nk_label(ctx, "slider", NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_COMBO:
    case TR_KIND_RADIO:
        nk_label(ctx, shown(w, w->kind == TR_KIND_COMBO ? "items" : "options", buf, sizeof buf), NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_PROGRESS:
    case TR_KIND_COLOR:
    case TR_KIND_IMAGE:
        nk_label(ctx, tr_kind_name(w->kind), NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_CUSTOM:
        nk_label(ctx, shown(w, "func", buf, sizeof buf), NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        break;
    case TR_KIND_TABLE:
        nk_label(ctx, shown(w, "columns", buf, sizeof buf), NK_TEXT_LEFT);
        note_hit(ctx, w->id);
        preview_children(ctx, ide, w->id);
        break;
    default:
        if (nk_tree_push_id(ctx, NK_TREE_NODE, tr_kind_name(w->kind), NK_MAXIMIZED, w->id)) {
            note_hit(ctx, w->id);
            preview_children(ctx, ide, w->id);
            nk_tree_pop(ctx);
        }
        break;
    }
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
        TrKind k = ide->doc->widgets[idxs[i]].kind;
        if (k == TR_KIND_MENUBAR || k == TR_KIND_POPUP || k == TR_KIND_MODAL)
            preview_one(ctx, ide, &ide->doc->widgets[idxs[i]]);
    }
    n = tr_flow_indices(ide->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n <= 0) return;
    nr = tr_pack_rows(ide->doc, idxs, n, rows, TR_WIDGETS_MAX);
    if (nr < 0) return;
    for (r = 0; r < nr; r++) {
        float h = rows[r].height > 0 ? (float)rows[r].height : 28.0f;
        if (rows[r].height < 0) h = 96.0f;
        h = h * (float)g_pct / 100.0f;
        if (h < 1.0f) h = 1.0f;
        nk_layout_row_dynamic(ctx, h, rows[r].n > 0 ? rows[r].n : 1);
        for (c = 0; c < rows[r].n; c++)
            preview_one(ctx, ide, &ide->doc->widgets[rows[r].at[c]]);
    }
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
    int i;
    if (w->nevents <= 0) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        nk_label(ctx, "no events", NK_TEXT_LEFT);
        return;
    }
    if (ide->body_ev < 0 || ide->body_ev >= w->nevents) ide->body_ev = 0;
    nk_layout_row_dynamic(ctx, pxf(22), w->nevents > 3 ? 3 : w->nevents);
    for (i = 0; i < w->nevents; i++) {
        if (nk_select_label(ctx, w->events[i].name, NK_TEXT_CENTERED, i == ide->body_ev)) {
            if (ide->body_ev != i) ide->body_id = 0;
            ide->body_ev = i;
        }
    }
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
    if (nk_menu_begin_label(ctx, "View", NK_TEXT_LEFT, nk_vec2((float)px(180), (float)px(340)))) {
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
        if (nk_menu_item_label(ctx, "Smaller", NK_TEXT_LEFT)) ide_ui_bump(ide, -25);
        if (nk_menu_item_label(ctx, "Larger", NK_TEXT_LEFT)) ide_ui_bump(ide, 25);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, (float)px(70));
    if (nk_menu_begin_label(ctx, "Build", NK_TEXT_LEFT, nk_vec2((float)px(180), (float)px(240)))) {
        nk_layout_row_dynamic(ctx, pxf(22), 1);
        if (nk_menu_item_label(ctx, "Nuklear", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_NUKLEAR);
        if (nk_menu_item_label(ctx, "libui", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_LIBUI);
        if (nk_menu_item_label(ctx, "LVGL", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_LVGL);
        if (nk_menu_item_label(ctx, "Build", NK_TEXT_LEFT)) ide_build(ide, 0);
        if (nk_menu_item_label(ctx, "Build and run", NK_TEXT_LEFT)) ide_build(ide, 1);
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
            tr_copy_cap(doc->active_config, sizeof doc->active_config, doc->configs[i]);
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

static void
draw_code(struct nk_context *ctx, Ide *ide, int body)
{
    char names[48][48];
    int lines[48];
    int nrout;
    int current;
    int i;
    const char *p;
    int line = 0;
    int shown = 0;
    int limit;
    refresh_preview(ide);
    p = ide->preview.data ? ide->preview.data : "";
    if (ide->jump[0] != '\0') {
        char needle[96];
        int at = -1;
        int nw = snprintf(needle, sizeof needle, "void %s(", ide->jump);
        if (nw > 0 && (size_t)nw < sizeof needle) at = ide_find_line(p, needle, 0);
        if (at >= 0) {
            ide->code_scroll = at;
            ide->code_caret = at;
        }
        ide->jump[0] = '\0';
    }
    nrout = collect_routines(p, names, lines, 48);
    current = ide_routine_index(lines, nrout, ide->code_scroll);
    if (body < px(160)) body = px(160);
    limit = body / px(18);
    if (limit < 8) limit = 8;
    if (limit > 40) limit = 40;
    {
        const char *items[49];
        int pick = current < 0 ? 0 : current + 1;
        int got;
        int nitems = nrout + 1;
        if (nitems > 49) nitems = 49;
        items[0] = "(declarations)";
        for (i = 0; i < nitems - 1; i++) items[i + 1] = names[i];
        nk_layout_row_dynamic(ctx, pxf(24), 1);
        got = nk_combo(ctx, items, nitems, pick, px(22), nk_vec2((float)px(360), (float)px(280)));
        if (got != pick) {
            int dest = 0;
            if (got > 0 && got - 1 < nrout) dest = lines[got - 1];
            ide->code_scroll = dest;
            ide->code_caret = dest;
            current = ide_routine_index(lines, nrout, ide->code_scroll);
        }
    }
    nk_layout_row_dynamic(ctx, pxf(24), 2);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->code_find, (int)sizeof ide->code_find, nk_filter_default);
    if (nk_button_label(ctx, "Find next")) {
        int at = ide_find_line(p, ide->code_find, ide->code_scroll + 1);
        if (at < 0) at = ide_find_line(p, ide->code_find, 0);
        if (at >= 0) {
            ide->code_scroll = at;
            ide->code_caret = at;
        }
        else ide_set_status(ide, "not found");
    }
    nk_layout_row_dynamic(ctx, (float)body, 1);
    if (nk_group_begin(ctx, "source", NK_WINDOW_BORDER)) {
        p = ide->preview.data ? ide->preview.data : "";
        line = 0;
        {
            int depth = 0;
            while (*p && shown < limit && line < 100000) {
                char row[140];
                char bars[16];
                char gutter[180];
                size_t k = 0;
                int nw;
                int show;
                int b;
                while (p[k] && p[k] != '\n' && k + 1 < sizeof row) {
                    row[k] = p[k];
                    k++;
                }
                row[k] = '\0';
                show = ide_scope_show(row, &depth);
                bars[0] = '\0';
                for (b = 0; b < show && b < (int)sizeof bars - 1; b++) bars[b] = '|';
                if (b < (int)sizeof bars) bars[b] = '\0';
                if (line >= ide->code_scroll) {
                    struct nk_color hl = nk_rgb(48, 62, 84);
                    int hot = line == ide->code_caret;
                    struct nk_rect box;
                    nw = snprintf(gutter, sizeof gutter, "%4d %s", line + 1, bars);
                    if (nw < 0) gutter[0] = '\0';
                    nk_layout_row_begin(ctx, NK_DYNAMIC, pxf(18), 2);
                    nk_layout_row_push(ctx, 0.14f);
                    box = nk_widget_bounds(ctx);
                    if (hot) nk_fill_rect(nk_window_get_canvas(ctx), box, 0, hl);
                    nk_label(ctx, gutter, NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 0.86f);
                    box = nk_widget_bounds(ctx);
                    if (hot) nk_fill_rect(nk_window_get_canvas(ctx), box, 0, hl);
                    nk_label(ctx, row, NK_TEXT_LEFT);
                    nk_layout_row_end(ctx);
                    shown++;
                }
                p += k;
                if (*p == '\n') p++;
                line++;
            }
        }
        nk_group_end(ctx);
    }
    nk_layout_row_dynamic(ctx, pxf(24), 3);
    if (nk_button_label(ctx, "Up") && ide->code_caret > 0) {
        ide->code_caret--;
        if (ide->code_caret < ide->code_scroll) ide->code_scroll = ide->code_caret;
    }
    if (nk_button_label(ctx, "Down")) {
        ide->code_caret++;
        if (ide->code_caret >= ide->code_scroll + limit)
            ide->code_scroll = ide->code_caret - limit + 1;
    }
    if (nk_button_label(ctx, "Design")) {
        ide->view = 0;
        ide->modal = IDE_MODAL_NONE;
    }
}

enum { EX_FORM = 1, EX_WIDGET, EX_PROC };

static void
open_explore(Ide *ide, int kind, int id, int line)
{
    if (kind == EX_PROC) {
        ide->code_scroll = line > 0 ? line : 0;
        ide->code_caret = ide->code_scroll;
        ide->jump[0] = '\0';
        ide->view = 1;
        ide->modal = IDE_MODAL_NONE;
        return;
    }
    if (id > 0) ide_select(ide, id);
    ide->view = 0;
    ide->modal = IDE_MODAL_NONE;
}

static void
draw_explore(struct nk_context *ctx, Ide *ide)
{
    char names[48][48];
    int lines[48];
    int nrout;
    int ids[160];
    int kinds[160];
    int lns[160];
    char labels[160][80];
    int n = 0;
    int i;
    const char *filt = ide->explore_filter;
    const TrWidget *root;
    refresh_preview(ide);
    nrout = collect_routines(ide->preview.data, names, lines, 48);
    root = ide->doc && ide->doc->nwidgets > 0 ? &ide->doc->widgets[0] : NULL;
    if (root && n < 160) {
        char label[80];
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
        for (i = 1; i < ide->doc->nwidgets && n < 160; i++) {
            const TrWidget *w = &ide->doc->widgets[i];
            char label[80];
            snprintf(label, sizeof label, "%s  %s", tr_kind_name(w->kind), w->name);
            if (!ide_text_has(label, filt)) continue;
            snprintf(labels[n], sizeof labels[n], "%s", label);
            kinds[n] = EX_WIDGET;
            ids[n] = w->id;
            lns[n] = 0;
            n++;
        }
    }
    for (i = 0; i < nrout && n < 160; i++) {
        char label[80];
        snprintf(label, sizeof label, "proc  %s", names[i]);
        if (!ide_text_has(label, filt)) continue;
        snprintf(labels[n], sizeof labels[n], "%s", label);
        kinds[n] = EX_PROC;
        ids[n] = 0;
        lns[n] = lines[i];
        n++;
    }
    if (ide->explore_sel >= n) ide->explore_sel = n > 0 ? 0 : -1;
    nk_layout_row_dynamic(ctx, pxf(24), 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->explore_filter,
        (int)sizeof ide->explore_filter, nk_filter_default);
    nk_layout_row_dynamic(ctx, (float)px(260), 1);
    if (nk_group_begin(ctx, "explore-index", NK_WINDOW_BORDER)) {
        for (i = 0; i < n; i++) {
            int on = i == ide->explore_sel;
            int before = on;
            nk_layout_row_dynamic(ctx, pxf(20), 1);
            if (nk_selectable_label(ctx, labels[i], NK_TEXT_LEFT, &on)) {
                if (before) open_explore(ide, kinds[i], ids[i], lns[i]);
                else ide->explore_sel = i;
            }
        }
        nk_group_end(ctx);
    }
    nk_layout_row_dynamic(ctx, pxf(18), 1);
    if (ide->explore_sel < 0 || ide->explore_sel >= n) {
        nk_label(ctx, "Select a form, a widget, or a proc.", NK_TEXT_LEFT);
    } else if (kinds[ide->explore_sel] == EX_PROC) {
        char line[80];
        nk_label(ctx, labels[ide->explore_sel], NK_TEXT_LEFT);
        snprintf(line, sizeof line, "line %d", lns[ide->explore_sel] + 1);
        nk_label(ctx, line, NK_TEXT_LEFT);
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
    nk_layout_row_dynamic(ctx, pxf(26), 2);
    if (nk_button_label(ctx, "Open") && ide->explore_sel >= 0 && ide->explore_sel < n)
        open_explore(ide, kinds[ide->explore_sel], ids[ide->explore_sel], lns[ide->explore_sel]);
    if (nk_button_label(ctx, "Design")) {
        ide->view = 0;
        ide->modal = IDE_MODAL_NONE;
    }
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
    else if (ide->modal == IDE_MODAL_EXPLORE) draw_explore(ctx, ide);
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
            else draw_explore(ctx, ide);
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
        if (avail < 2) avail = 2;
        pal_h = avail / 2;
        if (pal_h < px(180) && px(180) + px(140) <= avail) pal_h = px(180);
        if (avail - pal_h < px(140) && avail > px(140)) pal_h = avail - px(140);
        if (pal_h < 1) pal_h = 1;
        r = nk_rect(0, (float)top, (float)left, (float)pal_h);
        if (panel(ctx, "Palette", r, box)) {
            nk_layout_row_dynamic(ctx, pxf(22), 2);
            for (i = 0; i < (int)(sizeof palette / sizeof palette[0]); i++) {
                if (nk_button_label(ctx, tr_kind_name(palette[i]))) ide_add(ide, palette[i]);
            }
        }
        nk_end(ctx);
        r = nk_rect(0, (float)top + (float)pal_h, (float)left,
            (float)height - (float)top - (float)bottom - (float)pal_h);
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
            snprintf(title, sizeof title, "%s  [%s]", root->name, tr_backend_name(ide->doc->backend));
            nk_label(ctx, title, NK_TEXT_LEFT);
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
                    if (area < best_a) {
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
