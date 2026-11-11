#include "model.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const TrPropDef window_props[] = {
    {"title", TR_VAL_STRING, 1, 0, "Window"},
    {"open", TR_VAL_BOOL, 1, 1, "1"},
    {"resizable", TR_VAL_BOOL, 0, 0, "1"},
    {"movable", TR_VAL_BOOL, 0, 0, "1"},
    {"border", TR_VAL_BOOL, 0, 0, "1"},
    {"titlebar", TR_VAL_BOOL, 0, 0, "1"},
    {"closable", TR_VAL_BOOL, 0, 0, "1"},
    {"scroll", TR_VAL_BOOL, 0, 0, "1"}
};
static const TrPropDef label_props[] = {
    {"text", TR_VAL_STRING, 1, 0, "Label"},
    {"align", TR_VAL_STRING, 0, 0, "left"}
};
static const TrPropDef button_props[] = {
    {"label", TR_VAL_STRING, 1, 0, "Button"}
};
static const TrPropDef check_props[] = {
    {"label", TR_VAL_STRING, 1, 0, "Check"},
    {"checked", TR_VAL_BOOL, 1, 1, "0"}
};
static const TrPropDef radio_props[] = {
    {"label", TR_VAL_STRING, 1, 0, "Choose"},
    {"options", TR_VAL_STRING, 0, 0, "Yes;No"},
    {"selected", TR_VAL_INT, 1, 1, "0"}
};
static const TrPropDef input_props[] = {
    {"text", TR_VAL_STRING, 1, 1, ""},
    {"capacity", TR_VAL_INT, 0, 0, "64"},
    {"password", TR_VAL_BOOL, 0, 0, "0"},
    {"multiline", TR_VAL_BOOL, 0, 0, "0"}
};
static const TrPropDef slider_props[] = {
    {"value", TR_VAL_INT, 1, 1, "0"},
    {"min", TR_VAL_INT, 0, 0, "0"},
    {"max", TR_VAL_INT, 0, 0, "100"},
    {"step", TR_VAL_INT, 0, 0, "1"}
};
static const TrPropDef combo_props[] = {
    {"items", TR_VAL_STRING, 1, 0, "Red;Green;Blue"},
    {"selected", TR_VAL_INT, 1, 1, "0"}
};
static const TrPropDef progress_props[] = {
    {"value", TR_VAL_INT, 1, 1, "40"},
    {"max", TR_VAL_INT, 0, 0, "100"}
};
static const TrPropDef color_props[] = {
    {"r", TR_VAL_INT, 1, 1, "80"},
    {"g", TR_VAL_INT, 1, 1, "140"},
    {"b", TR_VAL_INT, 1, 1, "220"},
    {"a", TR_VAL_INT, 1, 1, "255"}
};
static const TrPropDef group_props[] = {
    {"title", TR_VAL_STRING, 1, 0, "Group"},
    {"border", TR_VAL_BOOL, 0, 0, "1"}
};
static const TrPropDef tabs_props[] = {
    {"selected", TR_VAL_INT, 1, 1, "0"}
};
static const TrPropDef tab_props[] = {
    {"title", TR_VAL_STRING, 1, 0, "Tab"}
};
static const TrPropDef collapse_props[] = {
    {"title", TR_VAL_STRING, 1, 0, "Section"},
    {"open", TR_VAL_BOOL, 1, 1, "1"}
};
static const TrPropDef tree_props[] = {
    {"title", TR_VAL_STRING, 1, 0, "Node"},
    {"open", TR_VAL_BOOL, 1, 1, "0"}
};
static const TrPropDef table_props[] = {
    {"columns", TR_VAL_STRING, 0, 0, "Name;Value"},
    {"rows", TR_VAL_INT, 1, 0, "3"},
    {"header", TR_VAL_BOOL, 0, 0, "1"}
};
static const TrPropDef menu_props[] = {
    {"label", TR_VAL_STRING, 1, 0, "Menu"}
};
static const TrPropDef item_props[] = {
    {"label", TR_VAL_STRING, 1, 0, "Item"},
    {"shortcut", TR_VAL_STRING, 0, 0, ""}
};
static const TrPropDef image_props[] = {
    {"path", TR_VAL_STRING, 1, 0, "image.png"}
};
static const TrPropDef custom_props[] = {
    {"func", TR_VAL_STRING, 0, 0, "custom_draw"}
};
static const TrPropDef split_props[] = {
    {"ratio", TR_VAL_INT, 1, 1, "50"},
    {"orient", TR_VAL_STRING, 0, 0, "h"}
};
static const TrPropDef child_props[] = {
    {"title", TR_VAL_STRING, 1, 0, "Child"},
    {"border", TR_VAL_BOOL, 0, 0, "1"}
};
static const TrPropDef dock_props[] = {
    {"title", TR_VAL_STRING, 1, 0, "Dock"}
};

static const char *const ev_appear[] = {"appear", "close", NULL};
static const char *const ev_click[] = {"click", NULL};
static const char *const ev_toggle[] = {"toggle", NULL};
static const char *const ev_change[] = {"change", NULL};
static const char *const ev_select[] = {"select", NULL};
static const char *const ev_draw[] = {"draw", NULL};

static void
set_err(char *err, size_t cap, int line, const char *msg)
{
    int n;
    if (!err || cap == 0 || err[0] != '\0') return;
    n = snprintf(err, cap, "line %d: %s", line, msg);
    if (n < 0) err[0] = '\0';
}

const char *
tr_backend_name(TrBackend b)
{
    if (b == TR_BACKEND_LIBUI) return "libui";
    if (b == TR_BACKEND_LVGL) return "lvgl";
    return "nuklear";
}

TrBackend
tr_backend_from_name(const char *s)
{
    if (s && strcmp(s, "libui") == 0) return TR_BACKEND_LIBUI;
    if (s && strcmp(s, "lvgl") == 0) return TR_BACKEND_LVGL;
    if (s && strcmp(s, "nuklear") == 0) return TR_BACKEND_NUKLEAR;
    return TR_BACKEND_NUKLEAR;
}

const char *
tr_kind_name(TrKind kind)
{
    switch (kind) {
    case TR_KIND_WINDOW: return "window";
    case TR_KIND_POPUP: return "popup";
    case TR_KIND_MODAL: return "modal";
    case TR_KIND_MAIN: return "main";
    case TR_KIND_ACTIVITY: return "activity";
    case TR_KIND_LABEL: return "label";
    case TR_KIND_BUTTON: return "button";
    case TR_KIND_CHECKBOX: return "check";
    case TR_KIND_RADIO: return "radio";
    case TR_KIND_INPUT: return "input";
    case TR_KIND_SLIDER: return "slider";
    case TR_KIND_COMBO: return "combo";
    case TR_KIND_PROGRESS: return "progress";
    case TR_KIND_COLOR: return "color";
    case TR_KIND_SEPARATOR: return "separator";
    case TR_KIND_SPACER: return "spacer";
    case TR_KIND_GROUP: return "group";
    case TR_KIND_TABS: return "tabs";
    case TR_KIND_TAB: return "tab";
    case TR_KIND_COLLAPSE: return "collapse";
    case TR_KIND_TREE: return "tree";
    case TR_KIND_TABLE: return "table";
    case TR_KIND_MENUBAR: return "menubar";
    case TR_KIND_MENU: return "menu";
    case TR_KIND_MENUITEM: return "item";
    case TR_KIND_IMAGE: return "image";
    case TR_KIND_CUSTOM: return "custom";
    case TR_KIND_SPLITTER: return "splitter";
    case TR_KIND_CHILD: return "child";
    case TR_KIND_DOCK: return "dock";
    case TR_KIND_NONE: break;
    }
    return "none";
}

TrKind
tr_kind_from_name(const char *name)
{
    TrKind k;
    if (!name) return TR_KIND_NONE;
    for (k = TR_KIND_WINDOW; k <= TR_KIND_DOCK; k = (TrKind)(k + 1)) {
        if (strcmp(tr_kind_name(k), name) == 0) return k;
    }
    return TR_KIND_NONE;
}

int
tr_kind_is_window(TrKind kind)
{
    return kind == TR_KIND_WINDOW || kind == TR_KIND_POPUP || kind == TR_KIND_MODAL ||
        kind == TR_KIND_MAIN || kind == TR_KIND_ACTIVITY;
}

int
tr_kind_is_container(TrKind kind)
{
    return tr_kind_is_window(kind) || kind == TR_KIND_GROUP || kind == TR_KIND_TABS ||
        kind == TR_KIND_TAB || kind == TR_KIND_COLLAPSE || kind == TR_KIND_TREE ||
        kind == TR_KIND_TABLE || kind == TR_KIND_MENUBAR || kind == TR_KIND_MENU ||
        kind == TR_KIND_SPLITTER || kind == TR_KIND_CHILD || kind == TR_KIND_DOCK;
}

const TrPropDef *
tr_prop_defs(TrKind kind, int *n)
{
    const TrPropDef *d = NULL;
    int c = 0;
    switch (kind) {
    case TR_KIND_WINDOW: case TR_KIND_POPUP: case TR_KIND_MODAL:
    case TR_KIND_MAIN: case TR_KIND_ACTIVITY:
        d = window_props; c = (int)(sizeof window_props / sizeof window_props[0]); break;
    case TR_KIND_LABEL: d = label_props; c = (int)(sizeof label_props / sizeof label_props[0]); break;
    case TR_KIND_BUTTON: d = button_props; c = (int)(sizeof button_props / sizeof button_props[0]); break;
    case TR_KIND_CHECKBOX: d = check_props; c = (int)(sizeof check_props / sizeof check_props[0]); break;
    case TR_KIND_RADIO: d = radio_props; c = (int)(sizeof radio_props / sizeof radio_props[0]); break;
    case TR_KIND_INPUT: d = input_props; c = (int)(sizeof input_props / sizeof input_props[0]); break;
    case TR_KIND_SLIDER: d = slider_props; c = (int)(sizeof slider_props / sizeof slider_props[0]); break;
    case TR_KIND_COMBO: d = combo_props; c = (int)(sizeof combo_props / sizeof combo_props[0]); break;
    case TR_KIND_PROGRESS: d = progress_props; c = (int)(sizeof progress_props / sizeof progress_props[0]); break;
    case TR_KIND_COLOR: d = color_props; c = (int)(sizeof color_props / sizeof color_props[0]); break;
    case TR_KIND_GROUP: d = group_props; c = (int)(sizeof group_props / sizeof group_props[0]); break;
    case TR_KIND_TABS: d = tabs_props; c = (int)(sizeof tabs_props / sizeof tabs_props[0]); break;
    case TR_KIND_TAB: d = tab_props; c = (int)(sizeof tab_props / sizeof tab_props[0]); break;
    case TR_KIND_COLLAPSE: d = collapse_props; c = (int)(sizeof collapse_props / sizeof collapse_props[0]); break;
    case TR_KIND_TREE: d = tree_props; c = (int)(sizeof tree_props / sizeof tree_props[0]); break;
    case TR_KIND_TABLE: d = table_props; c = (int)(sizeof table_props / sizeof table_props[0]); break;
    case TR_KIND_MENU: d = menu_props; c = (int)(sizeof menu_props / sizeof menu_props[0]); break;
    case TR_KIND_MENUITEM: d = item_props; c = (int)(sizeof item_props / sizeof item_props[0]); break;
    case TR_KIND_IMAGE: d = image_props; c = (int)(sizeof image_props / sizeof image_props[0]); break;
    case TR_KIND_CUSTOM: d = custom_props; c = (int)(sizeof custom_props / sizeof custom_props[0]); break;
    case TR_KIND_SPLITTER: d = split_props; c = (int)(sizeof split_props / sizeof split_props[0]); break;
    case TR_KIND_CHILD: d = child_props; c = (int)(sizeof child_props / sizeof child_props[0]); break;
    case TR_KIND_DOCK: d = dock_props; c = (int)(sizeof dock_props / sizeof dock_props[0]); break;
    case TR_KIND_SEPARATOR: case TR_KIND_SPACER: case TR_KIND_MENUBAR: case TR_KIND_NONE:
        break;
    }
    if (n) *n = c;
    return d;
}

const char *const *
tr_event_defs(TrKind kind, int *n)
{
    const char *const *e = NULL;
    int c = 0;
    switch (kind) {
    case TR_KIND_WINDOW: case TR_KIND_POPUP: case TR_KIND_MODAL:
    case TR_KIND_MAIN: case TR_KIND_ACTIVITY:
        e = ev_appear; break;
    case TR_KIND_BUTTON: case TR_KIND_MENUITEM:
        e = ev_click; break;
    case TR_KIND_CHECKBOX: case TR_KIND_COLLAPSE: case TR_KIND_TREE:
        e = ev_toggle; break;
    case TR_KIND_RADIO: case TR_KIND_INPUT: case TR_KIND_SLIDER:
    case TR_KIND_COMBO: case TR_KIND_TABS:
        e = ev_change; break;
    case TR_KIND_TABLE:
        e = ev_select; break;
    case TR_KIND_CUSTOM:
        e = ev_draw; break;
    default:
        break;
    }
    if (e) {
        while (e[c]) c++;
    }
    if (n) *n = c;
    return e;
}

static int
token_ok(const char *name)
{
    size_t i;
    unsigned char c;
    if (!name || name[0] == '\0') return 0;
    c = (unsigned char)name[0];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) return 0;
    for (i = 1; name[i] != '\0'; i++) {
        c = (unsigned char)name[i];
        if (i >= 63) return 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    if (strncmp(name, "tr_", 3) == 0) return 0;
    return 1;
}

static int
name_reserved(const char *name)
{
    return !token_ok(name) || tr_is_keyword(name);
}

static int
arena_reserve(TrDoc *doc, size_t need)
{
    char *p;
    size_t cap;
    if (!doc || !doc->arena) return -1;
    if (need > TR_ARENA_MAX) return -1;
    if (need <= doc->arena_cap) return 0;
    cap = doc->arena_cap ? doc->arena_cap : 4096;
    while (cap < need) {
        if (cap > TR_ARENA_MAX / 2) return -1;
        cap *= 2;
    }
    if (cap > TR_ARENA_MAX) cap = TR_ARENA_MAX;
    if (cap < need) return -1;
    p = (char *)realloc(doc->arena, cap);
    if (!p) return -1;
    doc->arena = p;
    doc->arena_cap = cap;
    return 0;
}

TrDoc *
tr_doc_new(void)
{
    TrDoc *doc = (TrDoc *)calloc(1, sizeof *doc);
    if (!doc) return NULL;
    doc->arena = (char *)malloc(4096);
    if (!doc->arena) {
        free(doc);
        return NULL;
    }
    doc->arena[0] = '\0';
    doc->arena_len = 1;
    doc->arena_cap = 4096;
    doc->next_id = 1;
    doc->version = 1;
    doc->scale_milli = 1000;
    doc->backend = TR_BACKEND_NUKLEAR;
    return doc;
}

void
tr_doc_free(TrDoc *doc)
{
    if (!doc) return;
    free(doc->arena);
    doc->arena = NULL;
    free(doc);
}

static int
user_text_ok(const char *text, size_t n)
{
    size_t i = 0;
    if (n > TR_USER_MAX) return 0;
    while (i < n) {
        size_t start = i;
        size_t len;
        while (i < n && text[i] != '\n') {
            if (text[i] == '\0') return 0;
            i++;
        }
        len = i - start;
        if (len == 7 && memcmp(text + start, "enduser", 7) == 0) return 0;
        if (len >= 10 && memcmp(text + start, "/* @tccrad", 10) == 0) return 0;
        if (len >= 14 && memcmp(text + start, "/* @end user", 12) == 0) return 0;
        if (len >= 15 && memcmp(text + start, "@end tccrad-doc", 15) == 0) return 0;
        if (i < n && text[i] == '\n') i++;
    }
    return 1;
}

int
tr_span_set(TrDoc *doc, TrSpan *slot, const char *text, size_t n)
{
    size_t base;
    if (!doc || !slot) return -1;
    if (!text) {
        text = "";
        n = 0;
    }
    while (n > 0 && text[n - 1] == '\n') n--;
    if (!user_text_ok(text, n)) return -1;
    if (n == 0) {
        slot->off = 0;
        slot->len = 0;
        return 0;
    }
    if (n > UINT_MAX) return -1;
    if (slot->off > 0 &&
        (size_t)slot->off + (size_t)slot->len + 1 == doc->arena_len) {
        base = slot->off;
        if (base > SIZE_MAX - n - 1) return -1;
        if (arena_reserve(doc, base + n + 1) != 0) return -1;
        memcpy(doc->arena + base, text, n);
        doc->arena[base + n] = '\0';
        slot->len = (unsigned)n;
        doc->arena_len = base + n + 1;
        return 0;
    }
    if (doc->arena_len > SIZE_MAX - n - 1) return -1;
    if (arena_reserve(doc, doc->arena_len + n + 1) != 0) return -1;
    if (doc->arena_len > UINT_MAX) return -1;
    memcpy(doc->arena + doc->arena_len, text, n);
    doc->arena[doc->arena_len + n] = '\0';
    slot->off = (unsigned)doc->arena_len;
    slot->len = (unsigned)n;
    doc->arena_len += n + 1;
    return 0;
}

const char *
tr_span_ptr(const TrDoc *doc, TrSpan slot)
{
    if (!doc || !doc->arena) return "";
    if ((size_t)slot.off >= doc->arena_len) return "";
    if ((size_t)slot.off + (size_t)slot.len >= doc->arena_len) return "";
    if (doc->arena[slot.off + slot.len] != '\0') return "";
    return doc->arena + slot.off;
}

static void
fill_defaults(TrWidget *w)
{
    int n = 0;
    int i;
    const TrPropDef *defs = tr_prop_defs(w->kind, &n);
    const char *const *ev;
    int ne = 0;
    w->nprops = 0;
    w->nevents = 0;
    for (i = 0; i < n && i < TR_PROPS_MAX; i++) {
        TrProp *p = &w->props[w->nprops++];
        memset(p, 0, sizeof *p);
        tr_copy_cap(p->key, sizeof p->key, defs[i].key);
        tr_copy_cap(p->value, sizeof p->value, defs[i].def ? defs[i].def : "");
    }
    ev = tr_event_defs(w->kind, &ne);
    for (i = 0; i < ne && i < TR_EVENTS_MAX; i++) {
        TrEvent *e = &w->events[w->nevents++];
        memset(e, 0, sizeof *e);
        tr_copy_cap(e->name, sizeof e->name, ev[i]);
    }
}

int
tr_parent_ok(TrKind parent, TrKind child)
{
    if (child == TR_KIND_WINDOW || child == TR_KIND_MAIN || child == TR_KIND_ACTIVITY)
        return 0;
    if (child == TR_KIND_POPUP || child == TR_KIND_MODAL)
        return parent == TR_KIND_WINDOW || parent == TR_KIND_MAIN || parent == TR_KIND_ACTIVITY;
    if (child == TR_KIND_MENUITEM) return parent == TR_KIND_MENU;
    if (child == TR_KIND_MENU) return parent == TR_KIND_MENUBAR;
    if (child == TR_KIND_MENUBAR)
        return parent == TR_KIND_WINDOW || parent == TR_KIND_MAIN || parent == TR_KIND_ACTIVITY;
    if (child == TR_KIND_TAB) return parent == TR_KIND_TABS;
    if (parent == TR_KIND_MENU || parent == TR_KIND_MENUBAR || parent == TR_KIND_TABS)
        return 0;
    return tr_kind_is_container(parent);
}

int
tr_doc_index(const TrDoc *doc, int id)
{
    int i;
    if (!doc || id <= 0) return -1;
    for (i = 0; i < doc->nwidgets; i++) {
        if (doc->widgets[i].id == id) return i;
    }
    return -1;
}

TrWidget *
tr_doc_widget(TrDoc *doc, int id)
{
    int i = tr_doc_index(doc, id);
    return i < 0 ? NULL : &doc->widgets[i];
}

const TrWidget *
tr_doc_widget_c(const TrDoc *doc, int id)
{
    int i = tr_doc_index(doc, id);
    return i < 0 ? NULL : &doc->widgets[i];
}

static int
parent_of(const TrDoc *doc, int id)
{
    int i = tr_doc_index(doc, id);
    if (i < 0) return -2;
    return doc->widgets[i].parent;
}

static int
is_inside(const TrDoc *doc, int index, int ancestor_id)
{
    int p = doc->widgets[index].parent;
    int guard = 0;
    while (p != -1 && guard++ < TR_WIDGETS_MAX) {
        if (p == ancestor_id) return 1;
        p = parent_of(doc, p);
        if (p == -2) return 0;
    }
    return 0;
}

static int
subtree_count(const TrDoc *doc, int index)
{
    int id;
    int n = 1;
    int i;
    if (!doc || index < 0 || index >= doc->nwidgets) return 0;
    id = doc->widgets[index].id;
    for (i = index + 1; i < doc->nwidgets; i++) {
        if (!is_inside(doc, i, id)) break;
        n++;
    }
    return n;
}

static int
widget_name_taken(const TrDoc *doc, const char *name, int skip_index)
{
    int i;
    for (i = 0; i < doc->nwidgets; i++) {
        if (i == skip_index) continue;
        if (strcmp(doc->widgets[i].name, name) == 0) return 1;
    }
    return 0;
}

int
tr_doc_add_widget(TrDoc *doc, int parent_id, TrKind kind, const char *name)
{
    int at;
    int pindex;
    char auto_name[TR_NAME_MAX];
    TrWidget *w;
    if (!doc || kind == TR_KIND_NONE) return -1;
    if (doc->nwidgets >= TR_WIDGETS_MAX) return -1;
    if (parent_id < 0) {
        if (doc->nwidgets != 0 || !tr_kind_is_window(kind)) return -1;
        at = 0;
    } else {
        pindex = tr_doc_index(doc, parent_id);
        if (pindex < 0) return -1;
        if (!tr_parent_ok(doc->widgets[pindex].kind, kind)) return -1;
        at = pindex + subtree_count(doc, pindex);
    }
    if (!name || name[0] == '\0') {
        int nw = snprintf(auto_name, sizeof auto_name, "%s%d", tr_kind_name(kind), doc->next_id);
        if (nw < 0 || (size_t)nw >= sizeof auto_name) return -1;
        name = auto_name;
    }
    if (name_reserved(name) || widget_name_taken(doc, name, -1)) return -1;
    if (at < doc->nwidgets) {
        memmove(&doc->widgets[at + 1], &doc->widgets[at],
            (size_t)(doc->nwidgets - at) * sizeof(TrWidget));
    }
    w = &doc->widgets[at];
    memset(w, 0, sizeof *w);
    w->id = doc->next_id++;
    w->parent = parent_id < 0 ? -1 : parent_id;
    w->kind = kind;
    tr_copy_cap(w->name, sizeof w->name, name);
    w->newline = 1;
    fill_defaults(w);
    doc->nwidgets++;
    return w->id;
}

int
tr_doc_delete(TrDoc *doc, int id)
{
    int index;
    int n;
    if (!doc) return -1;
    index = tr_doc_index(doc, id);
    if (index < 0) return -1;
    if (doc->widgets[index].parent < 0) return -1;
    n = subtree_count(doc, index);
    if (index + n < doc->nwidgets) {
        memmove(&doc->widgets[index], &doc->widgets[index + n],
            (size_t)(doc->nwidgets - index - n) * sizeof(TrWidget));
    }
    doc->nwidgets -= n;
    return 0;
}

static int
rotate_widgets(TrWidget *w, int start, int left_n, int total)
{
    TrWidget *tmp;
    if (left_n <= 0 || left_n >= total) return -1;
    tmp = (TrWidget *)malloc((size_t)left_n * sizeof(TrWidget));
    if (!tmp) return -1;
    memcpy(tmp, w + start, (size_t)left_n * sizeof(TrWidget));
    memmove(w + start, w + start + left_n, (size_t)(total - left_n) * sizeof(TrWidget));
    memcpy(w + start + (total - left_n), tmp, (size_t)left_n * sizeof(TrWidget));
    free(tmp);
    return 0;
}

int
tr_doc_move(TrDoc *doc, int id, int dir)
{
    int index;
    int parent;
    int prev;
    int i;
    int ours;
    int end;
    if (!doc || (dir != -1 && dir != 1)) return -1;
    index = tr_doc_index(doc, id);
    if (index < 0) return -1;
    parent = doc->widgets[index].parent;
    if (parent < 0) return -1;
    ours = subtree_count(doc, index);
    end = index + ours;
    if (dir < 0) {
        int prev_n;
        prev = -1;
        for (i = 0; i < index; i++) {
            if (doc->widgets[i].parent == parent) prev = i;
        }
        if (prev < 0) return -1;
        prev_n = subtree_count(doc, prev);
        if (prev + prev_n != index) return -1;
        return rotate_widgets(doc->widgets, prev, prev_n, prev_n + ours);
    }
    if (end >= doc->nwidgets) return -1;
    if (doc->widgets[end].parent != parent) return -1;
    return rotate_widgets(doc->widgets, index, ours, ours + subtree_count(doc, end));
}

int
tr_doc_duplicate(TrDoc *doc, int id)
{
    int index;
    int n;
    int i;
    int j;
    int at;
    int new_root;
    int map_from[TR_WIDGETS_MAX];
    int map_to[TR_WIDGETS_MAX];
    TrWidget *tmp;
    if (!doc) return -1;
    index = tr_doc_index(doc, id);
    if (index < 0 || doc->widgets[index].parent < 0) return -1;
    n = subtree_count(doc, index);
    if (n <= 0 || doc->nwidgets > TR_WIDGETS_MAX - n) return -1;
    tmp = (TrWidget *)malloc((size_t)n * sizeof(TrWidget));
    if (!tmp) return -1;
    memcpy(tmp, &doc->widgets[index], (size_t)n * sizeof(TrWidget));
    for (i = 0; i < n; i++) {
        map_from[i] = tmp[i].id;
        map_to[i] = doc->next_id++;
        tmp[i].id = map_to[i];
    }
    for (i = 1; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (tmp[i].parent == map_from[j]) {
                tmp[i].parent = map_to[j];
                break;
            }
        }
    }
    for (i = 0; i < n; i++) {
        char base[40];
        int k;
        tr_copy_cap(base, sizeof base, tmp[i].name);
        for (k = 2; k < 1000; k++) {
            char trial[TR_NAME_MAX];
            int nw = snprintf(trial, sizeof trial, "%s_%d", base, k);
            int used = 0;
            int t;
            if (nw < 0 || (size_t)nw >= sizeof trial || !tr_is_ident(trial)) continue;
            used = widget_name_taken(doc, trial, -1);
            for (t = 0; t < i; t++) {
                if (strcmp(tmp[t].name, trial) == 0) used = 1;
            }
            if (!used) {
                tr_copy_cap(tmp[i].name, sizeof tmp[i].name, trial);
                break;
            }
        }
    }
    at = index + n;
    if (at < doc->nwidgets) {
        memmove(&doc->widgets[at + n], &doc->widgets[at],
            (size_t)(doc->nwidgets - at) * sizeof(TrWidget));
    }
    memcpy(&doc->widgets[at], tmp, (size_t)n * sizeof(TrWidget));
    doc->nwidgets += n;
    new_root = map_to[0];
    free(tmp);
    return new_root;
}

int
tr_doc_set_root_kind(TrDoc *doc, TrKind kind)
{
    if (!doc || doc->nwidgets < 1) return -1;
    if (doc->widgets[0].parent != -1) return -1;
    if (!tr_kind_is_window(kind)) return -1;
    doc->widgets[0].kind = kind;
    return 0;
}

int
tr_child_indices(const TrDoc *doc, int parent_id, int *out, int max)
{
    int i;
    int n = 0;
    if (!doc || !out || max < 1) return -1;
    for (i = 0; i < doc->nwidgets; i++) {
        if (doc->widgets[i].parent != parent_id) continue;
        if (n >= max) return -1;
        out[n++] = i;
    }
    return n;
}

int
tr_flow_indices(const TrDoc *doc, int parent_id, int *out, int max)
{
    int i;
    int n = 0;
    if (!doc || !out || max < 1) return -1;
    for (i = 0; i < doc->nwidgets; i++) {
        TrKind k;
        if (doc->widgets[i].parent != parent_id) continue;
        if (doc->widgets[i].overlay) continue;
        k = doc->widgets[i].kind;
        if (k == TR_KIND_MENUBAR || k == TR_KIND_POPUP || k == TR_KIND_MODAL) continue;
        if (n >= max) return -1;
        out[n++] = i;
    }
    return n;
}

int
tr_pack_rows(const TrDoc *doc, const int *idxs, int n, TrRow *rows, int max_rows)
{
    int i = 0;
    int nr = 0;
    if (!doc || !rows || n < 0 || max_rows < 1) return -1;
    if (n > 0 && !idxs) return -1;
    while (i < n) {
        TrRow *r;
        int max_pos = 28;
        int most_neg = 0;
        if (nr >= max_rows) return -1;
        r = &rows[nr++];
        memset(r, 0, sizeof *r);
        for (;;) {
            const TrWidget *w = &doc->widgets[idxs[i]];
            int weight = w->stretch > 0 ? w->stretch : 1;
            r->at[r->n++] = idxs[i];
            if (w->h > max_pos) max_pos = w->h;
            if (w->h < most_neg) most_neg = w->h;
            if (r->stretch > INT_MAX - weight) return -1;
            r->stretch += weight;
            i++;
            if (i >= n || r->n >= TR_ROW_COLS) break;
            if (doc->widgets[idxs[i]].newline) break;
        }
        r->height = most_neg < 0 ? most_neg : max_pos;
    }
    return nr;
}

const TrProp *
tr_prop(const TrWidget *w, const char *key)
{
    int i;
    if (!w || !key) return NULL;
    for (i = 0; i < w->nprops; i++) {
        if (strcmp(w->props[i].key, key) == 0) return &w->props[i];
    }
    return NULL;
}

TrProp *
tr_prop_mut(TrWidget *w, const char *key)
{
    return (TrProp *)tr_prop(w, key);
}

int
tr_prop_int(const TrWidget *w, const char *key, int def)
{
    const TrProp *p = tr_prop(w, key);
    char *end;
    long v;
    if (!p || p->value[0] == '\0') return def;
    errno = 0;
    v = strtol(p->value, &end, 10);
    if (errno || end == p->value || *end != '\0') return def;
    if (v > INT_MAX || v < INT_MIN) return def;
    return (int)v;
}

int
tr_prop_set(TrWidget *w, const char *key, const char *value)
{
    TrProp *p;
    if (!w || !key || !value) return -1;
    if (strchr(key, ' ') || strlen(key) >= TR_NAME_MAX || !tr_is_ident(key)) return -1;
    if (strlen(value) >= TR_TEXT_MAX || strchr(value, '\n') || strchr(value, '\r'))
        return -1;
    p = tr_prop_mut(w, key);
    if (!p) {
        if (w->nprops >= TR_PROPS_MAX) return -1;
        p = &w->props[w->nprops++];
        memset(p, 0, sizeof *p);
        if (tr_copy_cap(p->key, sizeof p->key, key) != 0) return -1;
    }
    return tr_copy_cap(p->value, sizeof p->value, value);
}

TrFieldType
tr_field_type(const char *type, int *nchars)
{
    char *end;
    long n;
    if (nchars) *nchars = 0;
    if (!type) return TR_FT_NONE;
    if (strcmp(type, "int") == 0) return TR_FT_INT;
    if (strcmp(type, "unsigned") == 0) return TR_FT_UINT;
    if (strcmp(type, "float") == 0) return TR_FT_FLOAT;
    if (strcmp(type, "double") == 0) return TR_FT_DOUBLE;
    if (strncmp(type, "char[", 5) != 0) return TR_FT_NONE;
    errno = 0;
    n = strtol(type + 5, &end, 10);
    if (errno || end == type + 5 || *end != ']' || end[1] != '\0') return TR_FT_NONE;
    if (n < 1 || n > 4096) return TR_FT_NONE;
    if (nchars) *nchars = (int)n;
    return TR_FT_CHARS;
}

const TrField *
tr_field_find(const TrDoc *doc, const char *name)
{
    int i;
    if (!doc || !name) return NULL;
    for (i = 0; i < doc->nfields; i++) {
        if (strcmp(doc->fields[i].name, name) == 0) return &doc->fields[i];
    }
    return NULL;
}

static int
int_token(const char *s, int allow_neg)
{
    const char *p = s;
    long v;
    char *end;
    if (!s || s[0] == '\0') return 1;
    if (*p == '-' && allow_neg) p++;
    if (*p < '0' || *p > '9') return 0;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0') return 0;
    if (allow_neg) {
        if (v > INT_MAX || v < INT_MIN) return 0;
    } else if (v < 0 || v > UINT_MAX) {
        return 0;
    }
    return 1;
}

static int
float_token(const char *s)
{
    int saw_digit = 0;
    int saw_dot = 0;
    if (!s || s[0] == '\0') return 1;
    if (*s == '-' || *s == '+') s++;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            saw_digit = 1;
            continue;
        }
        if (*s == '.' && !saw_dot) {
            saw_dot = 1;
            continue;
        }
        return 0;
    }
    return saw_digit;
}

static int
init_ok(const char *type, const char *init)
{
    int nchars = 0;
    TrFieldType t = tr_field_type(type, &nchars);
    if (t == TR_FT_NONE || !init) return 0;
    if (strchr(init, '\n') || strchr(init, '\r') || strlen(init) >= TR_TEXT_MAX) return 0;
    if (t == TR_FT_INT) return int_token(init, 1);
    if (t == TR_FT_UINT) return init[0] != '-' && int_token(init, 0);
    if (t == TR_FT_FLOAT || t == TR_FT_DOUBLE) return float_token(init);
    if (t == TR_FT_CHARS) return strlen(init) < (size_t)nchars;
    return 0;
}

int
tr_field_add(TrDoc *doc, const char *type, const char *name, const char *init)
{
    TrField *f;
    if (!doc || doc->nfields >= TR_FIELDS_MAX) return -1;
    if (!init) init = "";
    if (tr_field_type(type, NULL) == TR_FT_NONE) return -1;
    if (name_reserved(name) || tr_field_find(doc, name)) return -1;
    if (!init_ok(type, init)) return -1;
    f = &doc->fields[doc->nfields++];
    memset(f, 0, sizeof *f);
    tr_copy_cap(f->type, sizeof f->type, type);
    tr_copy_cap(f->name, sizeof f->name, name);
    tr_copy_cap(f->init, sizeof f->init, init);
    return doc->nfields - 1;
}

int
tr_field_remove(TrDoc *doc, int index)
{
    if (!doc || index < 0 || index >= doc->nfields) return -1;
    if (index + 1 < doc->nfields) {
        memmove(&doc->fields[index], &doc->fields[index + 1],
            (size_t)(doc->nfields - index - 1) * sizeof(TrField));
    }
    doc->nfields--;
    return 0;
}

static int
ident_char(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_';
}

int
tr_rewrite_expr(const TrDoc *doc, const char *in, char *out, size_t cap)
{
    size_t i = 0;
    size_t o = 0;
    if (!doc || !in || !out || cap == 0 || !tr_expr_ok(in)) return -1;
    while (in[i] != '\0') {
        if (in[i] == '"') {
            if (o + 1 >= cap) return -1;
            out[o++] = in[i++];
            while (in[i] != '\0' && in[i] != '"') {
                if (o + 1 >= cap) return -1;
                out[o++] = in[i];
                if (in[i] == '\\' && in[i + 1] != '\0') {
                    i++;
                    if (o + 1 >= cap) return -1;
                    out[o++] = in[i];
                }
                i++;
            }
            if (in[i] == '"') {
                if (o + 1 >= cap) return -1;
                out[o++] = in[i++];
            }
            continue;
        }
        if ((in[i] >= 'A' && in[i] <= 'Z') || (in[i] >= 'a' && in[i] <= 'z') || in[i] == '_') {
            size_t start = i;
            char word[64];
            size_t n;
            int prev;
            while (ident_char((unsigned char)in[i])) i++;
            n = i - start;
            if (n >= sizeof word) return -1;
            memcpy(word, in + start, n);
            word[n] = '\0';
            prev = start > 0 ? (unsigned char)in[start - 1] : 0;
            if (prev != '.' && prev != '>' && tr_field_find(doc, word)) {
                if (o + 4 + n >= cap) return -1;
                memcpy(out + o, "ui->", 4);
                o += 4;
            } else if (o + n >= cap) {
                return -1;
            }
            memcpy(out + o, word, n);
            o += n;
            continue;
        }
        if (o + 1 >= cap) return -1;
        out[o++] = in[i++];
    }
    out[o] = '\0';
    return 0;
}

static int
emit_user(const TrDoc *doc, TrBuf *out, const char *tag, TrSpan sp)
{
    const char *t = tr_span_ptr(doc, sp);
    if (tr_buf_printf(out, "user %s\n", tag) != 0) return -1;
    if (t[0] != '\0') {
        if (tr_buf_append_cstr(out, t) != 0) return -1;
        if (tr_buf_append_cstr(out, "\n") != 0) return -1;
    }
    return tr_buf_append_cstr(out, "enduser\n");
}

int
tr_doc_serialize(const TrDoc *doc, TrBuf *out)
{
    int i;
    int p;
    if (!doc || !out) return -1;
    if (tr_buf_printf(out, "version 1\nclass %s\nbackend %s\n",
            doc->class_name, tr_backend_name(doc->backend)) != 0)
        return -1;
    if (tr_buf_printf(out, "title %s\nunit %s\nscale_milli %d\nstyle %s\nactive %s\n",
            doc->title, doc->unit_dp ? "dp" : "px", doc->scale_milli,
            doc->style_name, doc->active_config) != 0)
        return -1;
    for (i = 0; i < doc->nconfigs; i++) {
        if (tr_buf_printf(out, "config %s\n", doc->configs[i]) != 0) return -1;
    }
    for (i = 0; i < doc->nfields; i++) {
        if (tr_buf_printf(out, "field %s %s |%s\n", doc->fields[i].type,
                doc->fields[i].name, doc->fields[i].init) != 0)
            return -1;
    }
    for (i = 0; i < doc->nwidgets; i++) {
        const TrWidget *w = &doc->widgets[i];
        if (tr_buf_printf(out,
                "widget %d %s %d %s %d %d %d %d %d %d %d %d |%s\n",
                w->id, tr_kind_name(w->kind), w->parent, w->name, w->newline,
                w->stretch, w->x, w->y, w->w, w->h, w->overlay, w->anchor,
                w->config) != 0)
            return -1;
        for (p = 0; p < w->nprops; p++) {
            const TrProp *pr = &w->props[p];
            if (tr_buf_printf(out, "prop %d %s %d |%s|%s\n", w->id, pr->key,
                    pr->twoway ? 1 : 0, pr->bind, pr->value) != 0)
                return -1;
        }
        for (p = 0; p < w->nevents; p++) {
            const TrEvent *e = &w->events[p];
            if (tr_buf_printf(out, "event %d %s %s\n", w->id, e->name,
                    e->handler[0] ? e->handler : "-") != 0)
                return -1;
        }
    }
    if (emit_user(doc, out, "header", doc->user_header) != 0) return -1;
    if (emit_user(doc, out, "fields", doc->user_fields) != 0) return -1;
    if (emit_user(doc, out, "draw-pre", doc->user_draw_pre) != 0) return -1;
    if (emit_user(doc, out, "draw-post", doc->user_draw_post) != 0) return -1;
    if (emit_user(doc, out, "cell", doc->user_cell) != 0) return -1;
    for (i = 0; i < doc->nwidgets; i++) {
        const TrWidget *w = &doc->widgets[i];
        for (p = 0; p < w->nevents; p++) {
            char tag[96];
            int nw = snprintf(tag, sizeof tag, "event %d %s", w->id, w->events[p].name);
            if (nw < 0 || (size_t)nw >= sizeof tag) return -1;
            if (emit_user(doc, out, tag, w->events[p].user) != 0) return -1;
        }
    }
    return 0;
}

typedef struct TrScan {
    const char *p;
    const char *end;
    int line;
} TrScan;

static int
scan_line(TrScan *s, char *buf, size_t cap)
{
    size_t n = 0;
    if (!s || s->p >= s->end) return 1;
    while (s->p < s->end && *s->p != '\n') {
        if (*s->p == '\0') return -1;
        if (n + 1 >= cap) return -1;
        buf[n++] = *s->p++;
    }
    if (s->p < s->end && *s->p == '\n') s->p++;
    if (n && buf[n - 1] == '\r') n--;
    buf[n] = '\0';
    s->line++;
    return 0;
}

static int
take_tok(const char **p, char *dst, size_t cap)
{
    const char *s = *p;
    size_t n = 0;
    while (*s == ' ') s++;
    if (*s == '\0' || *s == '|') return -1;
    while (*s != '\0' && *s != ' ' && *s != '|') {
        if (n + 1 >= cap) return -1;
        dst[n++] = *s++;
    }
    dst[n] = '\0';
    *p = s;
    return 0;
}

static int
take_i(const char **p, int *out)
{
    char tok[32];
    char *end;
    long v;
    if (take_tok(p, tok, sizeof tok) != 0) return -1;
    errno = 0;
    v = strtol(tok, &end, 10);
    if (errno || end == tok || *end != '\0' || v > 1000000 || v < -1000000) return -1;
    *out = (int)v;
    return 0;
}

static TrSpan *
user_slot(TrDoc *doc, const char *tag, char *err, size_t errcap, int line)
{
    int id = 0;
    char name[TR_NAME_MAX];
    char kind[32];
    TrWidget *w;
    int i;
    if (strcmp(tag, "header") == 0) return &doc->user_header;
    if (strcmp(tag, "fields") == 0) return &doc->user_fields;
    if (strcmp(tag, "draw-pre") == 0) return &doc->user_draw_pre;
    if (strcmp(tag, "draw-post") == 0) return &doc->user_draw_post;
    if (strcmp(tag, "cell") == 0) return &doc->user_cell;
    if (sscanf(tag, "event %d %31s", &id, name) == 2) {
        w = tr_doc_widget(doc, id);
        if (!w) {
            set_err(err, errcap, line, "user event refers to a missing widget");
            return NULL;
        }
        for (i = 0; i < w->nevents; i++) {
            if (strcmp(w->events[i].name, name) == 0) return &w->events[i].user;
        }
        set_err(err, errcap, line, "user event name is not on that widget");
        return NULL;
    }
    (void)kind;
    set_err(err, errcap, line, "unknown user slot");
    return NULL;
}

static int
parse_into(TrDoc *doc, const char *text, size_t len, char *err, size_t errcap)
{
    TrScan scan;
    char line[2048];
    int saw_version = 0;
    int saw_class = 0;
    int rc;
    memset(doc->class_name, 0, sizeof doc->class_name);
    tr_copy_cap(doc->style_name, sizeof doc->style_name, "default");
    doc->scale_milli = 1000;
    doc->backend = TR_BACKEND_NUKLEAR;
    doc->version = 1;
    scan.p = text;
    scan.end = text + len;
    scan.line = 0;
    while ((rc = scan_line(&scan, line, sizeof line)) == 0) {
        const char *p;
        char key[32];
        if (line[0] == '\0' || line[0] == '#') continue;
        if (strcmp(line, "enduser") == 0) {
            set_err(err, errcap, scan.line, "enduser without a user block");
            return -1;
        }
        p = line;
        if (take_tok(&p, key, sizeof key) != 0) {
            set_err(err, errcap, scan.line, "empty record");
            return -1;
        }
        if (strcmp(key, "user") == 0) {
            char tag[96];
            TrBuf body;
            TrSpan *slot;
            while (*p == ' ') p++;
            if (tr_copy_cap(tag, sizeof tag, p) != 0) {
                set_err(err, errcap, scan.line, "user tag is too long");
                return -1;
            }
            slot = user_slot(doc, tag, err, errcap, scan.line);
            if (!slot) return -1;
            if (tr_buf_init(&body) != 0) return -1;
            for (;;) {
                int ur = scan_line(&scan, line, sizeof line);
                if (ur != 0) {
                    tr_buf_free(&body);
                    set_err(err, errcap, scan.line, "user block is not closed");
                    return -1;
                }
                if (strcmp(line, "enduser") == 0) break;
                if (body.len > 0 && tr_buf_append_cstr(&body, "\n") != 0) {
                    tr_buf_free(&body);
                    return -1;
                }
                if (tr_buf_append_cstr(&body, line) != 0) {
                    tr_buf_free(&body);
                    return -1;
                }
            }
            if (tr_span_set(doc, slot, body.data, body.len) != 0) {
                tr_buf_free(&body);
                set_err(err, errcap, scan.line, "user text is not allowed");
                return -1;
            }
            tr_buf_free(&body);
            continue;
        }
        if (strcmp(key, "version") == 0) {
            int v = 0;
            if (take_i(&p, &v) != 0 || v != 1) {
                set_err(err, errcap, scan.line, "unsupported version");
                return -1;
            }
            saw_version = 1;
        } else if (strcmp(key, "class") == 0) {
            char name[TR_NAME_MAX];
            if (take_tok(&p, name, sizeof name) != 0 || name_reserved(name)) {
                set_err(err, errcap, scan.line, "class name is not an identifier");
                return -1;
            }
            tr_copy_cap(doc->class_name, sizeof doc->class_name, name);
            saw_class = 1;
        } else if (strcmp(key, "backend") == 0) {
            char name[32];
            if (take_tok(&p, name, sizeof name) != 0) return -1;
            if (strcmp(name, "nuklear") && strcmp(name, "libui") && strcmp(name, "lvgl")) {
                set_err(err, errcap, scan.line, "unknown backend");
                return -1;
            }
            doc->backend = tr_backend_from_name(name);
        } else if (strcmp(key, "title") == 0) {
            while (*p == ' ') p++;
            if (tr_copy_cap(doc->title, sizeof doc->title, p) != 0) {
                set_err(err, errcap, scan.line, "title is too long");
                return -1;
            }
        } else if (strcmp(key, "unit") == 0) {
            char name[16];
            if (take_tok(&p, name, sizeof name) != 0) return -1;
            if (strcmp(name, "px") == 0) doc->unit_dp = 0;
            else if (strcmp(name, "dp") == 0) doc->unit_dp = 1;
            else {
                set_err(err, errcap, scan.line, "unit must be px or dp");
                return -1;
            }
        } else if (strcmp(key, "scale_milli") == 0) {
            int v = 0;
            if (take_i(&p, &v) != 0 || v < 100 || v > 8000) {
                set_err(err, errcap, scan.line, "scale_milli is out of range");
                return -1;
            }
            doc->scale_milli = v;
        } else if (strcmp(key, "style") == 0) {
            char name[TR_NAME_MAX];
            if (take_tok(&p, name, sizeof name) != 0) return -1;
            tr_copy_cap(doc->style_name, sizeof doc->style_name, name);
        } else if (strcmp(key, "active") == 0) {
            while (*p == ' ') p++;
            if (tr_copy_cap(doc->active_config, sizeof doc->active_config, p) != 0) return -1;
        } else if (strcmp(key, "config") == 0) {
            char name[TR_NAME_MAX];
            if (doc->nconfigs >= TR_CONFIGS_MAX) {
                set_err(err, errcap, scan.line, "too many configurations");
                return -1;
            }
            if (take_tok(&p, name, sizeof name) != 0 || !token_ok(name)) {
                set_err(err, errcap, scan.line, "configuration name is invalid");
                return -1;
            }
            tr_copy_cap(doc->configs[doc->nconfigs], TR_NAME_MAX, name);
            doc->nconfigs++;
        } else if (strcmp(key, "field") == 0) {
            char type[TR_TYPE_MAX];
            char name[TR_NAME_MAX];
            if (take_tok(&p, type, sizeof type) != 0 || take_tok(&p, name, sizeof name) != 0) {
                set_err(err, errcap, scan.line, "field needs a type and a name");
                return -1;
            }
            while (*p == ' ') p++;
            if (*p != '|') {
                set_err(err, errcap, scan.line, "field is missing '|'");
                return -1;
            }
            p++;
            if (tr_field_add(doc, type, name, p) < 0) {
                set_err(err, errcap, scan.line, "field is invalid");
                return -1;
            }
        } else if (strcmp(key, "widget") == 0) {
            TrWidget *w;
            char kind_name[32];
            char wname[TR_NAME_MAX];
            int id = 0, parent = 0, newline = 0, stretch = 0;
            int x = 0, y = 0, ww = 0, hh = 0, overlay = 0, anchor = 0;
            TrKind kind;
            if (doc->nwidgets >= TR_WIDGETS_MAX) {
                set_err(err, errcap, scan.line, "too many widgets");
                return -1;
            }
            if (take_i(&p, &id) || take_tok(&p, kind_name, sizeof kind_name) ||
                take_i(&p, &parent) || take_tok(&p, wname, sizeof wname) ||
                take_i(&p, &newline) || take_i(&p, &stretch) || take_i(&p, &x) ||
                take_i(&p, &y) || take_i(&p, &ww) || take_i(&p, &hh) ||
                take_i(&p, &overlay) || take_i(&p, &anchor)) {
                set_err(err, errcap, scan.line, "widget record is incomplete");
                return -1;
            }
            kind = tr_kind_from_name(kind_name);
            if (kind == TR_KIND_NONE || id <= 0 || name_reserved(wname)) {
                set_err(err, errcap, scan.line, "widget kind or name is invalid");
                return -1;
            }
            if (tr_doc_index(doc, id) >= 0 || widget_name_taken(doc, wname, -1)) {
                set_err(err, errcap, scan.line, "duplicate widget id or name");
                return -1;
            }
            while (*p == ' ') p++;
            if (*p != '|') {
                set_err(err, errcap, scan.line, "widget is missing '|'");
                return -1;
            }
            p++;
            w = &doc->widgets[doc->nwidgets++];
            memset(w, 0, sizeof *w);
            w->id = id;
            w->parent = parent;
            w->kind = kind;
            tr_copy_cap(w->name, sizeof w->name, wname);
            w->newline = newline ? 1 : 0;
            w->stretch = stretch;
            w->x = x;
            w->y = y;
            w->w = ww;
            w->h = hh;
            w->overlay = overlay ? 1 : 0;
            w->anchor = anchor < 0 ? 0 : (anchor > 3 ? 3 : anchor);
            if (tr_copy_cap(w->config, sizeof w->config, p) != 0) {
                set_err(err, errcap, scan.line, "widget config is too long");
                return -1;
            }
            fill_defaults(w);
            if (id >= doc->next_id) doc->next_id = id + 1;
        } else if (strcmp(key, "prop") == 0) {
            int id = 0;
            int two = 0;
            char pkey[TR_NAME_MAX];
            TrWidget *w;
            TrProp *pr;
            const char *bind;
            const char *value;
            if (take_i(&p, &id) || take_tok(&p, pkey, sizeof pkey) || take_i(&p, &two)) {
                set_err(err, errcap, scan.line, "prop record is incomplete");
                return -1;
            }
            while (*p == ' ') p++;
            if (*p != '|') {
                set_err(err, errcap, scan.line, "prop is missing '|'");
                return -1;
            }
            bind = ++p;
            value = strchr(bind, '|');
            if (!value) {
                set_err(err, errcap, scan.line, "prop is missing the value bar");
                return -1;
            }
            w = tr_doc_widget(doc, id);
            pr = w ? tr_prop_mut(w, pkey) : NULL;
            if (!w || !pr) {
                set_err(err, errcap, scan.line, "prop does not belong to a widget");
                return -1;
            }
            if ((size_t)(value - bind) >= TR_EXPR_MAX) {
                set_err(err, errcap, scan.line, "binding is too long");
                return -1;
            }
            memcpy(pr->bind, bind, (size_t)(value - bind));
            pr->bind[value - bind] = '\0';
            if (pr->bind[0] && !tr_expr_ok(pr->bind)) {
                set_err(err, errcap, scan.line, "binding expression is not allowed");
                return -1;
            }
            if (tr_prop_set(w, pkey, value + 1) != 0) {
                set_err(err, errcap, scan.line, "prop value is invalid");
                return -1;
            }
            pr->twoway = two ? 1 : 0;
        } else if (strcmp(key, "event") == 0) {
            int id = 0;
            char ename[TR_NAME_MAX];
            char handler[TR_NAME_MAX];
            TrWidget *w;
            int ei;
            if (take_i(&p, &id) || take_tok(&p, ename, sizeof ename) ||
                take_tok(&p, handler, sizeof handler)) {
                set_err(err, errcap, scan.line, "event record is incomplete");
                return -1;
            }
            w = tr_doc_widget(doc, id);
            if (!w) {
                set_err(err, errcap, scan.line, "event refers to a missing widget");
                return -1;
            }
            if (strcmp(handler, "-") != 0 && !tr_is_ident(handler)) {
                set_err(err, errcap, scan.line, "handler name is not an identifier");
                return -1;
            }
            for (ei = 0; ei < w->nevents; ei++) {
                if (strcmp(w->events[ei].name, ename) == 0) {
                    if (strcmp(handler, "-") == 0) w->events[ei].handler[0] = '\0';
                    else tr_copy_cap(w->events[ei].handler, sizeof w->events[ei].handler, handler);
                    break;
                }
            }
            if (ei == w->nevents) {
                set_err(err, errcap, scan.line, "event is not defined for that widget");
                return -1;
            }
        } else {
            set_err(err, errcap, scan.line, "unknown record");
            return -1;
        }
    }
    if (rc < 0) {
        set_err(err, errcap, scan.line, "line is too long or contains a NUL");
        return -1;
    }
    if (!saw_version || !saw_class) {
        set_err(err, errcap, 0, "document needs version and class");
        return -1;
    }
    if (doc->title[0] == '\0')
        tr_copy_cap(doc->title, sizeof doc->title, doc->class_name);
    if (doc->nconfigs == 0) {
        tr_copy_cap(doc->configs[0], TR_NAME_MAX, "default");
        doc->nconfigs = 1;
    }
    if (doc->active_config[0] == '\0')
        tr_copy_cap(doc->active_config, sizeof doc->active_config, doc->configs[0]);
    {
        int i;
        int roots = 0;
        int known = 0;
        for (i = 0; i < doc->nconfigs; i++) {
            if (strcmp(doc->configs[i], doc->active_config) == 0) known = 1;
        }
        if (!known) {
            set_err(err, errcap, 0, "active configuration is not listed");
            return -1;
        }
        for (i = 0; i < doc->nwidgets; i++) {
            int guard = 0;
            int parent = doc->widgets[i].parent;
            if (parent == -1) {
                roots++;
                if (!tr_kind_is_window(doc->widgets[i].kind)) {
                    set_err(err, errcap, 0, "root widget must be a window");
                    return -1;
                }
            } else if (tr_doc_index(doc, parent) < 0) {
                set_err(err, errcap, 0, "widget parent is missing");
                return -1;
            }
            while (parent != -1 && guard++ < TR_WIDGETS_MAX + 1) {
                int pi = tr_doc_index(doc, parent);
                if (pi < 0) break;
                parent = doc->widgets[pi].parent;
            }
            if (guard > TR_WIDGETS_MAX) {
                set_err(err, errcap, 0, "widget parent cycle");
                return -1;
            }
            if (doc->widgets[i].config[0] && !token_ok(doc->widgets[i].config)) {
                set_err(err, errcap, 0, "widget configuration name is invalid");
                return -1;
            }
        }
        if (roots != 1) {
            set_err(err, errcap, 0, "document needs exactly one root widget");
            return -1;
        }
    }
    return 0;
}

static void
adopt(TrDoc *dst, TrDoc *src)
{
    free(dst->arena);
    *dst = *src;
    src->arena = NULL;
    free(src);
}

int
tr_doc_parse(TrDoc *doc, const char *text, size_t len, char *err, size_t errcap)
{
    TrDoc *tmp;
    if (err && errcap) err[0] = '\0';
    if (!doc || !text) return -1;
    tmp = tr_doc_new();
    if (!tmp) return -1;
    if (parse_into(tmp, text, len, err, errcap) != 0) {
        tr_doc_free(tmp);
        return -1;
    }
    adopt(doc, tmp);
    return 0;
}

int
tr_doc_overlay_text(TrDoc *doc, const char *text, char *err, size_t errcap)
{
    const char *p = text;
    while (p && (p = strstr(p, "/* @tccrad user ")) != NULL) {
        const char *tagp = p + (int)strlen("/* @tccrad user ");
        const char *close = strstr(tagp, "*/");
        const char *nl;
        const char *end;
        char tag[96];
        size_t tag_n;
        const char *body;
        size_t body_n;
        TrSpan *slot;
        if (!close) {
            set_err(err, errcap, 0, "user marker is not closed");
            return -1;
        }
        tag_n = (size_t)(close - tagp);
        while (tag_n && tagp[tag_n - 1] == ' ') tag_n--;
        if (tag_n >= sizeof tag) return -1;
        memcpy(tag, tagp, tag_n);
        tag[tag_n] = '\0';
        nl = strchr(close, '\n');
        end = strstr(close, "/* @end user */");
        if (!nl || !end || end < nl) {
            set_err(err, errcap, 0, "user region is missing its end marker");
            return -1;
        }
        body = nl + 1;
        body_n = (size_t)(end - body);
        if (body_n && body[body_n - 1] == '\n') body_n--;
        slot = user_slot(doc, tag, err, errcap, 0);
        if (!slot) return -1;
        {
            char *tmp = (char *)malloc(body_n + 1);
            if (!tmp) return -1;
            memcpy(tmp, body, body_n);
            tmp[body_n] = '\0';
            if (tr_span_set(doc, slot, tmp, body_n) != 0) {
                free(tmp);
                set_err(err, errcap, 0, "user region text is not allowed");
                return -1;
            }
            free(tmp);
        }
        p = end + 1;
    }
    return 0;
}

int
tr_doc_load_text(TrDoc *doc, const char *text, size_t len, char *err, size_t errcap)
{
    const char *inner = text;
    size_t inner_len = len;
    const char *mark;
    const char *end;
    TrDoc *tmp;
    if (err && errcap) err[0] = '\0';
    if (!doc || !text) return -1;
    mark = strstr(text, "@tccrad-doc");
    if (mark) {
        inner = strchr(mark, '\n');
        if (!inner) {
            set_err(err, errcap, 0, "document marker is truncated");
            return -1;
        }
        inner++;
        end = strstr(inner, "@end tccrad-doc");
        if (!end || end < inner || (size_t)(end - text) > len) {
            set_err(err, errcap, 0, "document end marker is missing");
            return -1;
        }
        inner_len = (size_t)(end - inner);
    }
    tmp = tr_doc_new();
    if (!tmp) return -1;
    if (parse_into(tmp, inner, inner_len, err, errcap) != 0) {
        tr_doc_free(tmp);
        return -1;
    }
    if (mark && tr_doc_overlay_text(tmp, text, err, errcap) != 0) {
        tr_doc_free(tmp);
        return -1;
    }
    adopt(doc, tmp);
    return 0;
}

void
tr_doc_blank(TrDoc *doc)
{
    char *arena;
    size_t cap;
    int root;
    int id;
    TrWidget *w;
    TrProp *p;
    if (!doc || !doc->arena) return;
    arena = doc->arena;
    cap = doc->arena_cap;
    memset(doc, 0, sizeof *doc);
    doc->arena = arena;
    doc->arena_cap = cap;
    doc->arena_len = 1;
    doc->arena[0] = '\0';
    doc->version = 1;
    doc->next_id = 1;
    doc->backend = TR_BACKEND_NUKLEAR;
    doc->scale_milli = 1000;
    tr_copy_cap(doc->class_name, sizeof doc->class_name, "App");
    tr_copy_cap(doc->title, sizeof doc->title, "App");
    tr_copy_cap(doc->style_name, sizeof doc->style_name, "default");
    tr_copy_cap(doc->configs[0], TR_NAME_MAX, "default");
    doc->nconfigs = 1;
    tr_copy_cap(doc->active_config, sizeof doc->active_config, "default");
    tr_field_add(doc, "int", "show_all", "1");
    tr_field_add(doc, "char[64]", "name", "");
    root = tr_doc_add_widget(doc, -1, TR_KIND_MAIN, "root");
    w = tr_doc_widget(doc, root);
    if (!w) return;
    w->x = 80;
    w->y = 60;
    w->w = 480;
    w->h = 300;
    tr_prop_set(w, "title", "App");
    id = tr_doc_add_widget(doc, root, TR_KIND_LABEL, "name_lbl");
    w = tr_doc_widget(doc, id);
    if (w) tr_prop_set(w, "text", "Name");
    id = tr_doc_add_widget(doc, root, TR_KIND_INPUT, "name_in");
    w = tr_doc_widget(doc, id);
    if (w) {
        p = tr_prop_mut(w, "text");
        if (p) {
            tr_copy_cap(p->bind, sizeof p->bind, "name");
            p->twoway = 1;
        }
        tr_prop_set(w, "capacity", "64");
    }
    id = tr_doc_add_widget(doc, root, TR_KIND_CHECKBOX, "show_chk");
    w = tr_doc_widget(doc, id);
    if (w) {
        tr_prop_set(w, "label", "Show all");
        p = tr_prop_mut(w, "checked");
        if (p) {
            tr_copy_cap(p->bind, sizeof p->bind, "show_all");
            p->twoway = 1;
        }
    }
    id = tr_doc_add_widget(doc, root, TR_KIND_SPACER, "gap");
    w = tr_doc_widget(doc, id);
    if (w) w->stretch = 1;
    id = tr_doc_add_widget(doc, root, TR_KIND_BUTTON, "ok_btn");
    w = tr_doc_widget(doc, id);
    if (w) {
        w->newline = 0;
        tr_prop_set(w, "label", "OK");
    }
    id = tr_doc_add_widget(doc, root, TR_KIND_BUTTON, "cancel_btn");
    w = tr_doc_widget(doc, id);
    if (w) {
        w->newline = 0;
        tr_prop_set(w, "label", "Cancel");
    }
}
