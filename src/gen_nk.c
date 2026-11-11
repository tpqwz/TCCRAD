#include "codegen_priv.h"

#include <stdio.h>
#include <string.h>

static int nk_one(Gen *g, const TrWidget *w, int d);
static int nk_children(Gen *g, int parent, int d);

static const char *
nk_align(const TrWidget *w)
{
    const TrProp *p = tr_prop(w, "align");
    if (p && strcmp(p->value, "center") == 0) return "NK_TEXT_CENTERED";
    if (p && strcmp(p->value, "right") == 0) return "NK_TEXT_RIGHT";
    return "NK_TEXT_LEFT";
}

static int
preload(Gen *g, const CgSlot *s, int d)
{
    if (!s->preload_on) return 0;
    return cg_gi(g, d, "%s = (%s);\n", s->lv, s->preload);
}

static int
call_ev(Gen *g, const TrWidget *w, const char *ev, int d)
{
    const TrEvent *e = cg_event(w, ev);
    char hn[TR_NAME_MAX];
    if (!e) return 0;
    if (cg_handler(g->doc, w, e, hn, sizeof hn) != 0) return cg_err(g, "handler name is invalid");
    return cg_gi(g, d, "%s(ui);\n", hn);
}

static int
plain_int(Gen *g, const TrWidget *w, const char *key, int def)
{
    const TrProp *p = tr_prop(w, key);
    char rew[512];
    if (p && p->bind[0] != '\0' && !p->twoway) {
        if (tr_rewrite_expr(g->doc, p->bind, rew, sizeof rew) != 0) return -1;
        return cg_gp(g, "(%s)", rew);
    }
    return cg_gp(g, "%d", tr_prop_int(w, key, def));
}

static int
win_flags(Gen *g, const TrWidget *w)
{
    const char *bits[8];
    int n = 0;
    int i;
    if (tr_prop_int(w, "border", 1)) bits[n++] = "NK_WINDOW_BORDER";
    if (tr_prop_int(w, "movable", 1)) bits[n++] = "NK_WINDOW_MOVABLE";
    if (tr_prop_int(w, "resizable", 1)) bits[n++] = "NK_WINDOW_SCALABLE";
    if (tr_prop_int(w, "titlebar", 1)) bits[n++] = "NK_WINDOW_TITLE";
    if (tr_prop_int(w, "closable", 1)) bits[n++] = "NK_WINDOW_CLOSABLE";
    if (tr_prop_int(w, "scroll", 1)) bits[n++] = "NK_WINDOW_SCROLL_AUTO_HIDE";
    else bits[n++] = "NK_WINDOW_NO_SCROLLBAR";
    for (i = 0; i < n; i++) {
        if (i && cg_gp(g, "|") != 0) return -1;
        if (cg_gp(g, "%s", bits[i]) != 0) return -1;
    }
    return 0;
}

static int
opens_layout(TrKind kind)
{
    return kind == TR_KIND_GROUP || kind == TR_KIND_CHILD || kind == TR_KIND_DOCK ||
        kind == TR_KIND_COLLAPSE || kind == TR_KIND_TREE || kind == TR_KIND_TABS ||
        kind == TR_KIND_TABLE || kind == TR_KIND_SPLITTER || kind == TR_KIND_CUSTOM ||
        kind == TR_KIND_TAB;
}

static int
nk_flow(Gen *g, int parent, int d)
{
    int idxs[TR_WIDGETS_MAX];
    TrRow rows[TR_WIDGETS_MAX];
    int n;
    int nr;
    int r;
    int c;
    n = tr_flow_indices(g->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n < 0) return cg_err(g, "a container has too many children");
    if (n == 0) return 0;
    nr = tr_pack_rows(g->doc, idxs, n, rows, TR_WIDGETS_MAX);
    if (nr < 0) return cg_err(g, "row layout failed");
    for (r = 0; r < nr; r++) {
        char hexpr[80];
        const TrRow *row = &rows[r];
        int ns;
        int nested = 0;
        if (row->height < 0) {
            int gap = -row->height;
            ns = snprintf(hexpr, sizeof hexpr, "tr_remain(ctx, tr_u(ui, %d))", gap);
        } else {
            ns = snprintf(hexpr, sizeof hexpr, "(float)tr_u(ui, %d)", row->height);
        }
        if (ns < 0 || (size_t)ns >= sizeof hexpr) return -1;
        for (c = 0; c < row->n; c++) {
            if (opens_layout(g->doc->widgets[row->at[c]].kind)) nested = 1;
        }
        /* A container starts its own rows. row_end would then see the wrong row type. */
        if (nested) {
            for (c = 0; c < row->n; c++) {
                if (cg_gi(g, d, "nk_layout_row_dynamic(ctx, %s, 1);\n", hexpr) != 0) return -1;
                if (nk_one(g, &g->doc->widgets[row->at[c]], d) != 0) return -1;
            }
            continue;
        }
        if (cg_gi(g, d, "nk_layout_row_begin(ctx, NK_DYNAMIC, %s, %d);\n", hexpr, row->n) != 0)
            return -1;
        for (c = 0; c < row->n; c++) {
            const TrWidget *w = &g->doc->widgets[row->at[c]];
            int weight = w->stretch > 0 ? w->stretch : 1;
            if (cg_gi(g, d, "nk_layout_row_push(ctx, %d.0f / %d.0f);\n", weight, row->stretch) != 0)
                return -1;
            if (nk_one(g, w, d) != 0) return -1;
        }
        if (cg_gi(g, d, "nk_layout_row_end(ctx);\n") != 0) return -1;
    }
    return 0;
}

static int
nk_leaf_label(Gen *g, const TrWidget *w, int d)
{
    if (cg_gi(g, d, "nk_label(ctx, ") != 0) return -1;
    if (cg_text(g, w, "text") != 0) return -1;
    return cg_gp(g, ", %s);\n", nk_align(w));
}

static int
nk_leaf_button(Gen *g, const TrWidget *w, int d)
{
    if (cg_gi(g, d, "if (nk_button_label(ctx, ") != 0) return -1;
    if (cg_text(g, w, "label") != 0) return -1;
    if (cg_gp(g, ")) {\n") != 0) return -1;
    if (call_ev(g, w, "click", d + 1) != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_state_int(Gen *g, const TrWidget *w, const char *key, int d, CgSlot *s)
{
    if (cg_int_slot(g, w, key, s) != 0) return -1;
    return preload(g, s, d);
}

static int
nk_leaf_check(Gen *g, const TrWidget *w, int d)
{
    CgSlot s;
    if (nk_state_int(g, w, "checked", d, &s) != 0) return -1;
    if (cg_gi(g, d, "if (nk_checkbox_label(ctx, ") != 0) return -1;
    if (cg_text(g, w, "label") != 0) return -1;
    if (cg_gp(g, ", &%s)) {\n", s.lv) != 0) return -1;
    if (call_ev(g, w, "toggle", d + 1) != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_leaf_radio(Gen *g, const TrWidget *w, int d)
{
    CgSlot s;
    const TrProp *opt = tr_prop(w, "options");
    char parts[16][TR_TEXT_MAX];
    int n = 0;
    int i;
    const char *p;
    if (!opt || opt->bind[0] != '\0') return cg_err(g, "radio options must be a literal list");
    p = opt->value;
    while (*p && n < 16) {
        const char *semi = strchr(p, ';');
        size_t len = semi ? (size_t)(semi - p) : strlen(p);
        if (len >= TR_TEXT_MAX) return cg_err(g, "a radio option is too long");
        memcpy(parts[n], p, len);
        parts[n][len] = '\0';
        n++;
        if (!semi) break;
        p = semi + 1;
    }
    if (n < 1) return cg_err(g, "radio needs at least one option");
    if (nk_state_int(g, w, "selected", d, &s) != 0) return -1;
    if (cg_gi(g, d, "{ int tr_before = %s;\n", s.lv) != 0) return -1;
    for (i = 0; i < n; i++) {
        if (cg_gi(g, d, "if (nk_option_label(ctx, ") != 0) return -1;
        if (cg_cstr(g, parts[i]) != 0) return -1;
        if (cg_gp(g, ", %s == %d)) %s = %d;\n", s.lv, i, s.lv, i) != 0) return -1;
    }
    if (cg_gi(g, d, "if (%s != tr_before) {\n", s.lv) != 0) return -1;
    if (call_ev(g, w, "change", d + 1) != 0) return -1;
    if (cg_gi(g, d, "}\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_leaf_input(Gen *g, const TrWidget *w, int d)
{
    const TrProp *tx = tr_prop(w, "text");
    int twoway = tx && tx->twoway;
    int cap;
    int nchars = 0;
    const char *buf;
    const char *lenm;
    char bbuf[64];
    char lbuf[64];
    int secret = tr_prop_int(w, "password", 0) != 0 && tr_prop_int(w, "multiline", 0) == 0;
    if (twoway) {
        const TrField *f = tr_field_find(g->doc, tx->bind);
        if (!f || tr_field_type(f->type, &nchars) != TR_FT_CHARS) return -1;
        cap = nchars;
        if (snprintf(bbuf, sizeof bbuf, "ui->%s", f->name) < 0) return -1;
        if (snprintf(lbuf, sizeof lbuf, "ui->%s_len", f->name) < 0) return -1;
        buf = bbuf;
        lenm = lbuf;
    } else {
        cap = tr_prop_int(w, "capacity", 64);
        if (cap < 1) cap = 1;
        if (cap > 1024) cap = 1024;
        if (snprintf(bbuf, sizeof bbuf, "ui->tr_p_%d_text", w->id) < 0) return -1;
        if (snprintf(lbuf, sizeof lbuf, "ui->tr_p_%d_text_n", w->id) < 0) return -1;
        buf = bbuf;
        lenm = lbuf;
    }
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_n = %s;\n", lenm) != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_before;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_n < 0) tr_n = 0;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_n >= %d) tr_n = %d;\n", cap, cap - 1) != 0) return -1;
    if (cg_gi(g, d + 1, "tr_before = tr_n;\n") != 0) return -1;
    if (secret) {
        /* The edit shows stars. Keystrokes are recovered from what was appended. */
        if (cg_gi(g, d + 1, "{\n") != 0) return -1;
        if (cg_gi(g, d + 2, "char tr_mask[%d];\n", cap) != 0) return -1;
        if (cg_gi(g, d + 2, "int tr_i;\n") != 0) return -1;
        if (cg_gi(g, d + 2, "int tr_old = tr_n;\n") != 0) return -1;
        if (cg_gi(g, d + 2, "for (tr_i = 0; tr_i < tr_n; tr_i++) tr_mask[tr_i] = '*';\n") != 0)
            return -1;
        if (cg_gi(g, d + 2, "tr_mask[tr_n] = '\\0';\n") != 0) return -1;
        if (cg_gi(g, d + 2,
                "nk_edit_string(ctx, NK_EDIT_FIELD, tr_mask, &tr_n, %d, nk_filter_ascii);\n",
                cap) != 0)
            return -1;
        if (cg_gi(g, d + 2, "if (tr_n < 0) tr_n = 0;\n") != 0) return -1;
        if (cg_gi(g, d + 2, "if (tr_n >= %d) tr_n = %d;\n", cap, cap - 1) != 0) return -1;
        if (cg_gi(g, d + 2, "tr_mask[tr_n] = '\\0';\n") != 0) return -1;
        if (cg_gi(g, d + 2, "{\n") != 0) return -1;
        if (cg_gi(g, d + 3, "int tr_cur = tr_n;\n") != 0) return -1;
        if (cg_gi(g, d + 3, "if (ctx && ctx->current) tr_cur = ctx->current->edit.cursor;\n") != 0)
            return -1;
        if (cg_gi(g, d + 3, "tr_n = tr_unmask(%s, %d, tr_mask, tr_n, tr_old, tr_cur);\n", buf, cap) != 0)
            return -1;
        if (cg_gi(g, d + 2, "}\n") != 0) return -1;
        if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    } else {
        const char *flags = tr_prop_int(w, "multiline", 0) ? "NK_EDIT_BOX" : "NK_EDIT_FIELD";
        if (cg_gi(g, d + 1, "nk_edit_string(ctx, %s, %s, &tr_n, %d, nk_filter_default);\n",
                flags, buf, cap) != 0)
            return -1;
        if (cg_gi(g, d + 1, "if (tr_n < 0) tr_n = 0;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (tr_n >= %d) tr_n = %d;\n", cap, cap - 1) != 0) return -1;
        if (cg_gi(g, d + 1, "%s[tr_n] = '\\0';\n", buf) != 0) return -1;
    }
    if (cg_gi(g, d + 1, "%s = tr_n;\n", lenm) != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_n != tr_before) {\n") != 0) return -1;
    if (call_ev(g, w, "change", d + 2) != 0) return -1;
    if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_leaf_slider(Gen *g, const TrWidget *w, int d)
{
    CgSlot s;
    if (nk_state_int(g, w, "value", d, &s) != 0) return -1;
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_min = ") != 0) return -1;
    if (plain_int(g, w, "min", 0) != 0) return -1;
    if (cg_gp(g, ";\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_max = ") != 0) return -1;
    if (plain_int(g, w, "max", 100) != 0) return -1;
    if (cg_gp(g, ";\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_step = ") != 0) return -1;
    if (plain_int(g, w, "step", 1) != 0) return -1;
    if (cg_gp(g, ";\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_before = %s;\n", s.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_step < 1) tr_step = 1;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_min > tr_max) { int tr_sw = tr_min; tr_min = tr_max; tr_max = tr_sw; }\n") != 0)
        return -1;
    if (cg_gi(g, d + 1, "nk_slider_int(ctx, tr_min, &%s, tr_max, tr_step);\n", s.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "if (%s != tr_before) {\n", s.lv) != 0) return -1;
    if (call_ev(g, w, "change", d + 2) != 0) return -1;
    if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_leaf_combo(Gen *g, const TrWidget *w, int d)
{
    CgSlot s;
    if (nk_state_int(g, w, "selected", d, &s) != 0) return -1;
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_before = %s;\n", s.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_n = tr_sep_count(") != 0) return -1;
    if (cg_text(g, w, "items") != 0) return -1;
    if (cg_gp(g, ", ';');\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_n < 1) tr_n = 1;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_n > 256) tr_n = 256;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "%s = nk_combo_separator(ctx, ", s.lv) != 0) return -1;
    if (cg_text(g, w, "items") != 0) return -1;
    if (cg_gp(g, ", ';', %s, tr_n, 25, nk_vec2(nk_widget_width(ctx), 200));\n", s.lv) != 0)
        return -1;
    if (cg_gi(g, d + 1, "if (%s != tr_before) {\n", s.lv) != 0) return -1;
    if (call_ev(g, w, "change", d + 2) != 0) return -1;
    if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_leaf_progress(Gen *g, const TrWidget *w, int d)
{
    CgSlot s;
    if (nk_state_int(g, w, "value", d, &s) != 0) return -1;
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_max = ") != 0) return -1;
    if (plain_int(g, w, "max", 100) != 0) return -1;
    if (cg_gp(g, ";\n") != 0) return -1;
    if (cg_gi(g, d + 1, "nk_size tr_cur;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_max < 1) tr_max = 1;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (%s < 0) %s = 0;\n", s.lv, s.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "if (%s > tr_max) %s = tr_max;\n", s.lv, s.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "tr_cur = (nk_size)%s;\n", s.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "nk_progress(ctx, &tr_cur, (nk_size)tr_max, nk_true);\n") != 0) return -1;
    if (cg_gi(g, d + 1, "%s = (int)tr_cur;\n", s.lv) != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_leaf_color(Gen *g, const TrWidget *w, int d)
{
    CgSlot r, gc, b, a;
    if (nk_state_int(g, w, "r", d, &r) != 0) return -1;
    if (nk_state_int(g, w, "g", d, &gc) != 0) return -1;
    if (nk_state_int(g, w, "b", d, &b) != 0) return -1;
    if (nk_state_int(g, w, "a", d, &a) != 0) return -1;
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "struct nk_colorf tr_c;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "tr_c.r = (float)tr_bound_int(%s, 0, 255) / 255.0f;\n", r.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "tr_c.g = (float)tr_bound_int(%s, 0, 255) / 255.0f;\n", gc.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "tr_c.b = (float)tr_bound_int(%s, 0, 255) / 255.0f;\n", b.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "tr_c.a = (float)tr_bound_int(%s, 0, 255) / 255.0f;\n", a.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "tr_c = nk_color_picker(ctx, tr_c, NK_RGBA);\n") != 0) return -1;
    if (cg_gi(g, d + 1, "%s = tr_bound_int((int)(tr_c.r * 255.0f), 0, 255);\n", r.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "%s = tr_bound_int((int)(tr_c.g * 255.0f), 0, 255);\n", gc.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "%s = tr_bound_int((int)(tr_c.b * 255.0f), 0, 255);\n", b.lv) != 0) return -1;
    if (cg_gi(g, d + 1, "%s = tr_bound_int((int)(tr_c.a * 255.0f), 0, 255);\n", a.lv) != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_group(Gen *g, const TrWidget *w, int d, const char *title_key)
{
    int border = tr_prop_int(w, "border", 1);
    if (cg_gi(g, d, "if (nk_group_begin_titled(ctx, \"%s\", ", w->name) != 0) return -1;
    if (cg_text(g, w, title_key) != 0) return -1;
    if (cg_gp(g, ", %s)) {\n", border ? "NK_WINDOW_BORDER|NK_WINDOW_TITLE" : "NK_WINDOW_NO_SCROLLBAR") != 0)
        return -1;
    if (nk_children(g, w->id, d + 1) != 0) return -1;
    if (cg_gi(g, d + 1, "nk_group_end(ctx);\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_tree(Gen *g, const TrWidget *w, int d, const char *kind)
{
    CgSlot s;
    if (nk_state_int(g, w, "open", d, &s) != 0) return -1;
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "enum nk_collapse_states tr_st = %s ? NK_MAXIMIZED : NK_MINIMIZED;\n", s.lv) != 0)
        return -1;
    if (cg_gi(g, d + 1, "if (nk_tree_state_push(ctx, %s, ", kind) != 0) return -1;
    if (cg_text(g, w, "title") != 0) return -1;
    if (cg_gp(g, ", &tr_st)) {\n") != 0) return -1;
    if (nk_children(g, w->id, d + 2) != 0) return -1;
    if (cg_gi(g, d + 2, "nk_tree_pop(ctx);\n") != 0) return -1;
    if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    if (cg_gi(g, d + 1, "%s = (tr_st == NK_MAXIMIZED);\n", s.lv) != 0) return -1;
    if (call_ev(g, w, "toggle", d + 1) != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_tabs(Gen *g, const TrWidget *w, int d)
{
    int kids[TR_WIDGETS_MAX];
    int n;
    int i;
    CgSlot s;
    n = tr_child_indices(g->doc, w->id, kids, TR_WIDGETS_MAX);
    if (n < 0) return -1;
    if (nk_state_int(g, w, "selected", d, &s) != 0) return -1;
    if (n == 0) return 0;
    if (cg_gi(g, d, "if (%s < 0 || %s >= %d) %s = 0;\n", s.lv, s.lv, n, s.lv) != 0) return -1;
    if (cg_gi(g, d, "nk_layout_row_dynamic(ctx, 28, %d);\n", n) != 0) return -1;
    for (i = 0; i < n; i++) {
        const TrWidget *tab = &g->doc->widgets[kids[i]];
        if (cg_gi(g, d, "if (nk_select_label(ctx, ") != 0) return -1;
        if (cg_text(g, tab, "title") != 0) return -1;
        if (cg_gp(g, ", NK_TEXT_CENTERED, %s == %d)) %s = %d;\n", s.lv, i, s.lv, i) != 0) return -1;
    }
    for (i = 0; i < n; i++) {
        const TrWidget *tab = &g->doc->widgets[kids[i]];
        if (cg_gi(g, d, "if (%s == %d) {\n", s.lv, i) != 0) return -1;
        if (nk_children(g, tab->id, d + 1) != 0) return -1;
        if (cg_gi(g, d, "}\n") != 0) return -1;
    }
    return call_ev(g, w, "change", d);
}

static int
nk_table(Gen *g, const TrWidget *w, int d)
{
    const TrProp *cols = tr_prop(w, "columns");
    char parts[8][TR_TEXT_MAX];
    int nc = 0;
    const char *p;
    int flow_n;
    int idxs[TR_WIDGETS_MAX];
    if (cols && cols->bind[0] == '\0') {
        p = cols->value;
        while (*p && nc < 8) {
            const char *semi = strchr(p, ';');
            size_t len = semi ? (size_t)(semi - p) : strlen(p);
            if (len >= TR_TEXT_MAX) return cg_err(g, "a column name is too long");
            memcpy(parts[nc], p, len);
            parts[nc][len] = '\0';
            nc++;
            if (!semi) break;
            p = semi + 1;
        }
    }
    if (nc < 1) {
        memcpy(parts[0], "Value", 6);
        nc = 1;
    }
    flow_n = tr_flow_indices(g->doc, w->id, idxs, TR_WIDGETS_MAX);
    if (flow_n < 0) return -1;
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int tr_rows = ") != 0) return -1;
    if (plain_int(g, w, "rows", 3) != 0) return -1;
    if (cg_gp(g, ";\n") != 0) return -1;
    if (cg_gi(g, d + 1, "int row;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_rows < 0) tr_rows = 0;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_rows > TCCRAD_LOOP_MAX) tr_rows = TCCRAD_LOOP_MAX;\n") != 0) return -1;
    if (tr_prop_int(w, "header", 1)) {
        int c;
        if (cg_gi(g, d + 1, "nk_layout_row_dynamic(ctx, 22, %d);\n", nc) != 0) return -1;
        for (c = 0; c < nc; c++) {
            if (cg_gi(g, d + 1, "nk_label(ctx, ") != 0) return -1;
            if (cg_cstr(g, parts[c]) != 0) return -1;
            if (cg_gp(g, ", NK_TEXT_LEFT);\n") != 0) return -1;
        }
    }
    if (cg_gi(g, d + 1, "for (row = 0; row < tr_rows; row++) {\n") != 0) return -1;
    if (flow_n > 0) {
        if (nk_flow(g, w->id, d + 2) != 0) return -1;
    } else {
        int c;
        if (cg_gi(g, d + 2, "char tr_cell[160];\n") != 0) return -1;
        if (cg_gi(g, d + 2, "nk_layout_row_dynamic(ctx, 22, %d);\n", nc) != 0) return -1;
        for (c = 0; c < nc; c++) {
            if (cg_gi(g, d + 2, "%s_cell(ui, row, %d, tr_cell, sizeof tr_cell);\n",
                    g->doc->class_name, c) != 0)
                return -1;
            if (cg_gi(g, d + 2, "nk_label(ctx, tr_cell, NK_TEXT_LEFT);\n") != 0) return -1;
        }
    }
    if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    if (call_ev(g, w, "select", d + 1) != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_split(Gen *g, const TrWidget *w, int d)
{
    int kids[TR_WIDGETS_MAX];
    int n;
    CgSlot s;
    int vertical;
    const TrProp *orient = tr_prop(w, "orient");
    n = tr_flow_indices(g->doc, w->id, kids, TR_WIDGETS_MAX);
    if (n < 0) return -1;
    vertical = orient && orient->value[0] == 'v';
    if (nk_state_int(g, w, "ratio", d, &s) != 0) return -1;
    if (cg_gi(g, d, "if (%s < 10) %s = 10;\n", s.lv, s.lv) != 0) return -1;
    if (cg_gi(g, d, "if (%s > 90) %s = 90;\n", s.lv, s.lv) != 0) return -1;
    if (!vertical) {
        if (cg_gi(g, d, "nk_layout_row_begin(ctx, NK_DYNAMIC, 180, 2);\n") != 0) return -1;
        if (cg_gi(g, d, "nk_layout_row_push(ctx, (float)%s / 100.0f);\n", s.lv) != 0) return -1;
        if (n > 0 && nk_one(g, &g->doc->widgets[kids[0]], d) != 0) return -1;
        if (cg_gi(g, d, "nk_layout_row_push(ctx, (float)(100 - %s) / 100.0f);\n", s.lv) != 0) return -1;
        if (n > 1 && nk_one(g, &g->doc->widgets[kids[1]], d) != 0) return -1;
        return cg_gi(g, d, "nk_layout_row_end(ctx);\n");
    }
    if (n > 0 && nk_one(g, &g->doc->widgets[kids[0]], d) != 0) return -1;
    if (n > 1 && nk_one(g, &g->doc->widgets[kids[1]], d) != 0) return -1;
    return 0;
}

static int
nk_menu(Gen *g, const TrWidget *bar, int d)
{
    int menus[TR_WIDGETS_MAX];
    int nm;
    int i;
    nm = tr_child_indices(g->doc, bar->id, menus, TR_WIDGETS_MAX);
    if (nm < 0) return -1;
    if (nm == 0) return 0;
    if (cg_gi(g, d, "nk_menubar_begin(ctx);\n") != 0) return -1;
    if (cg_gi(g, d, "nk_layout_row_begin(ctx, NK_STATIC, 25, %d);\n", nm) != 0) return -1;
    for (i = 0; i < nm; i++) {
        const TrWidget *menu = &g->doc->widgets[menus[i]];
        int items[TR_WIDGETS_MAX];
        int ni;
        int k;
        ni = tr_child_indices(g->doc, menu->id, items, TR_WIDGETS_MAX);
        if (ni < 0) return -1;
        if (cg_gi(g, d, "nk_layout_row_push(ctx, 120);\n") != 0) return -1;
        if (cg_gi(g, d, "if (nk_menu_begin_label(ctx, ") != 0) return -1;
        if (cg_text(g, menu, "label") != 0) return -1;
        if (cg_gp(g, ", NK_TEXT_LEFT, nk_vec2(180, 240))) {\n") != 0) return -1;
        if (cg_gi(g, d + 1, "nk_layout_row_dynamic(ctx, 25, 1);\n") != 0) return -1;
        for (k = 0; k < ni; k++) {
            const TrWidget *it = &g->doc->widgets[items[k]];
            if (cg_gi(g, d + 1, "if (nk_menu_item_label(ctx, ") != 0) return -1;
            if (cg_text(g, it, "label") != 0) return -1;
            if (cg_gp(g, ", NK_TEXT_LEFT)) {\n") != 0) return -1;
            if (call_ev(g, it, "click", d + 2) != 0) return -1;
            if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        }
        if (cg_gi(g, d + 1, "nk_menu_end(ctx);\n") != 0) return -1;
        if (cg_gi(g, d, "}\n") != 0) return -1;
    }
    if (cg_gi(g, d, "nk_layout_row_end(ctx);\n") != 0) return -1;
    return cg_gi(g, d, "nk_menubar_end(ctx);\n");
}

static int
nk_popup(Gen *g, const TrWidget *w, int d)
{
    CgSlot s;
    int ww = w->w > 0 ? w->w : 320;
    int hh = w->h > 0 ? w->h : 200;
    if (nk_state_int(g, w, "open", d, &s) != 0) return -1;
    if (cg_gi(g, d, "if (%s && nk_popup_begin(ctx, NK_POPUP_STATIC, \"%s\", ", s.lv, w->name) != 0)
        return -1;
    if (win_flags(g, w) != 0) return -1;
    if (cg_gp(g, ", nk_rect((float)tr_u(ui, %d), (float)tr_u(ui, %d), (float)tr_u(ui, %d), (float)tr_u(ui, %d)))) {\n",
            w->x, w->y, ww, hh) != 0)
        return -1;
    if (nk_children(g, w->id, d + 1) != 0) return -1;
    if (cg_gi(g, d + 1, "nk_popup_end(ctx);\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_overlay(Gen *g, const TrWidget *w, int d)
{
    int ww = w->w > 0 ? w->w : 120;
    int hh = w->h > 0 ? w->h : 28;
    if (cg_gi(g, d, "{\n") != 0) return -1;
    if (cg_gi(g, d + 1, "float tr_pw = nk_window_get_content_region(ctx).w;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "float tr_ph = nk_window_get_content_region(ctx).h;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "float tr_w = (float)tr_u(ui, %d);\n", ww) != 0) return -1;
    if (cg_gi(g, d + 1, "float tr_h = (float)tr_u(ui, %d);\n", hh) != 0) return -1;
    if (cg_gi(g, d + 1, "float tr_x = (float)tr_u(ui, %d);\n", w->x) != 0) return -1;
    if (cg_gi(g, d + 1, "float tr_y = (float)tr_u(ui, %d);\n", w->y) != 0) return -1;
    if (w->anchor == 1 || w->anchor == 3) {
        if (cg_gi(g, d + 1, "tr_x = tr_pw - tr_x - tr_w;\n") != 0) return -1;
    }
    if (w->anchor == 2 || w->anchor == 3) {
        if (cg_gi(g, d + 1, "tr_y = tr_ph - tr_y - tr_h;\n") != 0) return -1;
    }
    if (cg_gi(g, d + 1, "if (tr_x < 0.0f) tr_x = 0.0f;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (tr_y < 0.0f) tr_y = 0.0f;\n") != 0) return -1;
    if (cg_gi(g, d + 1, "nk_layout_space_begin(ctx, NK_STATIC, tr_h, 1);\n") != 0) return -1;
    if (cg_gi(g, d + 1, "nk_layout_space_push(ctx, nk_rect(tr_x, tr_y, tr_w, tr_h));\n") != 0) return -1;
    if (nk_one(g, w, d + 1) != 0) return -1;
    if (cg_gi(g, d + 1, "nk_layout_space_end(ctx);\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

static int
nk_body(Gen *g, const TrWidget *w, int d)
{
    if (d > 32) return cg_err(g, "widget nesting is too deep");
    switch (w->kind) {
    case TR_KIND_LABEL: return nk_leaf_label(g, w, d);
    case TR_KIND_BUTTON: return nk_leaf_button(g, w, d);
    case TR_KIND_CHECKBOX: return nk_leaf_check(g, w, d);
    case TR_KIND_RADIO: return nk_leaf_radio(g, w, d);
    case TR_KIND_INPUT: return nk_leaf_input(g, w, d);
    case TR_KIND_SLIDER: return nk_leaf_slider(g, w, d);
    case TR_KIND_COMBO: return nk_leaf_combo(g, w, d);
    case TR_KIND_PROGRESS: return nk_leaf_progress(g, w, d);
    case TR_KIND_COLOR: return nk_leaf_color(g, w, d);
    case TR_KIND_SEPARATOR:
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "struct nk_rect tr_b;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (nk_widget(&tr_b, ctx) != NK_WIDGET_INVALID) {\n") != 0) return -1;
        if (cg_gi(g, d + 2, "struct nk_command_buffer *tr_cv = nk_window_get_canvas(ctx);\n") != 0) return -1;
        if (cg_gi(g, d + 2, "float tr_y = tr_b.y + tr_b.h * 0.5f;\n") != 0) return -1;
        if (cg_gi(g, d + 2, "nk_stroke_line(tr_cv, tr_b.x, tr_y, tr_b.x + tr_b.w, tr_y, 1.0f, nk_rgb(120,120,120));\n") != 0)
            return -1;
        if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_SPACER:
        return cg_gi(g, d, "nk_label(ctx, \"\", NK_TEXT_LEFT);\n");
    case TR_KIND_GROUP: return nk_group(g, w, d, "title");
    case TR_KIND_CHILD: return nk_group(g, w, d, "title");
    case TR_KIND_DOCK: return nk_group(g, w, d, "title");
    case TR_KIND_COLLAPSE: return nk_tree(g, w, d, "NK_TREE_TAB");
    case TR_KIND_TREE: return nk_tree(g, w, d, "NK_TREE_NODE");
    case TR_KIND_TABS: return nk_tabs(g, w, d);
    case TR_KIND_TAB: return nk_children(g, w->id, d);
    case TR_KIND_TABLE: return nk_table(g, w, d);
    case TR_KIND_SPLITTER: return nk_split(g, w, d);
    case TR_KIND_IMAGE:
        if (cg_gi(g, d, "nk_label(ctx, ") != 0) return -1;
        if (cg_text(g, w, "path") != 0) return -1;
        return cg_gp(g, ", NK_TEXT_LEFT);\n");
    case TR_KIND_CUSTOM: {
        char hn[TR_NAME_MAX];
        const TrEvent *ev = cg_event(w, "draw");
        if (!ev || cg_handler(g->doc, w, ev, hn, sizeof hn) != 0) return cg_err(g, "custom widget needs a draw event");
        return cg_gi(g, d, "%s(ui, ctx);\n", hn);
    }
    case TR_KIND_POPUP:
    case TR_KIND_MODAL:
        return nk_popup(g, w, d);
    case TR_KIND_MENUBAR:
        return nk_menu(g, w, d);
    case TR_KIND_MENU:
    case TR_KIND_MENUITEM:
        return 0;
    case TR_KIND_WINDOW:
    case TR_KIND_MAIN:
    case TR_KIND_ACTIVITY:
        return nk_children(g, w->id, d);
    case TR_KIND_NONE:
        break;
    }
    return cg_err(g, "widget kind is not supported");
}

static int
nk_one(Gen *g, const TrWidget *w, int d)
{
    int rc;
    if (w->config[0] != '\0') {
        if (cg_gi(g, d, "if (tr_cfg_on(ui, ") != 0) return -1;
        if (cg_cstr(g, w->config) != 0) return -1;
        if (cg_gp(g, ")) {\n") != 0) return -1;
        rc = nk_body(g, w, d + 1);
        if (cg_gi(g, d, "}\n") != 0) return -1;
        return rc;
    }
    return nk_body(g, w, d);
}

static int
nk_children(Gen *g, int parent, int d)
{
    int idxs[TR_WIDGETS_MAX];
    int n;
    int i;
    n = tr_child_indices(g->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n < 0) return cg_err(g, "too many widgets");
    for (i = 0; i < n; i++) {
        const TrWidget *w = &g->doc->widgets[idxs[i]];
        if (w->kind == TR_KIND_MENUBAR && nk_menu(g, w, d) != 0) return -1;
    }
    if (nk_flow(g, parent, d) != 0) return -1;
    for (i = 0; i < n; i++) {
        const TrWidget *w = &g->doc->widgets[idxs[i]];
        if (w->overlay && w->kind != TR_KIND_POPUP && w->kind != TR_KIND_MODAL &&
            w->kind != TR_KIND_MENUBAR) {
            if (nk_overlay(g, w, d) != 0) return -1;
        }
    }
    for (i = 0; i < n; i++) {
        const TrWidget *w = &g->doc->widgets[idxs[i]];
        if ((w->kind == TR_KIND_POPUP || w->kind == TR_KIND_MODAL) && nk_popup(g, w, d) != 0)
            return -1;
    }
    return 0;
}

static int
nk_begin_root(Gen *g, const TrWidget *w, int d)
{
    int ww = w->w > 64 ? w->w : 480;
    int hh = w->h > 64 ? w->h : 320;
    CgSlot open;
    if (cg_int_slot(g, w, "open", &open) != 0) return -1;
    if (w->kind == TR_KIND_POPUP || w->kind == TR_KIND_MODAL) {
        if (cg_gi(g, d, "if (nk_begin_titled(ctx, \"tr_host\", \"\", nk_rect(0, 0, vw > 1.0f ? vw : 640.0f, vh > 1.0f ? vh : 480.0f), NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BACKGROUND)) {\n") != 0)
            return -1;
        if (nk_popup(g, w, d + 1) != 0) return -1;
        if (cg_gi(g, d, "}\n") != 0) return -1;
        if (cg_gi(g, d, "nk_end(ctx);\n") != 0) return -1;
        return 0;
    }
    if (cg_gi(g, d, "if (%s) {\n", open.lv) != 0) return -1;
    if (w->kind == TR_KIND_ACTIVITY) {
        if (cg_gi(g, d + 1, "if (nk_begin_titled(ctx, \"%s\", ", w->name) != 0) return -1;
        if (cg_text(g, w, "title") != 0) return -1;
        if (cg_gp(g, ", nk_rect(0, 0, vw > 1.0f ? vw : (float)tr_u(ui, %d), vh > 1.0f ? vh : (float)tr_u(ui, %d)), ",
                ww, hh) != 0)
            return -1;
    } else {
        if (cg_gi(g, d + 1, "if (nk_begin_titled(ctx, \"%s\", ", w->name) != 0) return -1;
        if (cg_text(g, w, "title") != 0) return -1;
        if (cg_gp(g, ", nk_rect((float)tr_u(ui, %d), (float)tr_u(ui, %d), (float)tr_u(ui, %d), (float)tr_u(ui, %d)), ",
                w->x, w->y, ww, hh) != 0)
            return -1;
    }
    if (win_flags(g, w) != 0) return -1;
    if (cg_gp(g, ")) {\n") != 0) return -1;
    if (call_ev(g, w, "appear", d + 2) != 0) return -1;
    if (nk_children(g, w->id, d + 2) != 0) return -1;
    if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    if (cg_gi(g, d + 1, "nk_end(ctx);\n") != 0) return -1;
    if (cg_gi(g, d + 1, "if (nk_window_is_hidden(ctx, \"%s\")) {\n", w->name) != 0) return -1;
    if (cg_gi(g, d + 2, "%s = 0;\n", open.lv) != 0) return -1;
    if (call_ev(g, w, "close", d + 2) != 0) return -1;
    if (cg_gi(g, d + 1, "}\n") != 0) return -1;
    return cg_gi(g, d, "}\n");
}

int
cg_nk(Gen *g)
{
    const char *cls = g->doc->class_name;
    const TrWidget *root = &g->doc->widgets[0];
    CgSlot open;
    if (cg_int_slot(g, root, "open", &open) != 0) return -1;
    if (cg_gp(g, "int %s_frame(struct nk_context *ctx, %s *ui, float vw, float vh)\n{\n", cls, cls) != 0)
        return -1;
    if (cg_gi(g, 1, "if (!ctx || !ui) return 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "(void)vw; (void)vh;\n") != 0) return -1;
    if (cg_gi(g, 1, "%s_user_pre(ui);\n", cls) != 0) return -1;
    if (nk_begin_root(g, root, 1) != 0) return -1;
    if (cg_gi(g, 1, "%s_user_post(ui);\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "return %s;\n", open.lv) != 0) return -1;
    if (cg_gp(g, "}\n\n") != 0) return -1;
    if (cg_gp(g, "int %s_build(%s *ui, void *parent)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "(void)ui; (void)parent;\n") != 0) return -1;
    if (cg_gi(g, 1, "return -1;\n}\n") != 0) return -1;
    return 0;
}
