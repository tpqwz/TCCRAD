#include "ide.h"
#include "codegen.h"
#include "nk_glue.h"

#include <stdio.h>
#include <string.h>

static int nhits;
static struct { int id; struct nk_rect r; } hits[TR_WIDGETS_MAX];

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
        nk_layout_row_dynamic(ctx, 22, 1);
        if (nk_select_label(ctx, line, NK_TEXT_LEFT, w->id == ide->selected))
            ide_select(ide, w->id);
        if (tr_kind_is_container(w->kind)) draw_tree(ctx, ide, w->id, depth + 1);
    }
}

static void
prop_editor(struct nk_context *ctx, Ide *ide, TrWidget *w)
{
    int n = 0;
    int i;
    const TrPropDef *defs = tr_prop_defs(w->kind, &n);
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_label(ctx, tr_kind_name(w->kind), NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24, 2);
    nk_label(ctx, "name", NK_TEXT_LEFT);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, w->name, TR_NAME_MAX, nk_filter_default);
    nk_layout_row_dynamic(ctx, 24, 2);
    nk_label(ctx, "newline", NK_TEXT_LEFT);
    nk_checkbox_label(ctx, "", &w->newline);
    nk_layout_row_dynamic(ctx, 24, 1);
    w->stretch = nk_propertyi(ctx, "stretch", 0, w->stretch, 20, 1, 1.0f);
    nk_layout_row_dynamic(ctx, 24, 2);
    w->x = nk_propertyi(ctx, "x", -2000, w->x, 4000, 1, 1.0f);
    w->y = nk_propertyi(ctx, "y", -2000, w->y, 4000, 1, 1.0f);
    w->w = nk_propertyi(ctx, "w", -2000, w->w, 4000, 1, 1.0f);
    w->h = nk_propertyi(ctx, "h", -2000, w->h, 4000, 1, 1.0f);
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_checkbox_label(ctx, "overlay", &w->overlay);
    w->anchor = nk_propertyi(ctx, "anchor 0-3", 0, w->anchor, 3, 1, 1.0f);
    nk_layout_row_dynamic(ctx, 24, 2);
    nk_label(ctx, "config", NK_TEXT_LEFT);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, w->config, TR_NAME_MAX, nk_filter_default);
    for (i = 0; i < n; i++) {
        TrProp *p = tr_prop_mut(w, defs[i].key);
        int as_int;
        if (!p) continue;
        nk_layout_row_dynamic(ctx, 18, 1);
        nk_label(ctx, defs[i].key, NK_TEXT_LEFT);
        if (defs[i].val == TR_VAL_BOOL) {
            int on = tr_prop_int(w, defs[i].key, 0) ? 1 : 0;
            int before = on;
            nk_layout_row_dynamic(ctx, 22, 1);
            nk_checkbox_label(ctx, "on", &on);
            if (on != before) tr_prop_set(w, defs[i].key, on ? "1" : "0");
        } else if (defs[i].val == TR_VAL_INT) {
            as_int = tr_prop_int(w, defs[i].key, 0);
            nk_layout_row_dynamic(ctx, 24, 1);
            as_int = nk_propertyi(ctx, "#", -100000, as_int, 100000, 1, 1.0f);
            {
                char tmp[32];
                snprintf(tmp, sizeof tmp, "%d", as_int);
                if (strcmp(tmp, p->value) != 0) tr_prop_set(w, defs[i].key, tmp);
            }
        } else {
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, p->value, TR_TEXT_MAX, nk_filter_default);
        }
        if (defs[i].bindable) {
            nk_layout_row_dynamic(ctx, 22, 2);
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
        nk_layout_row_dynamic(ctx, 24, 1);
        cur = nk_combo(ctx, kinds, 5, cur, 22, nk_vec2(180, 140));
        if (cur >= 0 && cur < 5 && values[cur] != w->kind) tr_doc_set_root_kind(ide->doc, values[cur]);
    }
}

static void
event_editor(struct nk_context *ctx, Ide *ide, TrWidget *w)
{
    int i;
    if (w->nevents <= 0) {
        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "no events", NK_TEXT_LEFT);
        return;
    }
    if (ide->body_ev < 0 || ide->body_ev >= w->nevents) ide->body_ev = 0;
    nk_layout_row_dynamic(ctx, 22, w->nevents > 3 ? 3 : w->nevents);
    for (i = 0; i < w->nevents; i++) {
        if (nk_select_label(ctx, w->events[i].name, NK_TEXT_CENTERED, i == ide->body_ev)) {
            if (ide->body_ev != i) ide->body_id = 0;
            ide->body_ev = i;
        }
    }
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, w->events[ide->body_ev].handler,
        TR_NAME_MAX, nk_filter_default);
    if (ide->body_id != w->id || ide->body_ev < 0) {
        const char *t = tr_span_ptr(ide->doc, w->events[ide->body_ev].user);
        tr_copy_cap(ide->body, sizeof ide->body, t);
        ide->body_len = (int)strlen(ide->body);
        ide->body_id = w->id;
    }
    nk_layout_row_dynamic(ctx, 120, 1);
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
    nk_menubar_begin(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, 22, 6);
    nk_layout_row_push(ctx, 50);
    if (nk_menu_begin_label(ctx, "File", NK_TEXT_LEFT, nk_vec2(160, 200))) {
        nk_layout_row_dynamic(ctx, 22, 1);
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
    nk_layout_row_push(ctx, 50);
    if (nk_menu_begin_label(ctx, "Edit", NK_TEXT_LEFT, nk_vec2(160, 220))) {
        nk_layout_row_dynamic(ctx, 22, 1);
        if (nk_menu_item_label(ctx, "Undo", NK_TEXT_LEFT)) ide_undo(ide);
        if (nk_menu_item_label(ctx, "Redo", NK_TEXT_LEFT)) ide_redo(ide);
        if (nk_menu_item_label(ctx, "Duplicate", NK_TEXT_LEFT)) ide_duplicate(ide);
        if (nk_menu_item_label(ctx, "Delete", NK_TEXT_LEFT)) ide_delete(ide);
        if (nk_menu_item_label(ctx, "Move up", NK_TEXT_LEFT)) ide_move(ide, -1);
        if (nk_menu_item_label(ctx, "Move down", NK_TEXT_LEFT)) ide_move(ide, 1);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 70);
    if (nk_menu_begin_label(ctx, "Layout", NK_TEXT_LEFT, nk_vec2(180, 160))) {
        TrWidget *w = tr_doc_widget(ide->doc, ide->selected);
        nk_layout_row_dynamic(ctx, 22, 1);
        if (w && nk_menu_item_label(ctx, "Same row", NK_TEXT_LEFT)) w->newline = 0;
        if (w && nk_menu_item_label(ctx, "New row", NK_TEXT_LEFT)) w->newline = 1;
        if (w && nk_menu_item_label(ctx, "Stretch", NK_TEXT_LEFT)) w->stretch = w->stretch ? 0 : 1;
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60);
    if (nk_menu_begin_label(ctx, "View", NK_TEXT_LEFT, nk_vec2(180, 220))) {
        nk_layout_row_dynamic(ctx, 22, 1);
        if (nk_menu_item_label(ctx, "Code", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_CODE;
        if (nk_menu_item_label(ctx, "Fields", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_FIELDS;
        if (nk_menu_item_label(ctx, "Configs", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_CONFIG;
        if (nk_menu_item_label(ctx, "Style", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_STYLE;
        if (nk_menu_item_label(ctx, "All configs", NK_TEXT_LEFT)) ide->show_all_cfg = !ide->show_all_cfg;
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 70);
    if (nk_menu_begin_label(ctx, "Build", NK_TEXT_LEFT, nk_vec2(180, 240))) {
        nk_layout_row_dynamic(ctx, 22, 1);
        if (nk_menu_item_label(ctx, "Nuklear", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_NUKLEAR);
        if (nk_menu_item_label(ctx, "libui", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_LIBUI);
        if (nk_menu_item_label(ctx, "LVGL", NK_TEXT_LEFT)) ide_set_backend(ide, TR_BACKEND_LVGL);
        if (nk_menu_item_label(ctx, "Build", NK_TEXT_LEFT)) ide_build(ide, 0);
        if (nk_menu_item_label(ctx, "Build and run", NK_TEXT_LEFT)) ide_build(ide, 1);
        if (nk_menu_item_label(ctx, "Stop", NK_TEXT_LEFT)) ide_stop(ide);
        nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 60);
    if (nk_menu_begin_label(ctx, "Help", NK_TEXT_LEFT, nk_vec2(140, 80))) {
        nk_layout_row_dynamic(ctx, 22, 1);
        if (nk_menu_item_label(ctx, "About", NK_TEXT_LEFT)) ide->modal = IDE_MODAL_ABOUT;
        nk_menu_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);
    nk_layout_row_dynamic(ctx, 18, 1);
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
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->pick, (int)sizeof ide->pick, nk_filter_default);
    nk_layout_row_dynamic(ctx, 22, 1);
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
    nk_layout_row_dynamic(ctx, 28, 2);
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
    nk_layout_row_dynamic(ctx, 22, 3);
    nk_label(ctx, "type", NK_TEXT_LEFT);
    nk_label(ctx, "name", NK_TEXT_LEFT);
    nk_label(ctx, "init", NK_TEXT_LEFT);
    for (i = 0; i < ide->doc->nfields; i++) {
        TrField *f = &ide->doc->fields[i];
        nk_layout_row_dynamic(ctx, 22, 4);
        nk_label(ctx, f->type, NK_TEXT_LEFT);
        nk_label(ctx, f->name, NK_TEXT_LEFT);
        nk_label(ctx, f->init, NK_TEXT_LEFT);
        if (nk_button_label(ctx, "remove")) tr_field_remove(ide->doc, i);
    }
    nk_layout_row_dynamic(ctx, 24, 3);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->field_type, TR_TYPE_MAX, nk_filter_default);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->field_name, TR_NAME_MAX, nk_filter_default);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->field_init, TR_TEXT_MAX, nk_filter_default);
    nk_layout_row_dynamic(ctx, 26, 2);
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
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_checkbox_label(ctx, "show every configuration", &ide->show_all_cfg);
    for (i = 0; i < doc->nconfigs; i++) {
        int on = strcmp(doc->configs[i], doc->active_config) == 0;
        nk_layout_row_dynamic(ctx, 22, 1);
        if (nk_select_label(ctx, doc->configs[i], NK_TEXT_LEFT, on))
            tr_copy_cap(doc->active_config, sizeof doc->active_config, doc->configs[i]);
    }
    nk_layout_row_dynamic(ctx, 24, 2);
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
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, ide->style.name, TR_STYLE_NAME_MAX, nk_filter_default);
    for (i = 0; i < TR_STYLE_COLORS; i++) {
        int r = ide->style.color[i].r;
        int g = ide->style.color[i].g;
        int b = ide->style.color[i].b;
        nk_layout_row_dynamic(ctx, 22, 4);
        nk_label(ctx, tr_style_color_name(i), NK_TEXT_LEFT);
        r = nk_propertyi(ctx, "r", 0, r, 255, 1, 1.0f);
        g = nk_propertyi(ctx, "g", 0, g, 255, 1, 1.0f);
        b = nk_propertyi(ctx, "b", 0, b, 255, 1, 1.0f);
        ide->style.color[i].r = (unsigned char)r;
        ide->style.color[i].g = (unsigned char)g;
        ide->style.color[i].b = (unsigned char)b;
        ide->style.color[i].a = 255;
    }
    nk_layout_row_dynamic(ctx, 26, 2);
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
draw_code(struct nk_context *ctx, Ide *ide)
{
    TrBuf h;
    char err[160];
    const char *p;
    int line = 0;
    err[0] = '\0';
    tr_buf_clear(&ide->preview);
    if (tr_buf_init(&h) == 0) {
        if (tr_codegen(ide->doc, &ide->style, "app.h", &h, &ide->preview, err, sizeof err) != 0) {
            tr_buf_clear(&ide->preview);
            tr_buf_append_cstr(&ide->preview, err[0] ? err : "codegen failed");
        }
        tr_buf_free(&h);
    }
    nk_layout_row_dynamic(ctx, 16, 1);
    p = ide->preview.data ? ide->preview.data : "";
    while (*p && line < 400) {
        char row[160];
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n + 1 < sizeof row) {
            row[n] = p[n];
            n++;
        }
        row[n] = '\0';
        if (line >= ide->code_scroll) nk_label(ctx, row, NK_TEXT_LEFT);
        line++;
        p += n;
        if (*p == '\n') p++;
    }
    nk_layout_row_dynamic(ctx, 24, 3);
    if (nk_button_label(ctx, "Up")) if (ide->code_scroll > 0) ide->code_scroll -= 8;
    if (nk_button_label(ctx, "Down")) ide->code_scroll += 8;
    if (nk_button_label(ctx, "Close")) ide->modal = IDE_MODAL_NONE;
}

static void
draw_about(struct nk_context *ctx, Ide *ide)
{
    nk_layout_row_dynamic(ctx, 18, 1);
    nk_label(ctx, "tccrad 0.1.0-alpha.1", NK_TEXT_LEFT);
    nk_label(ctx, "A C RAD studio for Nuklear, libui, and LVGL.", NK_TEXT_LEFT);
    nk_label(ctx, "Designs round-trip through generated C. TinyCC builds and runs them.", NK_TEXT_LEFT);
    nk_label(ctx, "Bounded copies, checked allocation, posix_spawn. No system().", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 26, 1);
    if (nk_button_label(ctx, "Close")) ide->modal = IDE_MODAL_NONE;
}

static void
draw_modal(struct nk_context *ctx, Ide *ide, int w, int h)
{
    struct nk_rect r;
    const char *title = "Dialog";
    if (ide->modal == IDE_MODAL_NONE || ide->modal < 0) return;
    r = nk_rect(80, 60, (float)w - 160, (float)h - 120);
    if (ide->modal == IDE_MODAL_OPEN) title = "Open";
    if (ide->modal == IDE_MODAL_SAVE) title = "Save";
    if (ide->modal == IDE_MODAL_CODE) title = "Code preview";
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
    else if (ide->modal == IDE_MODAL_CODE) draw_code(ctx, ide);
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
    int left = 230;
    int right = 320;
    int top = 78;
    int bottom = 150;
    TrWidget *sel;
    int i;
    if (width < 800) width = 800;
    if (height < 500) height = 500;
    nhits = 0;
    r = nk_rect(0, 0, (float)width, (float)top);
    if (panel(ctx, "tccrad", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) draw_toolbar(ctx, ide);
    nk_end(ctx);

    {
        int avail = height - top - bottom;
        int pal_h = avail / 2;
        if (pal_h < 180) pal_h = 180;
        if (pal_h > avail - 140) pal_h = avail - 140;
        r = nk_rect(0, (float)top, (float)left, (float)pal_h);
        if (panel(ctx, "Palette", r, box)) {
            nk_layout_row_dynamic(ctx, 22, 2);
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
                nk_layout_row_dynamic(ctx, 22, 1);
                if (nk_select_label(ctx, line, NK_TEXT_LEFT, root->id == ide->selected))
                    ide_select(ide, root->id);
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
            nk_layout_row_dynamic(ctx, 22, 1);
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
                if (best > 0) ide_select(ide, best);
            }
        }
    }
    nk_end(ctx);

    r = nk_rect(0, (float)height - (float)bottom, (float)width, (float)bottom);
    if (panel(ctx, "Output", r, box)) {
        char line[TR_PATH_MAX + 8];
        const char *p = ide->log;
        int shown_lines = 0;
        nk_layout_row_dynamic(ctx, 16, 1);
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
