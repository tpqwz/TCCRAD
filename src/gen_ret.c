#include "codegen_priv.h"

#include <stdio.h>
#include <string.h>

static int ui_children(Gen *g, int parent, const char *box, int d);
static int lv_children(Gen *g, int parent, const char *obj, int d);

static int
stretch_of(const TrWidget *w)
{
    if (w && (w->stretch > 0 || w->w < 0 || w->h < 0)) return 1;
    return 0;
}

static int
call_named(Gen *g, const TrWidget *w, const char *ev, int d)
{
    const TrEvent *e = cg_event(w, ev);
    char hn[TR_NAME_MAX];
    if (!e) return 0;
    if (cg_handler(g->doc, w, e, hn, sizeof hn) != 0) return cg_err(g, "handler name is invalid");
    return cg_gi(g, d, "%s(ui);\n", hn);
}

static int
text_buf(const TrDoc *doc, const TrWidget *w, char *buf, size_t cap, char *lenm, size_t lcap)
{
    const TrProp *tx = tr_prop(w, "text");
    int nchars = 0;
    int n;
    if (tx && tx->twoway) {
        const TrField *f = tr_field_find(doc, tx->bind);
        if (!f || tr_field_type(f->type, &nchars) != TR_FT_CHARS) return -1;
        n = snprintf(buf, cap, "ui->%s", f->name);
        if (n < 0 || (size_t)n >= cap) return -1;
        n = snprintf(lenm, lcap, "ui->%s_len", f->name);
        return (n < 0 || (size_t)n >= lcap) ? -1 : 0;
    }
    n = snprintf(buf, cap, "ui->tr_p_%d_text", w->id);
    if (n < 0 || (size_t)n >= cap) return -1;
    n = snprintf(lenm, lcap, "ui->tr_p_%d_text_n", w->id);
    return (n < 0 || (size_t)n >= lcap) ? -1 : 0;
}

static int
split_list(const char *s, char parts[][TR_TEXT_MAX], int max, int *out)
{
    int n = 0;
    if (!s) s = "";
    while (*s && n < max) {
        const char *semi = strchr(s, ';');
        size_t len = semi ? (size_t)(semi - s) : strlen(s);
        if (len >= TR_TEXT_MAX) return -1;
        memcpy(parts[n], s, len);
        parts[n][len] = '\0';
        n++;
        if (!semi) break;
        s = semi + 1;
    }
    *out = n;
    return 0;
}

/* Column titles from the literal "columns" property. A binding falls back to one column. */
static int
column_names(const TrWidget *w, char parts[][TR_TEXT_MAX], int max, int *nc)
{
    const TrProp *cols = tr_prop(w, "columns");
    int n = 0;
    if (!nc || max < 1) return -1;
    if (cols && cols->bind[0] == '\0') {
        if (split_list(cols->value[0] ? cols->value : "Value", parts, max, &n) != 0) return -1;
    }
    if (n < 1) {
        memcpy(parts[0], "Value", 6);
        n = 1;
    }
    *nc = n;
    return 0;
}

static int
ui_cbs(Gen *g)
{
    const char *cls = g->doc->class_name;
    int i;
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *w = &g->doc->widgets[i];
        CgSlot s;
        if (w->kind == TR_KIND_BUTTON) {
            if (cg_gp(g, "static void tr_uicb_%d(uiButton *sender, void *data)\n{\n", w->id) != 0) return -1;
            if (cg_gi(g, 1, "%s *ui = (%s *)data;\n", cls, cls) != 0) return -1;
            if (cg_gi(g, 1, "(void)sender;\n") != 0) return -1;
            if (cg_gi(g, 1, "if (!ui) return;\n") != 0) return -1;
            if (call_named(g, w, "click", 1) != 0) return -1;
            if (cg_gp(g, "}\n") != 0) return -1;
        } else if (w->kind == TR_KIND_MENUITEM) {
            if (cg_gp(g, "static void tr_uimenu_%d(uiMenuItem *sender, uiWindow *window, void *data)\n{\n", w->id) != 0)
                return -1;
            if (cg_gi(g, 1, "%s *ui = (%s *)data;\n", cls, cls) != 0) return -1;
            if (cg_gi(g, 1, "(void)sender; (void)window;\n") != 0) return -1;
            if (cg_gi(g, 1, "if (!ui) return;\n") != 0) return -1;
            if (call_named(g, w, "click", 1) != 0) return -1;
            if (cg_gp(g, "}\n") != 0) return -1;
        } else if (w->kind == TR_KIND_CHECKBOX) {
            if (cg_int_slot(g, w, "checked", &s) != 0) return -1;
            if (cg_gp(g, "static void tr_uicb_%d(uiCheckbox *sender, void *data)\n{\n", w->id) != 0) return -1;
            if (cg_gi(g, 1, "%s *ui = (%s *)data;\n", cls, cls) != 0) return -1;
            if (cg_gi(g, 1, "if (!ui || !sender) return;\n") != 0) return -1;
            if (cg_gi(g, 1, "%s = uiCheckboxChecked(sender);\n", s.lv) != 0) return -1;
            if (call_named(g, w, "toggle", 1) != 0) return -1;
            if (cg_gp(g, "}\n") != 0) return -1;
        } else if (w->kind == TR_KIND_INPUT) {
            char bbuf[64], lbuf[64];
            if (text_buf(g->doc, w, bbuf, sizeof bbuf, lbuf, sizeof lbuf) != 0) return -1;
            if (tr_prop_int(w, "multiline", 0))
                cg_gp(g, "static void tr_uicb_%d(uiMultilineEntry *sender, void *data)\n{\n", w->id);
            else
                cg_gp(g, "static void tr_uicb_%d(uiEntry *sender, void *data)\n{\n", w->id);
            if (cg_gi(g, 1, "%s *ui = (%s *)data;\n", cls, cls) != 0) return -1;
            if (cg_gi(g, 1, "char *tr_t;\n") != 0) return -1;
            if (cg_gi(g, 1, "if (!ui || !sender) return;\n") != 0) return -1;
            if (tr_prop_int(w, "multiline", 0)) {
                if (cg_gi(g, 1, "tr_t = uiMultilineEntryText(sender);\n") != 0) return -1;
            } else if (cg_gi(g, 1, "tr_t = uiEntryText(sender);\n") != 0) return -1;
            if (cg_gi(g, 1, "if (!tr_t) return;\n") != 0) return -1;
            if (cg_gi(g, 1, "tr_copy(%s, sizeof %s, tr_t);\n", bbuf, bbuf) != 0) return -1;
            if (cg_gi(g, 1, "%s = tr_cstr_len(%s, (int)sizeof %s);\n", lbuf, bbuf, bbuf) != 0) return -1;
            if (cg_gi(g, 1, "uiFreeText(tr_t);\n") != 0) return -1;
            if (call_named(g, w, "change", 1) != 0) return -1;
            if (cg_gp(g, "}\n") != 0) return -1;
        } else if (w->kind == TR_KIND_SLIDER || w->kind == TR_KIND_COMBO ||
                   w->kind == TR_KIND_RADIO || w->kind == TR_KIND_COLOR) {
            const char *sig = "uiSlider";
            const char *getter = "uiSliderValue";
            const char *key = "value";
            const char *ev = "change";
            if (w->kind == TR_KIND_COMBO) { sig = "uiCombobox"; getter = "uiComboboxSelected"; key = "selected"; }
            if (w->kind == TR_KIND_RADIO) { sig = "uiRadioButtons"; getter = "uiRadioButtonsSelected"; key = "selected"; }
            if (w->kind == TR_KIND_COLOR) { sig = "uiSpinbox"; getter = "uiSpinboxValue"; key = "r"; ev = NULL; }
            if (cg_int_slot(g, w, key, &s) != 0) return -1;
            if (cg_gp(g, "static void tr_uicb_%d(%s *sender, void *data)\n{\n", w->id, sig) != 0) return -1;
            if (cg_gi(g, 1, "%s *ui = (%s *)data;\n", cls, cls) != 0) return -1;
            if (cg_gi(g, 1, "if (!ui || !sender) return;\n") != 0) return -1;
            if (cg_gi(g, 1, "%s = %s(sender);\n", s.lv, getter) != 0) return -1;
            if (ev && call_named(g, w, ev, 1) != 0) return -1;
            if (cg_gp(g, "}\n") != 0) return -1;
        }
    }
    if (cg_gp(g, "static int tr_ui_close(uiWindow *sender, void *data)\n{\n") != 0) return -1;
    if (cg_gi(g, 1, "%s *ui = (%s *)data;\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "(void)sender;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (ui) ui->tr_open = 0;\n") != 0) return -1;
    if (call_named(g, &g->doc->widgets[0], "close", 1) != 0) return -1;
    if (cg_gi(g, 1, "uiQuit();\n") != 0) return -1;
    return cg_gi(g, 1, "return 1;\n}\n\n");
}

static int
ui_box_append(Gen *g, const char *box, const char *ctl, int stretch, int d)
{
    return cg_gi(g, d, "uiBoxAppend(%s, uiControl(%s), %d);\n", box, ctl, stretch);
}

static int
ui_widget(Gen *g, const TrWidget *w, const char *box, int d)
{
    char var[32];
    int st = stretch_of(w);
    CgSlot s;
    if (d > 32) return cg_err(g, "widget nesting is too deep");
    if (snprintf(var, sizeof var, "tr_w_%d", w->id) < 0) return -1;
    switch (w->kind) {
    case TR_KIND_LABEL:
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiLabel *%s = uiNewLabel(", var) != 0) return -1;
        if (cg_text(g, w, "text") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (ui_box_append(g, box, var, st, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_BUTTON:
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiButton *%s = uiNewButton(", var) != 0) return -1;
        if (cg_text(g, w, "label") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "uiButtonOnClicked(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        if (ui_box_append(g, box, var, st, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_CHECKBOX:
        if (cg_int_slot(g, w, "checked", &s) != 0) return -1;
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiCheckbox *%s = uiNewCheckbox(", var) != 0) return -1;
        if (cg_text(g, w, "label") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "uiCheckboxSetChecked(%s, %s);\n", var, s.lv) != 0) return -1;
        if (cg_gi(g, d + 1, "uiCheckboxOnToggled(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        if (ui_box_append(g, box, var, st, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_INPUT: {
        char bbuf[64], lbuf[64];
        int multi = tr_prop_int(w, "multiline", 0);
        int password = tr_prop_int(w, "password", 0);
        if (text_buf(g->doc, w, bbuf, sizeof bbuf, lbuf, sizeof lbuf) != 0) return -1;
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (multi) {
            if (cg_gi(g, d + 1, "uiMultilineEntry *%s = uiNewMultilineEntry();\n", var) != 0) return -1;
            if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
            if (cg_gi(g, d + 1, "uiMultilineEntrySetText(%s, %s);\n", var, bbuf) != 0) return -1;
            if (cg_gi(g, d + 1, "uiMultilineEntryOnChanged(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        } else {
            if (cg_gi(g, d + 1, "uiEntry *%s = %s();\n", var, password ? "uiNewPasswordEntry" : "uiNewEntry") != 0)
                return -1;
            if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
            if (cg_gi(g, d + 1, "uiEntrySetText(%s, %s);\n", var, bbuf) != 0) return -1;
            if (cg_gi(g, d + 1, "uiEntryOnChanged(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        }
        if (ui_box_append(g, box, var, st, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_SLIDER:
        if (cg_int_slot(g, w, "value", &s) != 0) return -1;
        if (cg_gi(g, d, "{ uiSlider *%s = uiNewSlider(0, 100);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "uiSliderSetValue(%s, tr_bound_int(%s, 0, 100));\n", var, s.lv) != 0) return -1;
        if (cg_gi(g, d + 1, "uiSliderOnChanged(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        if (ui_box_append(g, box, var, 1, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_COMBO:
    case TR_KIND_RADIO: {
        char parts[16][TR_TEXT_MAX];
        int n = 0;
        int i;
        const TrProp *opt = tr_prop(w, w->kind == TR_KIND_COMBO ? "items" : "options");
        if (cg_int_slot(g, w, "selected", &s) != 0) return -1;
        if (split_list(opt ? opt->value : "", parts, 16, &n) != 0) return cg_err(g, "option list is too long");
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (w->kind == TR_KIND_COMBO) {
            if (cg_gi(g, d + 1, "uiCombobox *%s = uiNewCombobox();\n", var) != 0) return -1;
        } else if (cg_gi(g, d + 1, "uiRadioButtons *%s = uiNewRadioButtons();\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        for (i = 0; i < n; i++) {
            if (w->kind == TR_KIND_COMBO) {
                if (cg_gi(g, d + 1, "uiComboboxAppend(%s, ", var) != 0) return -1;
            } else if (cg_gi(g, d + 1, "uiRadioButtonsAppend(%s, ", var) != 0) return -1;
            if (cg_cstr(g, parts[i]) != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
        }
        if (w->kind == TR_KIND_COMBO) {
            if (cg_gi(g, d + 1, "uiComboboxSetSelected(%s, %s);\n", var, s.lv) != 0) return -1;
            if (cg_gi(g, d + 1, "uiComboboxOnSelected(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        } else {
            if (cg_gi(g, d + 1, "uiRadioButtonsSetSelected(%s, %s);\n", var, s.lv) != 0) return -1;
            if (cg_gi(g, d + 1, "uiRadioButtonsOnSelected(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        }
        if (ui_box_append(g, box, var, st, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_PROGRESS:
        if (cg_int_slot(g, w, "value", &s) != 0) return -1;
        if (cg_gi(g, d, "{ uiProgressBar *%s = uiNewProgressBar();\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "int tr_v = tr_bound_int(%s, 0, 100);\n", s.lv) != 0) return -1;
        if (cg_gi(g, d + 1, "uiProgressBarSetValue(%s, tr_v);\n", var) != 0) return -1;
        if (ui_box_append(g, box, var, 1, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_COLOR:
        if (cg_int_slot(g, w, "r", &s) != 0) return -1;
        if (cg_gi(g, d, "{ uiSpinbox *%s = uiNewSpinbox(0, 255);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "uiSpinboxSetValue(%s, tr_bound_int(%s, 0, 255));\n", var, s.lv) != 0) return -1;
        if (cg_gi(g, d + 1, "uiSpinboxOnChanged(%s, tr_uicb_%d, ui);\n", var, w->id) != 0) return -1;
        if (ui_box_append(g, box, var, 0, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_SEPARATOR:
        return cg_gi(g, d, "uiBoxAppend(%s, uiControl(uiNewHorizontalSeparator()), 0);\n", box);
    case TR_KIND_SPACER:
        return cg_gi(g, d, "uiBoxAppend(%s, uiControl(uiNewLabel(\"\")), 1);\n", box);
    case TR_KIND_IMAGE:
        if (cg_gi(g, d, "uiBoxAppend(%s, uiControl(uiNewLabel(", box) != 0) return -1;
        if (cg_text(g, w, "path") != 0) return -1;
        return cg_gp(g, ")), 0);\n");
    case TR_KIND_CUSTOM: {
        char hn[TR_NAME_MAX];
        const TrEvent *ev = cg_event(w, "draw");
        if (!ev || cg_handler(g->doc, w, ev, hn, sizeof hn) != 0) return -1;
        if (cg_gi(g, d, "uiBoxAppend(%s, uiControl(uiNewLabel(\"[custom]\")), 0);\n", box) != 0) return -1;
        return cg_gi(g, d, "%s(ui, (void *)%s);\n", hn, box);
    }
    case TR_KIND_GROUP:
    case TR_KIND_CHILD:
    case TR_KIND_DOCK:
    case TR_KIND_COLLAPSE:
    case TR_KIND_TREE: {
        char inner[32];
        if (snprintf(inner, sizeof inner, "tr_box_%d", w->id) < 0) return -1;
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiGroup *tr_g_%d = uiNewGroup(", w->id) != 0) return -1;
        if (cg_text(g, w, "title") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiBox *%s = uiNewVerticalBox();\n", inner) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!tr_g_%d || !%s) return -1;\n", w->id, inner) != 0) return -1;
        if (cg_gi(g, d + 1, "uiBoxSetPadded(%s, 1);\n", inner) != 0) return -1;
        if (cg_gi(g, d + 1, "uiGroupSetMargined(tr_g_%d, 1);\n", w->id) != 0) return -1;
        if (cg_gi(g, d + 1, "uiGroupSetChild(tr_g_%d, uiControl(%s));\n", w->id, inner) != 0) return -1;
        if (cg_gi(g, d + 1, "uiBoxAppend(%s, uiControl(tr_g_%d), %d);\n", box, w->id, st) != 0) return -1;
        if (ui_children(g, w->id, inner, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_TABS: {
        int kids[TR_WIDGETS_MAX];
        int n = tr_child_indices(g->doc, w->id, kids, TR_WIDGETS_MAX);
        int i;
        if (n < 0) return -1;
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiTab *tr_tab_%d = uiNewTab();\n", w->id) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!tr_tab_%d) return -1;\n", w->id) != 0) return -1;
        for (i = 0; i < n; i++) {
            const TrWidget *tab = &g->doc->widgets[kids[i]];
            if (cg_gi(g, d + 1, "{\n") != 0) return -1;
            if (cg_gi(g, d + 2, "uiBox *tr_page_%d = uiNewVerticalBox();\n", tab->id) != 0) return -1;
            if (cg_gi(g, d + 2, "if (!tr_page_%d) return -1;\n", tab->id) != 0) return -1;
            if (cg_gi(g, d + 2, "uiTabAppend(tr_tab_%d, ", w->id) != 0) return -1;
            if (cg_text(g, tab, "title") != 0) return -1;
            if (cg_gp(g, ", uiControl(tr_page_%d));\n", tab->id) != 0) return -1;
            {
                char page[32];
                if (snprintf(page, sizeof page, "tr_page_%d", tab->id) < 0) return -1;
                if (ui_children(g, tab->id, page, d + 2) != 0) return -1;
            }
            if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        }
        if (cg_gi(g, d + 1, "uiBoxAppend(%s, uiControl(tr_tab_%d), 1);\n", box, w->id) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_TABLE: {
        char parts[8][TR_TEXT_MAX];
        int nc = 0;
        int c;
        int head = tr_prop_int(w, "header", 1) ? 1 : 0;
        if (column_names(w, parts, 8, &nc) != 0) return -1;
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "int tr_rows = ") != 0) return -1;
        {
            const TrProp *p = tr_prop(w, "rows");
            char rew[512];
            if (p && p->bind[0] && !p->twoway) {
                if (tr_rewrite_expr(g->doc, p->bind, rew, sizeof rew) != 0) return -1;
                if (cg_gp(g, "(%s);\n", rew) != 0) return -1;
            } else if (cg_gp(g, "%d;\n", tr_prop_int(w, "rows", 3)) != 0) return -1;
        }
        if (cg_gi(g, d + 1, "int tr_cols = %d;\n", nc) != 0) return -1;
        if (cg_gi(g, d + 1, "int row, col;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (tr_rows < 0) tr_rows = 0;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (tr_rows > 64) tr_rows = 64;\n") != 0) return -1;
        if (head) {
            if (cg_gi(g, d + 1, "{\n") != 0) return -1;
            if (cg_gi(g, d + 2, "uiBox *tr_hdr = uiNewHorizontalBox();\n") != 0) return -1;
            if (cg_gi(g, d + 2, "if (!tr_hdr) return -1;\n") != 0) return -1;
            for (c = 0; c < nc; c++) {
                if (cg_gi(g, d + 2, "uiBoxAppend(tr_hdr, uiControl(uiNewLabel(") != 0) return -1;
                if (cg_cstr(g, parts[c]) != 0) return -1;
                if (cg_gp(g, ")), 1);\n") != 0) return -1;
            }
            if (cg_gi(g, d + 2, "uiBoxAppend(%s, uiControl(tr_hdr), 0);\n", box) != 0) return -1;
            if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        }
        if (cg_gi(g, d + 1, "for (row = 0; row < tr_rows; row++) {\n") != 0) return -1;
        if (cg_gi(g, d + 2, "uiBox *tr_line = uiNewHorizontalBox();\n") != 0) return -1;
        if (cg_gi(g, d + 2, "if (!tr_line) return -1;\n") != 0) return -1;
        if (cg_gi(g, d + 2, "for (col = 0; col < tr_cols; col++) {\n") != 0) return -1;
        if (cg_gi(g, d + 3, "char tr_cell[160];\n") != 0) return -1;
        if (cg_gi(g, d + 3, "%s_cell(ui, row, col, tr_cell, sizeof tr_cell);\n", g->doc->class_name) != 0)
            return -1;
        if (cg_gi(g, d + 3, "uiBoxAppend(tr_line, uiControl(uiNewLabel(tr_cell)), 1);\n") != 0) return -1;
        if (cg_gi(g, d + 2, "}\n") != 0) return -1;
        if (cg_gi(g, d + 2, "uiBoxAppend(%s, uiControl(tr_line), 0);\n", box) != 0) return -1;
        if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_SPLITTER: {
        char split[32];
        if (snprintf(split, sizeof split, "tr_split_%d", w->id) < 0) return -1;
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiBox *%s = uiNewHorizontalBox();\n", split) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", split) != 0) return -1;
        if (cg_gi(g, d + 1, "uiBoxSetPadded(%s, 1);\n", split) != 0) return -1;
        if (ui_children(g, w->id, split, d + 1) != 0) return -1;
        if (cg_gi(g, d + 1, "uiBoxAppend(%s, uiControl(%s), 1);\n", box, split) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_POPUP:
    case TR_KIND_MODAL: {
        char popbox[32];
        if (snprintf(popbox, sizeof popbox, "tr_box_%d", w->id) < 0) return -1;
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiWindow *tr_pop_%d = uiNewWindow(", w->id) != 0) return -1;
        if (cg_text(g, w, "title") != 0) return -1;
        if (cg_gp(g, ", %d, %d, 0);\n", w->w > 64 ? w->w : 320, w->h > 64 ? w->h : 200) != 0) return -1;
        if (cg_gi(g, d + 1, "uiBox *%s = uiNewVerticalBox();\n", popbox) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!tr_pop_%d || !%s) return -1;\n", w->id, popbox) != 0) return -1;
        if (cg_gi(g, d + 1, "uiWindowSetMargined(tr_pop_%d, 1);\n", w->id) != 0) return -1;
        if (cg_gi(g, d + 1, "uiWindowSetChild(tr_pop_%d, uiControl(%s));\n", w->id, popbox) != 0) return -1;
        if (ui_children(g, w->id, popbox, d + 1) != 0) return -1;
        if (cg_gi(g, d + 1, "uiControlShow(uiControl(tr_pop_%d));\n", w->id) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_TAB:
        return ui_children(g, w->id, box, d);
    case TR_KIND_MENU:
    case TR_KIND_MENUITEM:
    case TR_KIND_MENUBAR:
        return 0;
    default:
        return cg_gi(g, d, "uiBoxAppend(%s, uiControl(uiNewLabel(\"%s\")), 0);\n", box, tr_kind_name(w->kind));
    }
}

static int
ui_rows(Gen *g, int parent, const char *box, int d)
{
    int idxs[TR_WIDGETS_MAX];
    TrRow rows[TR_WIDGETS_MAX];
    int n = tr_flow_indices(g->doc, parent, idxs, TR_WIDGETS_MAX);
    int nr;
    int r;
    int c;
    if (n < 0) return -1;
    if (n == 0) return 0;
    nr = tr_pack_rows(g->doc, idxs, n, rows, TR_WIDGETS_MAX);
    if (nr < 0) return -1;
    for (r = 0; r < nr; r++) {
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiBox *tr_row = uiNewHorizontalBox();\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (!tr_row) return -1;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiBoxSetPadded(tr_row, 1);\n") != 0) return -1;
        if (cg_gi(g, d + 1, "uiBoxAppend(%s, uiControl(tr_row), 0);\n", box) != 0) return -1;
        for (c = 0; c < rows[r].n; c++) {
            if (ui_widget(g, &g->doc->widgets[rows[r].at[c]], "tr_row", d + 1) != 0) return -1;
        }
        if (cg_gi(g, d, "}\n") != 0) return -1;
    }
    return 0;
}

static int
ui_children(Gen *g, int parent, const char *box, int d)
{
    int idxs[TR_WIDGETS_MAX];
    int n = tr_child_indices(g->doc, parent, idxs, TR_WIDGETS_MAX);
    int i;
    if (n < 0) return -1;
    if (ui_rows(g, parent, box, d) != 0) return -1;
    for (i = 0; i < n; i++) {
        const TrWidget *w = &g->doc->widgets[idxs[i]];
        if (w->kind == TR_KIND_POPUP || w->kind == TR_KIND_MODAL) {
            if (ui_widget(g, w, box, d) != 0) return -1;
        }
    }
    return 0;
}

static int
ui_menus(Gen *g)
{
    int i;
    int k;
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *bar = &g->doc->widgets[i];
        int menus[TR_WIDGETS_MAX];
        int nm;
        int m;
        if (bar->kind != TR_KIND_MENUBAR) continue;
        nm = tr_child_indices(g->doc, bar->id, menus, TR_WIDGETS_MAX);
        if (nm < 0) return -1;
        for (m = 0; m < nm; m++) {
            const TrWidget *menu = &g->doc->widgets[menus[m]];
            int items[TR_WIDGETS_MAX];
            int ni = tr_child_indices(g->doc, menu->id, items, TR_WIDGETS_MAX);
            if (ni < 0) return -1;
            if (cg_gi(g, 1, "{\n") != 0) return -1;
            if (cg_gi(g, 2, "uiMenu *tr_menu = uiNewMenu(") != 0) return -1;
            if (cg_text(g, menu, "label") != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
            if (cg_gi(g, 2, "if (!tr_menu) return -1;\n") != 0) return -1;
            for (k = 0; k < ni; k++) {
                const TrWidget *it = &g->doc->widgets[items[k]];
                if (cg_gi(g, 2, "{\n") != 0) return -1;
                if (cg_gi(g, 3, "uiMenuItem *tr_it = uiMenuAppendItem(tr_menu, ") != 0) return -1;
                if (cg_text(g, it, "label") != 0) return -1;
                if (cg_gp(g, ");\n") != 0) return -1;
                if (cg_gi(g, 3, "if (tr_it) uiMenuItemOnClicked(tr_it, tr_uimenu_%d, ui);\n", it->id) != 0)
                    return -1;
                if (cg_gi(g, 2, "}\n") != 0) return -1;
            }
            if (cg_gi(g, 1, "}\n") != 0) return -1;
        }
    }
    return 0;
}

static int
has_menu(const TrDoc *doc)
{
    int i;
    for (i = 0; i < doc->nwidgets; i++) {
        if (doc->widgets[i].kind == TR_KIND_MENUBAR) return 1;
    }
    return 0;
}

int
cg_libui(Gen *g)
{
    const char *cls = g->doc->class_name;
    const TrWidget *root = &g->doc->widgets[0];
    int ww = root->w > 64 ? root->w : 480;
    int hh = root->h > 64 ? root->h : 320;
    char box[32];
    if (ui_cbs(g) != 0) return -1;
    if (cg_gp(g, "int %s_frame(struct nk_context *ctx, %s *ui, float vw, float vh)\n{\n", cls, cls) != 0)
        return -1;
    if (cg_gi(g, 1, "(void)ctx; (void)ui; (void)vw; (void)vh;\n") != 0) return -1;
    if (cg_gi(g, 1, "return -1;\n}\n\n") != 0) return -1;
    if (cg_gp(g, "int %s_build(%s *ui, void *parent)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "uiInitOptions tr_opt;\n") != 0) return -1;
    if (cg_gi(g, 1, "const char *tr_err;\n") != 0) return -1;
    if (cg_gi(g, 1, "uiWindow *tr_win;\n") != 0) return -1;
    if (cg_gi(g, 1, "uiBox *tr_root;\n") != 0) return -1;
    if (cg_gi(g, 1, "(void)parent;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (!ui) return -1;\n") != 0) return -1;
    if (cg_gi(g, 1, "%s_user_pre(ui);\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "memset(&tr_opt, 0, sizeof tr_opt);\n") != 0) return -1;
    if (cg_gi(g, 1, "tr_opt.Size = sizeof tr_opt;\n") != 0) return -1;
    if (cg_gi(g, 1, "tr_err = uiInit(&tr_opt);\n") != 0) return -1;
    if (cg_gi(g, 1, "if (tr_err) { uiFreeInitError(tr_err); return -1; }\n") != 0) return -1;
    if (ui_menus(g) != 0) return -1;
    if (cg_gi(g, 1, "tr_win = uiNewWindow(") != 0) return -1;
    if (cg_text(g, root, "title") != 0) return -1;
    if (cg_gp(g, ", tr_u(ui, %d), tr_u(ui, %d), %d);\n", ww, hh, has_menu(g->doc)) != 0) return -1;
    if (cg_gi(g, 1, "tr_root = uiNewVerticalBox();\n") != 0) return -1;
    if (cg_gi(g, 1, "if (!tr_win || !tr_root) return -1;\n") != 0) return -1;
    if (cg_gi(g, 1, "uiWindowSetMargined(tr_win, 1);\n") != 0) return -1;
    if (cg_gi(g, 1, "uiWindowOnClosing(tr_win, tr_ui_close, ui);\n") != 0) return -1;
    if (root->kind == TR_KIND_ACTIVITY) {
        if (cg_gi(g, 1, "uiWindowSetBorderless(tr_win, 1);\n") != 0) return -1;
        if (cg_gi(g, 1, "uiWindowSetFullscreen(tr_win, 1);\n") != 0) return -1;
    }
    if (cg_gi(g, 1, "uiBoxSetPadded(tr_root, 1);\n") != 0) return -1;
    if (cg_gi(g, 1, "uiWindowSetChild(tr_win, uiControl(tr_root));\n") != 0) return -1;
    if (snprintf(box, sizeof box, "tr_root") < 0) return -1;
    if (ui_children(g, root->id, box, 1) != 0) return -1;
    if (cg_gi(g, 1, "uiControlShow(uiControl(tr_win));\n") != 0) return -1;
    if (cg_gi(g, 1, "%s_user_post(ui);\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "return 0;\n}\n") != 0) return -1;
    return 0;
}

static int
lv_cbs(Gen *g)
{
    const char *cls = g->doc->class_name;
    int i;
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *w = &g->doc->widgets[i];
        const char *ev = NULL;
        if (w->kind == TR_KIND_BUTTON || w->kind == TR_KIND_MENUITEM) ev = "click";
        else if (w->kind == TR_KIND_CHECKBOX || w->kind == TR_KIND_COLLAPSE || w->kind == TR_KIND_TREE)
            ev = "toggle";
        else if (w->kind == TR_KIND_INPUT || w->kind == TR_KIND_SLIDER || w->kind == TR_KIND_COMBO ||
                 w->kind == TR_KIND_RADIO || w->kind == TR_KIND_TABS)
            ev = "change";
        else continue;
        if (cg_gp(g, "static void tr_lvcb_%d(lv_event_t *e)\n{\n", w->id) != 0) return -1;
        if (cg_gi(g, 1, "%s *ui = (%s *)lv_event_get_user_data(e);\n", cls, cls) != 0) return -1;
        if (cg_gi(g, 1, "lv_obj_t *tr_tgt;\n") != 0) return -1;
        if (cg_gi(g, 1, "if (!ui) return;\n") != 0) return -1;
        if (cg_gi(g, 1, "tr_tgt = lv_event_get_target(e);\n") != 0) return -1;
        if (w->kind == TR_KIND_CHECKBOX) {
            CgSlot s;
            if (cg_int_slot(g, w, "checked", &s) != 0) return -1;
            if (cg_gi(g, 1, "%s = lv_obj_has_state(tr_tgt, LV_STATE_CHECKED) ? 1 : 0;\n", s.lv) != 0)
                return -1;
        } else if (w->kind == TR_KIND_SLIDER) {
            CgSlot s;
            if (cg_int_slot(g, w, "value", &s) != 0) return -1;
            if (cg_gi(g, 1, "%s = lv_slider_get_value(tr_tgt);\n", s.lv) != 0) return -1;
        } else if (w->kind == TR_KIND_COMBO || w->kind == TR_KIND_RADIO) {
            CgSlot s;
            if (cg_int_slot(g, w, "selected", &s) != 0) return -1;
            if (cg_gi(g, 1, "%s = (int)lv_dropdown_get_selected(tr_tgt);\n", s.lv) != 0) return -1;
        } else if (w->kind == TR_KIND_TABS) {
            CgSlot s;
            if (cg_int_slot(g, w, "selected", &s) != 0) return -1;
            if (cg_gi(g, 1, "%s = (int)lv_tabview_get_tab_active(tr_tgt);\n", s.lv) != 0) return -1;
        } else if (w->kind == TR_KIND_INPUT) {
            char bbuf[64], lbuf[64];
            if (text_buf(g->doc, w, bbuf, sizeof bbuf, lbuf, sizeof lbuf) != 0) return -1;
            if (cg_gi(g, 1, "tr_copy(%s, sizeof %s, lv_textarea_get_text(tr_tgt));\n", bbuf, bbuf) != 0)
                return -1;
            if (cg_gi(g, 1, "%s = tr_cstr_len(%s, (int)sizeof %s);\n", lbuf, bbuf, bbuf) != 0) return -1;
        } else if (cg_gi(g, 1, "(void)tr_tgt;\n") != 0) return -1;
        if (call_named(g, w, ev, 1) != 0) return -1;
        if (cg_gp(g, "}\n") != 0) return -1;
    }
    return 0;
}

/* Option text is data, never a printf format. Pieces are joined with a newline. */
static int
lv_emit_options(Gen *g, char parts[][TR_TEXT_MAX], int n)
{
    int i;
    if (cg_gp(g, "\"") != 0) return -1;
    for (i = 0; i < n; i++) {
        const unsigned char *p;
        if (i && cg_gp(g, "\\n") != 0) return -1;
        for (p = (const unsigned char *)parts[i]; *p != '\0'; p++) {
            if (*p == '\\' || *p == '"') {
                if (cg_gp(g, "\\%c", (int)*p) != 0) return -1;
            } else if (*p < 32 || *p > 126) {
                if (cg_gp(g, "\\%03o", (unsigned)*p) != 0) return -1;
            } else if (cg_gp(g, "%c", (int)*p) != 0) {
                return -1;
            }
        }
    }
    return cg_gp(g, "\"");
}

static int
lv_widget(Gen *g, const TrWidget *w, const char *parent, int d)
{
    char var[32];
    int st = stretch_of(w);
    if (d > 32) return cg_err(g, "widget nesting is too deep");
    if (snprintf(var, sizeof var, "tr_o_%d", w->id) < 0) return -1;
    switch (w->kind) {
    case TR_KIND_LABEL:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_label_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_label_set_text(%s, ", var) != 0) return -1;
        if (cg_text(g, w, "text") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (st && cg_gi(g, d + 1, "lv_obj_set_flex_grow(%s, 1);\n", var) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_BUTTON:
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_t *%s = lv_button_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_t *tr_lb = lv_label_create(%s);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s || !tr_lb) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_label_set_text(tr_lb, ") != 0) return -1;
        if (cg_text(g, w, "label") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_add_event_cb(%s, tr_lvcb_%d, LV_EVENT_CLICKED, ui);\n", var, w->id) != 0)
            return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_CHECKBOX: {
        CgSlot s;
        if (cg_int_slot(g, w, "checked", &s) != 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_checkbox_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_checkbox_set_text(%s, ", var) != 0) return -1;
        if (cg_text(g, w, "label") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (%s) lv_obj_add_state(%s, LV_STATE_CHECKED);\n", s.lv, var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_add_event_cb(%s, tr_lvcb_%d, LV_EVENT_VALUE_CHANGED, ui);\n", var, w->id) != 0)
            return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_INPUT: {
        char bbuf[64], lbuf[64];
        if (text_buf(g->doc, w, bbuf, sizeof bbuf, lbuf, sizeof lbuf) != 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_textarea_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (!tr_prop_int(w, "multiline", 0)) {
            if (cg_gi(g, d + 1, "lv_textarea_set_one_line(%s, true);\n", var) != 0) return -1;
        }
        if (tr_prop_int(w, "password", 0)) {
            if (cg_gi(g, d + 1, "lv_textarea_set_password_mode(%s, true);\n", var) != 0) return -1;
        }
        if (cg_gi(g, d + 1, "lv_textarea_set_text(%s, %s);\n", var, bbuf) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_add_event_cb(%s, tr_lvcb_%d, LV_EVENT_VALUE_CHANGED, ui);\n", var, w->id) != 0)
            return -1;
        if (st && cg_gi(g, d + 1, "lv_obj_set_flex_grow(%s, 1);\n", var) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_SLIDER: {
        CgSlot s;
        if (cg_int_slot(g, w, "value", &s) != 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_slider_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_slider_set_range(%s, 0, 100);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_slider_set_value(%s, tr_bound_int(%s, 0, 100), LV_ANIM_OFF);\n", var, s.lv) != 0)
            return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_width(%s, lv_pct(100));\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_add_event_cb(%s, tr_lvcb_%d, LV_EVENT_VALUE_CHANGED, ui);\n", var, w->id) != 0)
            return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_COMBO:
    case TR_KIND_RADIO: {
        CgSlot s;
        char parts[16][TR_TEXT_MAX];
        int n = 0;
        const TrProp *opt = tr_prop(w, w->kind == TR_KIND_COMBO ? "items" : "options");
        if (cg_int_slot(g, w, "selected", &s) != 0) return -1;
        if (split_list(opt ? opt->value : "", parts, 16, &n) != 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_dropdown_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_dropdown_set_options(%s, ", var) != 0) return -1;
        if (lv_emit_options(g, parts, n) != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        if (cg_gi(g, d + 1, "lv_dropdown_set_selected(%s, (uint32_t)%s);\n", var, s.lv) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_add_event_cb(%s, tr_lvcb_%d, LV_EVENT_VALUE_CHANGED, ui);\n", var, w->id) != 0)
            return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_PROGRESS: {
        CgSlot s;
        if (cg_int_slot(g, w, "value", &s) != 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_bar_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_bar_set_range(%s, 0, 100);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_bar_set_value(%s, tr_bound_int(%s, 0, 100), LV_ANIM_OFF);\n", var, s.lv) != 0)
            return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_width(%s, lv_pct(100));\n", var) != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_COLOR:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_obj_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_size(%s, lv_pct(100), 18);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_style_bg_color(%s, lv_color_make(%d, %d, %d), 0);\n", var,
                tr_prop_int(w, "r", 0), tr_prop_int(w, "g", 0), tr_prop_int(w, "b", 0)) != 0)
            return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_SEPARATOR:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_obj_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_size(%s, lv_pct(100), 8);\n", var) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_SPACER:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_obj_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_flex_grow(%s, 1);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_height(%s, 8);\n", var) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_IMAGE:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_image_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_image_set_src(%s, ", var) != 0) return -1;
        if (cg_text(g, w, "path") != 0) return -1;
        if (cg_gp(g, ");\n") != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_CUSTOM: {
        char hn[TR_NAME_MAX];
        const TrEvent *ev = cg_event(w, "draw");
        if (!ev || cg_handler(g->doc, w, ev, hn, sizeof hn) != 0) return -1;
        return cg_gi(g, d, "%s(ui, (void *)%s);\n", hn, parent);
    }
    case TR_KIND_GROUP:
    case TR_KIND_CHILD:
    case TR_KIND_DOCK:
    case TR_KIND_COLLAPSE:
    case TR_KIND_TREE:
    case TR_KIND_SPLITTER:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_obj_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_width(%s, lv_pct(100));\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_flex_flow(%s, LV_FLEX_FLOW_COLUMN);\n", var) != 0) return -1;
        if (w->kind != TR_KIND_SPLITTER) {
            if (cg_gi(g, d + 1, "{\n") != 0) return -1;
            if (cg_gi(g, d + 2, "lv_obj_t *tr_hd = lv_label_create(%s);\n", var) != 0) return -1;
            if (cg_gi(g, d + 2, "if (tr_hd) lv_label_set_text(tr_hd, ") != 0) return -1;
            if (cg_text(g, w, "title") != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
            if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        }
        if (w->kind == TR_KIND_COLLAPSE || w->kind == TR_KIND_TREE) {
            if (cg_gi(g, d + 1, "lv_obj_add_event_cb(%s, tr_lvcb_%d, LV_EVENT_CLICKED, ui);\n", var, w->id) != 0)
                return -1;
        }
        if (st && cg_gi(g, d + 1, "lv_obj_set_flex_grow(%s, 1);\n", var) != 0) return -1;
        if (lv_children(g, w->id, var, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_TABS: {
        int kids[TR_WIDGETS_MAX];
        int n = tr_child_indices(g->doc, w->id, kids, TR_WIDGETS_MAX);
        int i;
        if (n < 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_tabview_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_size(%s, lv_pct(100), lv_pct(100));\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_add_event_cb(%s, tr_lvcb_%d, LV_EVENT_VALUE_CHANGED, ui);\n", var, w->id) != 0)
            return -1;
        for (i = 0; i < n; i++) {
            const TrWidget *tab = &g->doc->widgets[kids[i]];
            if (cg_gi(g, d + 1, "{\n") != 0) return -1;
            if (cg_gi(g, d + 2, "lv_obj_t *tr_page = lv_tabview_add_tab(%s, ", var) != 0) return -1;
            if (cg_text(g, tab, "title") != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
            if (cg_gi(g, d + 2, "if (!tr_page) return -1;\n") != 0) return -1;
            if (cg_gi(g, d + 2, "lv_obj_set_flex_flow(tr_page, LV_FLEX_FLOW_COLUMN);\n") != 0) return -1;
            if (lv_children(g, tab->id, "tr_page", d + 2) != 0) return -1;
            if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        }
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_TABLE: {
        char parts[8][TR_TEXT_MAX];
        int nc = 0;
        int c;
        int head = tr_prop_int(w, "header", 1) ? 1 : 0;
        if (column_names(w, parts, 8, &nc) != 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_table_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "int tr_rows = %d;\n", tr_prop_int(w, "rows", 3)) != 0) return -1;
        if (cg_gi(g, d + 1, "int tr_cols = %d;\n", nc) != 0) return -1;
        if (cg_gi(g, d + 1, "int tr_head = %d;\n", head) != 0) return -1;
        if (cg_gi(g, d + 1, "int row, col;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "if (tr_rows < 0) tr_rows = 0;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "if (tr_rows > 64) tr_rows = 64;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "lv_table_set_column_count(%s, (uint32_t)tr_cols);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_table_set_row_count(%s, (uint32_t)(tr_rows + tr_head));\n", var) != 0)
            return -1;
        for (c = 0; c < nc; c++) {
            if (!head) break;
            if (cg_gi(g, d + 1, "lv_table_set_cell_value(%s, 0, %d, ", var, c) != 0) return -1;
            if (cg_cstr(g, parts[c]) != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
        }
        if (cg_gi(g, d + 1, "for (row = 0; row < tr_rows; row++) {\n") != 0) return -1;
        if (cg_gi(g, d + 2, "for (col = 0; col < tr_cols; col++) {\n") != 0) return -1;
        if (cg_gi(g, d + 3, "char tr_cell[160];\n") != 0) return -1;
        if (cg_gi(g, d + 3, "%s_cell(ui, row, col, tr_cell, sizeof tr_cell);\n", g->doc->class_name) != 0)
            return -1;
        if (cg_gi(g, d + 3, "lv_table_set_cell_value(%s, (uint32_t)(row + tr_head), (uint32_t)col, tr_cell);\n", var) != 0)
            return -1;
        if (cg_gi(g, d + 2, "}\n") != 0) return -1;
        if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        return cg_gi(g, d, "}\n");
    }
    case TR_KIND_POPUP:
    case TR_KIND_MODAL:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_obj_create(lv_layer_top());\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_size(%s, %d, %d);\n", var, w->w > 0 ? w->w : 320, w->h > 0 ? w->h : 200) != 0)
            return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_flex_flow(%s, LV_FLEX_FLOW_COLUMN);\n", var) != 0) return -1;
        if (lv_children(g, w->id, var, d + 1) != 0) return -1;
        return cg_gi(g, d, "}\n");
    case TR_KIND_TAB:
        return lv_children(g, w->id, parent, d);
    case TR_KIND_MENU:
    case TR_KIND_MENUITEM:
        return 0;
    case TR_KIND_MENUBAR: {
        int menus[TR_WIDGETS_MAX];
        int nm = tr_child_indices(g->doc, w->id, menus, TR_WIDGETS_MAX);
        int mi;
        if (nm < 0) return -1;
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_obj_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!%s) return -1;\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_width(%s, lv_pct(100));\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_height(%s, LV_SIZE_CONTENT);\n", var) != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_flex_flow(%s, LV_FLEX_FLOW_ROW);\n", var) != 0) return -1;
        for (mi = 0; mi < nm; mi++) {
            const TrWidget *menu = &g->doc->widgets[menus[mi]];
            int items[TR_WIDGETS_MAX];
            int ni = tr_child_indices(g->doc, menu->id, items, TR_WIDGETS_MAX);
            int k;
            if (ni < 0) return -1;
            if (cg_gi(g, d + 1, "{\n") != 0) return -1;
            if (cg_gi(g, d + 2, "lv_obj_t *tr_cap = lv_label_create(%s);\n", var) != 0) return -1;
            if (cg_gi(g, d + 2, "if (tr_cap) lv_label_set_text(tr_cap, ") != 0) return -1;
            if (cg_text(g, menu, "label") != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
            for (k = 0; k < ni; k++) {
                const TrWidget *it = &g->doc->widgets[items[k]];
                if (cg_gi(g, d + 2, "{\n") != 0) return -1;
                if (cg_gi(g, d + 3, "lv_obj_t *tr_btn = lv_button_create(%s);\n", var) != 0) return -1;
                if (cg_gi(g, d + 3, "lv_obj_t *tr_bl = lv_label_create(tr_btn);\n") != 0) return -1;
                if (cg_gi(g, d + 3, "if (!tr_btn || !tr_bl) return -1;\n") != 0) return -1;
                if (cg_gi(g, d + 3, "lv_label_set_text(tr_bl, ") != 0) return -1;
                if (cg_text(g, it, "label") != 0) return -1;
                if (cg_gp(g, ");\n") != 0) return -1;
                if (cg_gi(g, d + 3, "lv_obj_add_event_cb(tr_btn, tr_lvcb_%d, LV_EVENT_CLICKED, ui);\n", it->id) != 0)
                    return -1;
                if (cg_gi(g, d + 2, "}\n") != 0) return -1;
            }
            if (cg_gi(g, d + 1, "}\n") != 0) return -1;
        }
        return cg_gi(g, d, "}\n");
    }
    default:
        if (cg_gi(g, d, "{ lv_obj_t *%s = lv_label_create(%s);\n", var, parent) != 0) return -1;
        if (cg_gi(g, d + 1, "if (%s) lv_label_set_text(%s, \"%s\");\n", var, var, tr_kind_name(w->kind)) != 0)
            return -1;
        return cg_gi(g, d, "}\n");
    }
}

static int
lv_children(Gen *g, int parent, const char *obj, int d)
{
    int idxs[TR_WIDGETS_MAX];
    TrRow rows[TR_WIDGETS_MAX];
    int n;
    int nr;
    int r;
    int c;
    int i;
    n = tr_child_indices(g->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n < 0) return -1;
    for (i = 0; i < n; i++) {
        const TrWidget *bar = &g->doc->widgets[idxs[i]];
        if (bar->kind == TR_KIND_MENUBAR && lv_widget(g, bar, obj, d) != 0) return -1;
    }
    n = tr_flow_indices(g->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n < 0) return -1;
    nr = n == 0 ? 0 : tr_pack_rows(g->doc, idxs, n, rows, TR_WIDGETS_MAX);
    if (nr < 0) return -1;
    for (r = 0; r < nr; r++) {
        if (cg_gi(g, d, "{\n") != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_t *tr_row = lv_obj_create(%s);\n", obj) != 0) return -1;
        if (cg_gi(g, d + 1, "if (!tr_row) return -1;\n") != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_width(tr_row, lv_pct(100));\n") != 0) return -1;
        if (cg_gi(g, d + 1, "lv_obj_set_flex_flow(tr_row, LV_FLEX_FLOW_ROW);\n") != 0) return -1;
        for (c = 0; c < rows[r].n; c++) {
            if (lv_widget(g, &g->doc->widgets[rows[r].at[c]], "tr_row", d + 1) != 0) return -1;
        }
        if (cg_gi(g, d, "}\n") != 0) return -1;
    }
    n = tr_child_indices(g->doc, parent, idxs, TR_WIDGETS_MAX);
    if (n < 0) return -1;
    for (i = 0; i < n; i++) {
        const TrWidget *w = &g->doc->widgets[idxs[i]];
        if (w->kind == TR_KIND_POPUP || w->kind == TR_KIND_MODAL) {
            if (lv_widget(g, w, obj, d) != 0) return -1;
        }
    }
    return 0;
}

int
cg_lvgl(Gen *g)
{
    const char *cls = g->doc->class_name;
    const TrWidget *root = &g->doc->widgets[0];
    if (lv_cbs(g) != 0) return -1;
    if (cg_gp(g, "int %s_frame(struct nk_context *ctx, %s *ui, float vw, float vh)\n{\n", cls, cls) != 0)
        return -1;
    if (cg_gi(g, 1, "(void)ctx; (void)ui; (void)vw; (void)vh;\n") != 0) return -1;
    if (cg_gi(g, 1, "return -1;\n}\n\n") != 0) return -1;
    if (cg_gp(g, "int %s_build(%s *ui, void *parent)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "lv_obj_t *tr_parent = (lv_obj_t *)parent;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (!ui || !tr_parent) return -1;\n") != 0) return -1;
    if (cg_gi(g, 1, "%s_user_pre(ui);\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "lv_obj_set_flex_flow(tr_parent, LV_FLEX_FLOW_COLUMN);\n") != 0) return -1;
    if (cg_gi(g, 1, "lv_obj_set_style_bg_color(tr_parent, lv_color_hex(0x%02x%02x%02x), 0);\n",
            g->style ? g->style->color[1].r : 45,
            g->style ? g->style->color[1].g : 45,
            g->style ? g->style->color[1].b : 45) != 0)
        return -1;
    if (root->kind == TR_KIND_ACTIVITY || root->kind == TR_KIND_MAIN || root->kind == TR_KIND_WINDOW) {
        if (lv_children(g, root->id, "tr_parent", 1) != 0) return -1;
    } else if (lv_widget(g, root, "tr_parent", 1) != 0) return -1;
    if (cg_gi(g, 1, "%s_user_post(ui);\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "(void)%d;\n", root->w) != 0) return -1;
    return cg_gi(g, 1, "return 0;\n}\n");
}
