#ifndef TCCRAD_MODEL_H
#define TCCRAD_MODEL_H

#include "safe.h"

#include <stddef.h>

#define TR_NAME_MAX 48
#define TR_TEXT_MAX 160
#define TR_EXPR_MAX 96
#define TR_TYPE_MAX 32
#define TR_PROPS_MAX 10
#define TR_EVENTS_MAX 3
#define TR_WIDGETS_MAX 128
#define TR_FIELDS_MAX 48
#define TR_CONFIGS_MAX 8
#define TR_ROW_COLS 16
#define TR_USER_MAX 32768
#define TR_ARENA_MAX (256 * 1024)
#define TR_FILE_MAX (1024 * 1024)

typedef enum TrBackend {
    TR_BACKEND_NUKLEAR = 1,
    TR_BACKEND_LIBUI = 2,
    TR_BACKEND_LVGL = 3
} TrBackend;

typedef enum TrKind {
    TR_KIND_NONE = 0,
    TR_KIND_WINDOW,
    TR_KIND_POPUP,
    TR_KIND_MODAL,
    TR_KIND_MAIN,
    TR_KIND_ACTIVITY,
    TR_KIND_LABEL,
    TR_KIND_BUTTON,
    TR_KIND_CHECKBOX,
    TR_KIND_RADIO,
    TR_KIND_INPUT,
    TR_KIND_SLIDER,
    TR_KIND_COMBO,
    TR_KIND_PROGRESS,
    TR_KIND_COLOR,
    TR_KIND_SEPARATOR,
    TR_KIND_SPACER,
    TR_KIND_GROUP,
    TR_KIND_TABS,
    TR_KIND_TAB,
    TR_KIND_COLLAPSE,
    TR_KIND_TREE,
    TR_KIND_TABLE,
    TR_KIND_MENUBAR,
    TR_KIND_MENU,
    TR_KIND_MENUITEM,
    TR_KIND_IMAGE,
    TR_KIND_CUSTOM,
    TR_KIND_SPLITTER,
    TR_KIND_CHILD,
    TR_KIND_DOCK
} TrKind;

typedef enum TrFieldType {
    TR_FT_NONE = 0,
    TR_FT_INT,
    TR_FT_UINT,
    TR_FT_FLOAT,
    TR_FT_DOUBLE,
    TR_FT_CHARS
} TrFieldType;

typedef enum TrVal {
    TR_VAL_STRING = 0,
    TR_VAL_INT,
    TR_VAL_BOOL
} TrVal;

typedef struct TrPropDef {
    const char *key;
    TrVal val;
    int bindable;
    int twoway_ok;
    const char *def;
} TrPropDef;

typedef struct TrSpan {
    unsigned off;
    unsigned len;
} TrSpan;

typedef struct TrProp {
    char key[TR_NAME_MAX];
    char value[TR_TEXT_MAX];
    char bind[TR_EXPR_MAX];
    int twoway;
} TrProp;

typedef struct TrEvent {
    char name[TR_NAME_MAX];
    char handler[TR_NAME_MAX];
    TrSpan user;
} TrEvent;

typedef struct TrWidget {
    int id;
    int parent;
    TrKind kind;
    char name[TR_NAME_MAX];
    int newline;
    int stretch;
    int x, y, w, h;
    int overlay;
    int anchor;
    char config[TR_NAME_MAX];
    TrProp props[TR_PROPS_MAX];
    int nprops;
    TrEvent events[TR_EVENTS_MAX];
    int nevents;
} TrWidget;

typedef struct TrField {
    char type[TR_TYPE_MAX];
    char name[TR_NAME_MAX];
    char init[TR_TEXT_MAX];
} TrField;

typedef struct TrDoc {
    int version;
    char class_name[TR_NAME_MAX];
    char title[TR_TEXT_MAX];
    TrBackend backend;
    char style_name[TR_NAME_MAX];
    int unit_dp;
    int scale_milli;
    int spaced;
    char active_config[TR_NAME_MAX];
    char configs[TR_CONFIGS_MAX][TR_NAME_MAX];
    int nconfigs;
    TrField fields[TR_FIELDS_MAX];
    int nfields;
    TrWidget widgets[TR_WIDGETS_MAX];
    int nwidgets;
    int next_id;
    char *arena;
    size_t arena_len;
    size_t arena_cap;
    TrSpan user_header;
    TrSpan user_fields;
    TrSpan user_draw_pre;
    TrSpan user_draw_post;
    TrSpan user_cell;
} TrDoc;

typedef struct TrRow {
    int at[TR_ROW_COLS];
    int n;
    int height;
    int stretch;
} TrRow;

TrDoc *tr_doc_new(void);
void tr_doc_free(TrDoc *doc);
void tr_doc_blank(TrDoc *doc);

const char *tr_kind_name(TrKind kind);
TrKind tr_kind_from_name(const char *name);
int tr_kind_is_window(TrKind kind);
int tr_kind_is_container(TrKind kind);
const TrPropDef *tr_prop_defs(TrKind kind, int *n);
const char *const *tr_event_defs(TrKind kind, int *n);

int tr_doc_add_widget(TrDoc *doc, int parent_id, TrKind kind, const char *name);
int tr_doc_delete(TrDoc *doc, int id);
int tr_doc_duplicate(TrDoc *doc, int id);
int tr_doc_move(TrDoc *doc, int id, int dir);
int tr_doc_set_root_kind(TrDoc *doc, TrKind kind);
int tr_parent_ok(TrKind parent_kind, TrKind child_kind);

int tr_doc_index(const TrDoc *doc, int id);
TrWidget *tr_doc_widget(TrDoc *doc, int id);
const TrWidget *tr_doc_widget_c(const TrDoc *doc, int id);

int tr_child_indices(const TrDoc *doc, int parent_id, int *out, int max);
int tr_flow_indices(const TrDoc *doc, int parent_id, int *out, int max);
int tr_pack_rows(const TrDoc *doc, const int *idxs, int n, TrRow *rows, int max_rows);

const TrProp *tr_prop(const TrWidget *w, const char *key);
TrProp *tr_prop_mut(TrWidget *w, const char *key);
int tr_prop_int(const TrWidget *w, const char *key, int def);
int tr_prop_set(TrWidget *w, const char *key, const char *value);

int tr_field_add(TrDoc *doc, const char *type, const char *name, const char *init);
int tr_field_remove(TrDoc *doc, int index);
TrFieldType tr_field_type(const char *type, int *nchars);
const TrField *tr_field_find(const TrDoc *doc, const char *name);

int tr_span_set(TrDoc *doc, TrSpan *slot, const char *text, size_t n);
const char *tr_span_ptr(const TrDoc *doc, TrSpan slot);

int tr_rewrite_expr(const TrDoc *doc, const char *in, char *out, size_t cap);

int tr_doc_serialize(const TrDoc *doc, TrBuf *out);
int tr_doc_parse(TrDoc *doc, const char *text, size_t len, char *err, size_t errcap);
int tr_doc_load_text(TrDoc *doc, const char *text, size_t len, char *err, size_t errcap);
int tr_doc_overlay_text(TrDoc *doc, const char *text, char *err, size_t errcap);

const char *tr_backend_name(TrBackend b);
TrBackend tr_backend_from_name(const char *s);

#endif
