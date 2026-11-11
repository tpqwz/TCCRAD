#include "codegen_priv.h"

#include "tccrad.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int name_reserved_local(const char *name);

int
cg_err(Gen *g, const char *msg)
{
    if (g && g->err && g->errcap && g->err[0] == '\0')
        tr_copy_cap(g->err, g->errcap, msg ? msg : "codegen failed");
    return -1;
}

int
cg_gp(Gen *g, const char *fmt, ...)
{
    va_list ap;
    int rc;
    if (!g || !g->o || !fmt) return -1;
    va_start(ap, fmt);
    rc = tr_buf_vprintf(g->o, fmt, ap);
    va_end(ap);
    return rc;
}

int
cg_gi(Gen *g, int depth, const char *fmt, ...)
{
    va_list ap;
    int rc;
    if (!g || !g->o || !fmt) return -1;
    if (depth < 0) depth = 0;
    if (depth > 48) depth = 48;
    if (tr_buf_printf(g->o, "%*s", depth * 4, "") != 0) return -1;
    va_start(ap, fmt);
    rc = tr_buf_vprintf(g->o, fmt, ap);
    va_end(ap);
    return rc;
}

int
cg_cstr(Gen *g, const char *s)
{
    const unsigned char *p;
    if (cg_gp(g, "\"") != 0) return -1;
    for (p = (const unsigned char *)(s ? s : ""); *p; p++) {
        if (*p == '\\' || *p == '"') {
            if (cg_gp(g, "\\%c", (int)*p) != 0) return -1;
        } else if (*p == '\n') {
            if (cg_gp(g, "\\n") != 0) return -1;
        } else if (*p < 32 || *p > 126) {
            if (cg_gp(g, "\\%03o", (unsigned)*p) != 0) return -1;
        } else if (cg_gp(g, "%c", (int)*p) != 0) {
            return -1;
        }
    }
    return cg_gp(g, "\"");
}

int
cg_text(Gen *g, const TrWidget *w, const char *key)
{
    const TrProp *p = tr_prop(w, key);
    char rew[512];
    if (!p || p->bind[0] == '\0') return cg_cstr(g, p ? p->value : "");
    if (tr_rewrite_expr(g->doc, p->bind, rew, sizeof rew) != 0)
        return cg_err(g, "a text binding is not allowed");
    return cg_gp(g, "(%s)", rew);
}

int
cg_region(Gen *g, const char *tag, TrSpan sp, const char *fallback)
{
    const char *t = tr_span_ptr(g->doc, sp);
    if (cg_gp(g, "/* @tccrad user %s */\n", tag) != 0) return -1;
    if (t && t[0] != '\0') {
        if (cg_gp(g, "%s\n", t) != 0) return -1;
    } else if (fallback && fallback[0] != '\0') {
        if (cg_gp(g, "%s\n", fallback) != 0) return -1;
    }
    return cg_gp(g, "/* @end user */\n");
}

const TrEvent *
cg_event(const TrWidget *w, const char *name)
{
    int i;
    if (!w || !name) return NULL;
    for (i = 0; i < w->nevents; i++) {
        if (strcmp(w->events[i].name, name) == 0) return &w->events[i];
    }
    return NULL;
}

int
cg_handler(const TrDoc *doc, const TrWidget *w, const TrEvent *ev, char *dst, size_t cap)
{
    int n;
    if (!doc || !w || !ev || !dst || cap == 0) return -1;
    if (ev->handler[0] != '\0') {
        if (!tr_is_ident(ev->handler)) return -1;
        return tr_copy_cap(dst, cap, ev->handler);
    }
    n = snprintf(dst, cap, "%s_%s_%s", doc->class_name, w->name, ev->name);
    if (n < 0 || (size_t)n >= cap || !tr_is_ident(dst)) return -1;
    return 0;
}

int
cg_int_slot(Gen *g, const TrWidget *w, const char *key, CgSlot *s)
{
    const TrProp *p;
    int n;
    if (!g || !w || !key || !s) return -1;
    memset(s, 0, sizeof *s);
    p = tr_prop(w, key);
    if (p && p->twoway) {
        const TrField *f = tr_field_find(g->doc, p->bind);
        TrFieldType t = f ? tr_field_type(f->type, NULL) : TR_FT_NONE;
        if (!tr_is_ident(p->bind) || (t != TR_FT_INT && t != TR_FT_UINT))
            return cg_err(g, "two-way binding must name an int field");
        n = snprintf(s->lv, sizeof s->lv, "ui->%s", p->bind);
        if (n < 0 || (size_t)n >= sizeof s->lv) return -1;
        return 0;
    }
    if (strcmp(key, "open") == 0 && w->parent < 0) {
        if (tr_copy_cap(s->lv, sizeof s->lv, "ui->tr_open") != 0) return -1;
        return 0;
    }
    n = snprintf(s->lv, sizeof s->lv, "ui->tr_p_%d_%s", w->id, key);
    if (n < 0 || (size_t)n >= sizeof s->lv || !tr_is_ident(s->lv + 4)) return -1;
    if (p && p->bind[0] != '\0' && strcmp(key, "open") != 0) {
        if (tr_rewrite_expr(g->doc, p->bind, s->preload, sizeof s->preload) != 0)
            return cg_err(g, "binding expression is not allowed");
        s->preload_on = 1;
    }
    return 0;
}

static int
include_ok(const char *s)
{
    size_t i;
    if (!s || s[0] == '\0' || strlen(s) > 64) return 0;
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
            return 0;
    }
    return strstr(s, "..") == NULL;
}

static int
handler_ok(const TrDoc *doc)
{
    char names[TR_WIDGETS_MAX * TR_EVENTS_MAX][TR_NAME_MAX];
    int n = 0;
    int i;
    int e;
    int a;
    int b;
    for (i = 0; i < doc->nwidgets; i++) {
        const TrWidget *w = &doc->widgets[i];
        for (e = 0; e < w->nevents; e++) {
            if (n >= (int)(sizeof names / sizeof names[0])) return -1;
            if (cg_handler(doc, w, &w->events[e], names[n], TR_NAME_MAX) != 0) return -1;
            n++;
        }
    }
    for (a = 0; a < n; a++) {
        for (b = a + 1; b < n; b++) {
            if (strcmp(names[a], names[b]) == 0) return -1;
        }
    }
    return 0;
}

static int
cg_check(Gen *g)
{
    int i;
    if (!g->doc || g->doc->nwidgets < 1) return cg_err(g, "document has no widgets");
    if (name_reserved_local(g->doc->class_name)) return cg_err(g, "class name is not usable");
    if (!tr_kind_is_window(g->doc->widgets[0].kind) || g->doc->widgets[0].parent != -1)
        return cg_err(g, "the root widget must be a window");
    if (handler_ok(g->doc) != 0) return cg_err(g, "event handler names collide or are invalid");
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *w = &g->doc->widgets[i];
        int p;
        if (w->kind == TR_KIND_CUSTOM) {
            const TrProp *fn = tr_prop(w, "func");
            const TrEvent *ev = cg_event(w, "draw");
            if (ev && ev->handler[0] == '\0' && fn && !tr_is_ident(fn->value))
                return cg_err(g, "custom widget function name is invalid");
        }
        if (w->kind == TR_KIND_INPUT && tr_prop(w, "text") && tr_prop(w, "text")->twoway) {
            int nchars = 0;
            const TrField *f = tr_field_find(g->doc, tr_prop(w, "text")->bind);
            if (!f || tr_field_type(f->type, &nchars) != TR_FT_CHARS)
                return cg_err(g, "text binding must name a char array field");
        }
        for (p = 0; p < w->nprops; p++) {
            char junk[512];
            if (w->props[p].bind[0] == '\0') continue;
            if (w->props[p].twoway) {
                CgSlot slot;
                const char *k = w->props[p].key;
                if (strcmp(k, "text") == 0) continue;
                if (cg_int_slot(g, w, k, &slot) != 0) return -1;
            } else if (tr_rewrite_expr(g->doc, w->props[p].bind, junk, sizeof junk) != 0) {
                return cg_err(g, "a binding expression is not allowed");
            }
        }
    }
    return 0;
}

static int
name_reserved_local(const char *name)
{
    return !tr_is_ident(name) || strncmp(name, "tr_", 3) == 0;
}

static int
emit_guard(const char *class_name, char *dst, size_t cap)
{
    size_t i;
    int n = snprintf(dst, cap, "TCCRAD_%s_H", class_name);
    if (n < 0 || (size_t)n >= cap) return -1;
    for (i = 0; dst[i] != '\0'; i++) {
        if (dst[i] >= 'a' && dst[i] <= 'z') dst[i] = (char)(dst[i] - 'a' + 'A');
    }
    return 0;
}

static int
emit_privates(Gen *g)
{
    int i;
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *w = &g->doc->widgets[i];
        static const char *const keys[] = {
            "checked", "selected", "value", "ratio", "open", "r", "g", "b", "a", NULL
        };
        int k;
        for (k = 0; keys[k]; k++) {
            const TrProp *p = tr_prop(w, keys[k]);
            CgSlot slot;
            if (!p || p->twoway) continue;
            if (strcmp(keys[k], "open") == 0 && w->parent < 0) continue;
            if (cg_int_slot(g, w, keys[k], &slot) != 0) return -1;
            if (strncmp(slot.lv, "ui->tr_p_", 9) != 0) continue;
            if (cg_gi(g, 1, "int %s;\n", slot.lv + 4) != 0) return -1;
        }
        if (w->kind == TR_KIND_INPUT && !(tr_prop(w, "text") && tr_prop(w, "text")->twoway)) {
            int cap = tr_prop_int(w, "capacity", 64);
            if (cap < 1) cap = 1;
            if (cap > 1024) cap = 1024;
            if (cg_gi(g, 1, "char tr_p_%d_text[%d];\n", w->id, cap) != 0) return -1;
            if (cg_gi(g, 1, "int tr_p_%d_text_n;\n", w->id) != 0) return -1;
        }
    }
    return 0;
}

static int
emit_struct(Gen *g, TrBuf *h)
{
    int i;
    char guard[80];
    const char *cls = g->doc->class_name;
    TrBuf *saved = g->o;
    g->o = h;
    if (emit_guard(cls, guard, sizeof guard) != 0) return cg_err(g, "class name is too long");
    if (cg_gp(g, "/* Generated by tccrad. The fields region is yours. */\n") != 0) return -1;
    if (cg_gp(g, "#ifndef %s\n#define %s\n", guard, guard) != 0) return -1;
    if (cg_gp(g, "#include \"tccrad.h\"\n#include <stddef.h>\n\n") != 0) return -1;
    if (cg_gp(g, "struct nk_context;\n") != 0) return -1;
    if (cg_gp(g, "typedef struct %s %s;\n", cls, cls) != 0) return -1;
    if (cg_gp(g, "struct %s {\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "int tr_scale_milli;\n") != 0) return -1;
    if (cg_gi(g, 1, "int tr_config;\n") != 0) return -1;
    if (cg_gi(g, 1, "int tr_open;\n") != 0) return -1;
    for (i = 0; i < g->doc->nfields; i++) {
        const TrField *f = &g->doc->fields[i];
        int nchars = 0;
        TrFieldType t = tr_field_type(f->type, &nchars);
        if (t == TR_FT_CHARS) {
            if (cg_gi(g, 1, "char %s[%d];\n", f->name, nchars) != 0) return -1;
            if (cg_gi(g, 1, "int %s_len;\n", f->name) != 0) return -1;
        } else if (cg_gi(g, 1, "%s %s;\n", f->type, f->name) != 0) {
            return -1;
        }
    }
    if (emit_privates(g) != 0) return -1;
    if (cg_region(g, "fields", g->doc->user_fields, "") != 0) return -1;
    if (cg_gp(g, "};\n\n") != 0) return -1;
    if (cg_gp(g, "void %s_init(%s *ui);\n", cls, cls) != 0) return -1;
    if (cg_gp(g, "void %s_set_config(%s *ui, const char *name);\n", cls, cls) != 0) return -1;
    if (cg_gp(g, "void %s_set_scale(%s *ui, int scale_milli);\n", cls, cls) != 0) return -1;
    if (cg_gp(g, "const char *%s_os_title(const %s *ui);\n", cls, cls) != 0) return -1;
    if (cg_gp(g, "int %s_os_resizable(const %s *ui);\n", cls, cls) != 0) return -1;
    if (cg_gp(g, "int %s_frame(struct nk_context *ctx, %s *ui, float vw, float vh);\n", cls, cls) != 0)
        return -1;
    if (cg_gp(g, "int %s_build(%s *ui, void *parent);\n", cls, cls) != 0) return -1;
    if (cg_gp(g, "void %s_cell(const %s *ui, int row, int col, char *dst, size_t cap);\n", cls, cls) != 0)
        return -1;
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *w = &g->doc->widgets[i];
        int e;
        for (e = 0; e < w->nevents; e++) {
            char hn[TR_NAME_MAX];
            if (cg_handler(g->doc, w, &w->events[e], hn, sizeof hn) != 0) return -1;
            if (w->kind == TR_KIND_CUSTOM && strcmp(w->events[e].name, "draw") == 0) {
                if (cg_gp(g, "void %s(%s *ui, void *tr_backend);\n", hn, cls) != 0) return -1;
            } else if (cg_gp(g, "void %s(%s *ui);\n", hn, cls) != 0) {
                return -1;
            }
        }
    }
    if (cg_gp(g, "\n#endif\n") != 0) return -1;
    g->o = saved;
    return 0;
}

static int
active_index(const TrDoc *doc)
{
    int i;
    for (i = 0; i < doc->nconfigs; i++) {
        if (strcmp(doc->configs[i], doc->active_config) == 0) return i;
    }
    return 0;
}

static int
emit_init(Gen *g)
{
    const char *cls = g->doc->class_name;
    const TrWidget *root = &g->doc->widgets[0];
    int i;
    if (cg_gp(g, "void %s_init(%s *ui)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "if (!ui) return;\n") != 0) return -1;
    if (cg_gi(g, 1, "memset(ui, 0, sizeof *ui);\n") != 0) return -1;
    if (cg_gi(g, 1, "ui->tr_scale_milli = %d;\n", g->doc->scale_milli) != 0) return -1;
    if (cg_gi(g, 1, "ui->tr_config = %d;\n", active_index(g->doc)) != 0) return -1;
    if (cg_gi(g, 1, "ui->tr_open = %d;\n", tr_prop_int(root, "open", 1) ? 1 : 0) != 0) return -1;
    for (i = 0; i < g->doc->nfields; i++) {
        const TrField *f = &g->doc->fields[i];
        int nchars = 0;
        TrFieldType t = tr_field_type(f->type, &nchars);
        if (t == TR_FT_CHARS) {
            if (cg_gi(g, 1, "tr_copy(ui->%s, sizeof ui->%s, ", f->name, f->name) != 0) return -1;
            if (cg_cstr(g, f->init) != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
            if (cg_gi(g, 1, "ui->%s_len = %d;\n", f->name, (int)strlen(f->init)) != 0) return -1;
        } else if (t == TR_FT_FLOAT || t == TR_FT_DOUBLE) {
            const char *init = f->init[0] ? f->init : "0";
            if (cg_gi(g, 1, "ui->%s = %s;\n", f->name, init) != 0) return -1;
        } else {
            const char *init = f->init[0] ? f->init : "0";
            if (cg_gi(g, 1, "ui->%s = %s;\n", f->name, init) != 0) return -1;
        }
    }
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *w = &g->doc->widgets[i];
        static const char *const keys[] = {
            "checked", "selected", "value", "ratio", "open", "r", "g", "b", "a", NULL
        };
        int k;
        for (k = 0; keys[k]; k++) {
            CgSlot slot;
            const TrProp *p = tr_prop(w, keys[k]);
            if (!p || p->twoway) continue;
            if (strcmp(keys[k], "open") == 0 && w->parent < 0) continue;
            if (cg_int_slot(g, w, keys[k], &slot) != 0) return -1;
            if (strncmp(slot.lv, "ui->tr_p_", 9) != 0) continue;
            if (cg_gi(g, 1, "%s = %d;\n", slot.lv, tr_prop_int(w, keys[k], 0)) != 0) return -1;
        }
        if (w->kind == TR_KIND_INPUT && !(tr_prop(w, "text") && tr_prop(w, "text")->twoway)) {
            const TrProp *tx = tr_prop(w, "text");
            if (cg_gi(g, 1, "tr_copy(ui->tr_p_%d_text, sizeof ui->tr_p_%d_text, ", w->id, w->id) != 0)
                return -1;
            if (cg_cstr(g, tx ? tx->value : "") != 0) return -1;
            if (cg_gp(g, ");\n") != 0) return -1;
            if (cg_gi(g, 1, "ui->tr_p_%d_text_n = %d;\n", w->id, tx ? (int)strlen(tx->value) : 0) != 0)
                return -1;
        }
    }
    if (cg_gp(g, "}\n\n") != 0) return -1;
    if (cg_gp(g, "void %s_set_config(%s *ui, const char *name)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "int i;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (!ui || !name) return;\n") != 0) return -1;
    if (cg_gi(g, 1, "for (i = 0; i < %d; i++) {\n", g->doc->nconfigs) != 0) return -1;
    if (cg_gi(g, 2, "if (strcmp(tr_cfgs[i], name) == 0) ui->tr_config = i;\n") != 0) return -1;
    if (cg_gi(g, 1, "}\n}\n\n") != 0) return -1;
    if (cg_gp(g, "void %s_set_scale(%s *ui, int scale_milli)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "if (!ui) return;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (scale_milli < 100) scale_milli = 100;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (scale_milli > 8000) scale_milli = 8000;\n") != 0) return -1;
    if (cg_gi(g, 1, "ui->tr_scale_milli = scale_milli;\n}\n\n") != 0) return -1;
    if (cg_gp(g, "const char *%s_os_title(const %s *ui)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "if (!ui) return \"\";\n") != 0) return -1;
    if (cg_gi(g, 1, "(void)ui;\n") != 0) return -1;
    if (cg_gi(g, 1, "return ") != 0) return -1;
    if (cg_text(g, root, "title") != 0) return -1;
    if (cg_gp(g, ";\n}\n\n") != 0) return -1;
    if (cg_gp(g, "int %s_os_resizable(const %s *ui)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "(void)ui;\n") != 0) return -1;
    if (cg_gi(g, 1, "return %d;\n}\n\n", tr_prop_int(root, "resizable", 1) ? 1 : 0) != 0) return -1;
    return 0;
}

static int
emit_handlers(Gen *g)
{
    const char *cls = g->doc->class_name;
    int i;
    int e;
    for (i = 0; i < g->doc->nwidgets; i++) {
        const TrWidget *w = &g->doc->widgets[i];
        for (e = 0; e < w->nevents; e++) {
            char hn[TR_NAME_MAX];
            char tag[96];
            int n;
            if (cg_handler(g->doc, w, &w->events[e], hn, sizeof hn) != 0) return -1;
            n = snprintf(tag, sizeof tag, "event %d %s", w->id, w->events[e].name);
            if (n < 0 || (size_t)n >= sizeof tag) return -1;
            if (w->kind == TR_KIND_CUSTOM && strcmp(w->events[e].name, "draw") == 0) {
                if (cg_gp(g, "void %s(%s *ui, void *tr_backend)\n{\n", hn, cls) != 0) return -1;
                if (cg_region(g, tag, w->events[e].user, "    (void)ui;\n    (void)tr_backend;") != 0)
                    return -1;
            } else {
                if (cg_gp(g, "void %s(%s *ui)\n{\n", hn, cls) != 0) return -1;
                if (cg_region(g, tag, w->events[e].user, "    (void)ui;") != 0) return -1;
            }
            if (cg_gp(g, "}\n\n") != 0) return -1;
        }
    }
    if (cg_gp(g, "void %s_cell(const %s *ui, int row, int col, char *dst, size_t cap)\n{\n", cls, cls) != 0)
        return -1;
    if (cg_region(g, "cell", g->doc->user_cell,
            "    (void)ui;\n    (void)row;\n    (void)col;\n    if (dst && cap > 0) dst[0] = '\\0';") != 0)
        return -1;
    if (cg_gp(g, "}\n\n") != 0) return -1;
    if (cg_gp(g, "static void %s_user_pre(%s *ui)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "if (!ui) return;\n") != 0) return -1;
    if (cg_region(g, "draw-pre", g->doc->user_draw_pre, "    (void)ui;") != 0) return -1;
    if (cg_gp(g, "}\n\n") != 0) return -1;
    if (cg_gp(g, "static void %s_user_post(%s *ui)\n{\n", cls, cls) != 0) return -1;
    if (cg_gi(g, 1, "if (!ui) return;\n") != 0) return -1;
    if (cg_region(g, "draw-post", g->doc->user_draw_post, "    (void)ui;") != 0) return -1;
    return cg_gp(g, "}\n\n");
}

static int
emit_helpers(Gen *g)
{
    const char *cls = g->doc->class_name;
    int i;
    if (cg_gp(g, "#if defined(__GNUC__) || defined(__clang__)\n") != 0) return -1;
    if (cg_gp(g, "#define TR_MAYBE_UNUSED __attribute__((unused))\n#else\n") != 0) return -1;
    if (cg_gp(g, "#define TR_MAYBE_UNUSED\n#endif\n") != 0) return -1;
    if (cg_gp(g, "static const char *const tr_cfgs[] = {\n") != 0) return -1;
    for (i = 0; i < g->doc->nconfigs; i++) {
        if (cg_gp(g, "    ") != 0) return -1;
        if (cg_cstr(g, g->doc->configs[i]) != 0) return -1;
        if (cg_gp(g, ",\n") != 0) return -1;
    }
    if (cg_gp(g, "};\n\n") != 0) return -1;
    if (cg_gp(g, "static int tr_cfg_on(const %s *ui, const char *name) TR_MAYBE_UNUSED;\n", cls) != 0)
        return -1;
    if (cg_gp(g, "static int tr_cfg_on(const %s *ui, const char *name)\n{\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "if (!name || name[0] == '\\0') return 1;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (!ui || ui->tr_config < 0 || ui->tr_config >= %d) return 0;\n",
            g->doc->nconfigs) != 0)
        return -1;
    if (cg_gi(g, 1, "return strcmp(tr_cfgs[ui->tr_config], name) == 0;\n}\n\n") != 0) return -1;
    if (cg_gp(g, "static int tr_u(const %s *ui, int v) TR_MAYBE_UNUSED;\n", cls) != 0) return -1;
    if (cg_gp(g, "static int tr_u(const %s *ui, int v)\n{\n", cls) != 0) return -1;
    if (cg_gi(g, 1, "if (!ui) return v;\n") != 0) return -1;
    if (cg_gi(g, 1, "return tr_unit(v, ui->tr_scale_milli);\n}\n\n") != 0) return -1;
    if (cg_gp(g, "#if TCCRAD_BACKEND == TCCRAD_BACKEND_NUKLEAR\n") != 0) return -1;
    if (cg_gp(g, "static float tr_remain(struct nk_context *ctx, int gap) TR_MAYBE_UNUSED;\n") != 0)
        return -1;
    if (cg_gp(g, "static float tr_remain(struct nk_context *ctx, int gap)\n{\n") != 0) return -1;
    if (cg_gi(g, 1, "struct nk_rect r;\n") != 0) return -1;
    if (cg_gi(g, 1, "float h;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (!ctx || !ctx->current || !ctx->current->layout) return 28.0f;\n") != 0)
        return -1;
    if (cg_gi(g, 1, "r = nk_window_get_content_region(ctx);\n") != 0) return -1;
    if (cg_gi(g, 1, "h = (r.y + r.h) - ctx->current->layout->at_y - (float)gap;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (h < 1.0f) h = 1.0f;\n") != 0) return -1;
    if (cg_gi(g, 1, "return h;\n}\n") != 0) return -1;
    if (cg_gp(g, "static int tr_unmask(char *buf, int cap, const char *mask, int nlen, int oldlen, int cursor) TR_MAYBE_UNUSED;\n") != 0)
        return -1;
    if (cg_gp(g, "static int tr_unmask(char *buf, int cap, const char *mask, int nlen, int oldlen, int cursor)\n{\n") != 0)
        return -1;
    if (cg_gi(g, 1, "char next[4096];\n") != 0) return -1;
    if (cg_gi(g, 1, "int lead = 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "int tail = 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "int i;\n") != 0) return -1;
    if (cg_gi(g, 1, "int out = 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (!buf || !mask || cap < 2) return 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (nlen < 0) nlen = 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (oldlen < 0) oldlen = 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (nlen >= cap) nlen = cap - 1;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (oldlen >= cap) oldlen = cap - 1;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (cursor < 0) cursor = 0;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (cursor > nlen) cursor = nlen;\n") != 0) return -1;
    if (cg_gi(g, 1, "while (lead < nlen && mask[lead] == '*') lead++;\n") != 0) return -1;
    if (cg_gi(g, 1, "while (tail < nlen - lead && mask[nlen - 1 - tail] == '*') tail++;\n") != 0) return -1;
    if (cg_gi(g, 1, "if (lead + tail >= nlen) {\n") != 0) return -1;
    if (cg_gi(g, 2, "if (nlen < oldlen) {\n") != 0) return -1;
    if (cg_gi(g, 3, "int del = oldlen - nlen;\n") != 0) return -1;
    if (cg_gi(g, 3, "if (cursor > oldlen) cursor = oldlen;\n") != 0) return -1;
    if (cg_gi(g, 3, "if (cursor + del > oldlen) del = oldlen - cursor;\n") != 0) return -1;
    if (cg_gi(g, 3, "for (i = 0; i < cursor && i < oldlen && out + 1 < cap; i++) next[out++] = buf[i];\n") != 0)
        return -1;
    if (cg_gi(g, 3, "for (i = cursor + del; i < oldlen && out + 1 < cap; i++) next[out++] = buf[i];\n") != 0)
        return -1;
    if (cg_gi(g, 2, "} else if (nlen > oldlen) {\n") != 0) return -1;
    if (cg_gi(g, 3, "int add = nlen - oldlen;\n") != 0) return -1;
    if (cg_gi(g, 3, "int at = cursor - add;\n") != 0) return -1;
    if (cg_gi(g, 3, "if (at < 0) at = 0;\n") != 0) return -1;
    if (cg_gi(g, 3, "if (at > oldlen) at = oldlen;\n") != 0) return -1;
    if (cg_gi(g, 3, "for (i = 0; i < at && out + 1 < cap; i++) next[out++] = buf[i];\n") != 0) return -1;
    if (cg_gi(g, 3, "for (i = 0; i < add && out + 1 < cap; i++) next[out++] = '*';\n") != 0) return -1;
    if (cg_gi(g, 3, "for (i = at; i < oldlen && out + 1 < cap; i++) next[out++] = buf[i];\n") != 0) return -1;
    if (cg_gi(g, 2, "} else {\n") != 0) return -1;
    if (cg_gi(g, 3, "for (i = 0; i < oldlen && out + 1 < cap; i++) next[out++] = buf[i];\n") != 0) return -1;
    if (cg_gi(g, 2, "}\n") != 0) return -1;
    if (cg_gi(g, 1, "} else {\n") != 0) return -1;
    if (cg_gi(g, 2, "int mid_b = lead;\n") != 0) return -1;
    if (cg_gi(g, 2, "int mid_e = nlen - tail;\n") != 0) return -1;
    if (cg_gi(g, 2, "if (lead > oldlen) lead = oldlen;\n") != 0) return -1;
    if (cg_gi(g, 2, "if (tail > oldlen - lead) tail = oldlen - lead;\n") != 0) return -1;
    if (cg_gi(g, 2, "for (i = 0; i < lead && out + 1 < cap; i++) next[out++] = buf[i];\n") != 0) return -1;
    if (cg_gi(g, 2, "for (i = mid_b; i < mid_e && out + 1 < cap; i++) next[out++] = mask[i];\n") != 0) return -1;
    if (cg_gi(g, 2, "for (i = oldlen - tail; i < oldlen && out + 1 < cap; i++) next[out++] = buf[i];\n") != 0)
        return -1;
    if (cg_gi(g, 1, "}\n") != 0) return -1;
    if (cg_gi(g, 1, "for (i = 0; i < out; i++) buf[i] = next[i];\n") != 0) return -1;
    if (cg_gi(g, 1, "buf[out] = '\\0';\n") != 0) return -1;
    if (cg_gi(g, 1, "return out;\n}\n") != 0) return -1;
    return cg_gp(g, "#endif\n\n");
}

static int
emit_c_head(Gen *g, const char *include_name)
{
    TrBuf doc;
    int def = TCCRAD_BACKEND_NUKLEAR;
    if (g->doc->backend == TR_BACKEND_LIBUI) def = TCCRAD_BACKEND_LIBUI;
    if (g->doc->backend == TR_BACKEND_LVGL) def = TCCRAD_BACKEND_LVGL;
    if (tr_buf_init(&doc) != 0) return -1;
    if (tr_doc_serialize(g->doc, &doc) != 0) {
        tr_buf_free(&doc);
        return cg_err(g, "could not serialize the document");
    }
    if (cg_gp(g, "/* Generated by tccrad. Edit only inside @tccrad user regions. */\n") != 0) {
        tr_buf_free(&doc);
        return -1;
    }
    if (cg_gp(g, "/* @tccrad-doc\n%s@end tccrad-doc */\n\n", doc.data) != 0) {
        tr_buf_free(&doc);
        return -1;
    }
    tr_buf_free(&doc);
    if (cg_gp(g, "#include \"tccrad.h\"\n") != 0) return -1;
    if (cg_gp(g, "#include <string.h>\n") != 0) return -1;
    if (cg_gp(g, "#ifndef TCCRAD_BACKEND\n#define TCCRAD_BACKEND %d\n#endif\n", def) != 0) return -1;
    if (cg_gp(g, "#if TCCRAD_BACKEND == TCCRAD_BACKEND_NUKLEAR\n") != 0) return -1;
    if (cg_gp(g, "#define NK_INCLUDE_FIXED_TYPES\n#define NK_INCLUDE_STANDARD_IO\n") != 0) return -1;
    if (cg_gp(g, "#define NK_INCLUDE_STANDARD_VARARGS\n#define NK_INCLUDE_DEFAULT_ALLOCATOR\n") != 0)
        return -1;
    if (cg_gp(g, "#include \"nuklear.h\"\n") != 0) return -1;
    if (cg_gp(g, "#elif TCCRAD_BACKEND == TCCRAD_BACKEND_LIBUI\n#include <ui.h>\n") != 0) return -1;
    if (cg_gp(g, "#elif TCCRAD_BACKEND == TCCRAD_BACKEND_LVGL\n#include <lvgl.h>\n") != 0) return -1;
    if (cg_gp(g, "#else\n#error unknown TCCRAD_BACKEND\n#endif\n") != 0) return -1;
    if (cg_gp(g, "#include \"%s\"\n\n", include_name) != 0) return -1;
    return cg_region(g, "header", g->doc->user_header, "");
}

int
tr_codegen(const TrDoc *doc, const TrStyle *style, const char *include_name,
    TrBuf *hout, TrBuf *cout, char *err, size_t errcap)
{
    Gen g;
    TrStyle fallback;
    if (err && errcap) err[0] = '\0';
    if (!doc || !hout || !cout || !include_ok(include_name)) return -1;
    if (!style) {
        tr_style_defaults(&fallback, "default");
        style = &fallback;
    }
    memset(&g, 0, sizeof g);
    g.doc = doc;
    g.style = style;
    g.err = err;
    g.errcap = errcap;
    g.o = cout;
    tr_buf_clear(hout);
    tr_buf_clear(cout);
    if (cg_check(&g) != 0) return -1;
    if (emit_struct(&g, hout) != 0) return -1;
    if (emit_c_head(&g, include_name) != 0) return -1;
    if (emit_helpers(&g) != 0) return -1;
    if (emit_init(&g) != 0) return -1;
    if (emit_handlers(&g) != 0) return -1;
    if (cg_gp(&g, "#if TCCRAD_BACKEND == TCCRAD_BACKEND_NUKLEAR\n") != 0) return -1;
    if (cg_nk(&g) != 0) return -1;
    if (cg_gp(&g, "#elif TCCRAD_BACKEND == TCCRAD_BACKEND_LIBUI\n") != 0) return -1;
    if (cg_libui(&g) != 0) return -1;
    if (cg_gp(&g, "#elif TCCRAD_BACKEND == TCCRAD_BACKEND_LVGL\n") != 0) return -1;
    if (cg_lvgl(&g) != 0) return -1;
    if (cg_gp(&g, "#endif\n") != 0) return -1;
    return 0;
}

static int
emit_colors(Gen *g)
{
    int i;
    const TrStyle *st = g->style;
    if (cg_gi(g, 1, "static struct nk_color tr_colors[32];\n") != 0) return -1;
    if (cg_gi(g, 1, "_Static_assert(32 == (int)NK_COLOR_COUNT, \"style table\");\n") != 0) return -1;
    for (i = 0; i < TR_STYLE_COLORS; i++) {
        if (cg_gi(g, 1, "tr_colors[%d] = nk_rgba(%u, %u, %u, %u);\n", i,
                (unsigned)st->color[i].r, (unsigned)st->color[i].g,
                (unsigned)st->color[i].b, (unsigned)st->color[i].a) != 0)
            return -1;
    }
    return cg_gi(g, 1, "nk_style_from_table(ctx, tr_colors);\n");
}

int
tr_codegen_main(const TrDoc *doc, const TrStyle *style, const char *include_name,
    TrBuf *mout, char *err, size_t errcap)
{
    Gen g;
    TrStyle fallback;
    const char *cls;
    TrKind root_kind;
    int def = TCCRAD_BACKEND_NUKLEAR;
    if (err && errcap) err[0] = '\0';
    if (!doc || !mout || doc->nwidgets < 1 || !include_ok(include_name)) return -1;
    if (!style) {
        tr_style_defaults(&fallback, "default");
        style = &fallback;
    }
    if (doc->backend == TR_BACKEND_LIBUI) def = TCCRAD_BACKEND_LIBUI;
    if (doc->backend == TR_BACKEND_LVGL) def = TCCRAD_BACKEND_LVGL;
    cls = doc->class_name;
    root_kind = doc->widgets[0].kind;
    memset(&g, 0, sizeof g);
    g.doc = doc;
    g.style = style;
    g.o = mout;
    g.err = err;
    g.errcap = errcap;
    tr_buf_clear(mout);
    if (cg_gp(&g, "/* Generated host. Safe to replace after the first build. */\n") != 0) return -1;
    if (cg_gp(&g, "#include \"tccrad.h\"\n") != 0) return -1;
    if (cg_gp(&g, "#include <stdio.h>\n#include <string.h>\n") != 0) return -1;
    if (cg_gp(&g, "#ifndef TCCRAD_BACKEND\n#define TCCRAD_BACKEND %d\n#endif\n", def) != 0) return -1;
    if (cg_gp(&g, "#if TCCRAD_BACKEND == TCCRAD_BACKEND_NUKLEAR\n") != 0) return -1;
    if (cg_gp(&g, "#define NK_INCLUDE_FIXED_TYPES\n#define NK_INCLUDE_STANDARD_IO\n") != 0) return -1;
    if (cg_gp(&g, "#define NK_INCLUDE_STANDARD_VARARGS\n#define NK_INCLUDE_DEFAULT_ALLOCATOR\n") != 0)
        return -1;
    if (cg_gp(&g, "#define NK_IMPLEMENTATION\n#define NK_XLIB_IMPLEMENTATION\n") != 0) return -1;
    if (cg_gp(&g, "#include \"nuklear.h\"\n#include \"nuklear_xlib.h\"\n#include <X11/Xutil.h>\n") != 0)
        return -1;
    if (cg_gp(&g, "#include <time.h>\n") != 0) return -1;
    if (cg_gp(&g, "#include \"%s\"\n", include_name) != 0) return -1;
    if (cg_gp(&g, "int main(void)\n{\n") != 0) return -1;
    if (cg_gi(&g, 1, "Display *dpy; Window win; XFont *font; struct nk_context *ctx;\n") != 0) return -1;
    if (cg_gi(&g, 1, "int scr; int running = 1; %s ui;\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "unsigned ww = %d; unsigned hh = %d;\n",
            doc->widgets[0].w > 64 ? doc->widgets[0].w : 480,
            doc->widgets[0].h > 64 ? doc->widgets[0].h : 320) != 0)
        return -1;
    if (cg_gi(&g, 1, "%s_init(&ui);\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "dpy = XOpenDisplay(NULL);\n") != 0) return -1;
    if (cg_gi(&g, 1, "if (!dpy) { fprintf(stderr, \"No DISPLAY\\n\"); return 1; }\n") != 0) return -1;
    if (cg_gi(&g, 1, "scr = XDefaultScreen(dpy);\n") != 0) return -1;
    if (root_kind == TR_KIND_ACTIVITY) {
        if (cg_gi(&g, 1, "ww = (unsigned)DisplayWidth(dpy, scr);\n") != 0) return -1;
        if (cg_gi(&g, 1, "hh = (unsigned)DisplayHeight(dpy, scr);\n") != 0) return -1;
    }
    if (cg_gi(&g, 1, "win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 40, 40, ww, hh, 0, 0, 0);\n") != 0)
        return -1;
    if (cg_gi(&g, 1, "XStoreName(dpy, win, %s_os_title(&ui));\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "XSelectInput(dpy, win, ExposureMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|ButtonMotionMask|PointerMotionMask|StructureNotifyMask);\n") != 0)
        return -1;
    if (cg_gi(&g, 1, "XMapWindow(dpy, win);\n") != 0) return -1;
    if (cg_gi(&g, 1, "font = nk_xfont_create(dpy, \"fixed\");\n") != 0) return -1;
    if (cg_gi(&g, 1, "ctx = nk_xlib_init(font, dpy, scr, win, ww, hh);\n") != 0) return -1;
    if (cg_gi(&g, 1, "if (!ctx) return 1;\n") != 0) return -1;
    if (emit_colors(&g) != 0) return -1;
    if (cg_gi(&g, 1, "while (running) {\n") != 0) return -1;
    if (cg_gi(&g, 2, "XEvent ev; nk_input_begin(ctx);\n") != 0) return -1;
    if (cg_gi(&g, 2, "while (XPending(dpy)) {\n") != 0) return -1;
    if (cg_gi(&g, 3, "XNextEvent(dpy, &ev);\n") != 0) return -1;
    if (cg_gi(&g, 3, "if (ev.type == ClientMessage) { running = 0; break; }\n") != 0) return -1;
    if (cg_gi(&g, 3, "nk_xlib_handle_event(dpy, scr, win, &ev);\n") != 0) return -1;
    if (cg_gi(&g, 2, "}\n") != 0) return -1;
    if (cg_gi(&g, 2, "nk_input_end(ctx);\n") != 0) return -1;
    if (cg_gi(&g, 2, "if (!%s_frame(ctx, &ui, (float)ww, (float)hh)) running = 0;\n", cls) != 0)
        return -1;
    if (cg_gi(&g, 2, "XClearWindow(dpy, win);\n") != 0) return -1;
    if (cg_gi(&g, 2, "nk_xlib_render(win, nk_rgb(28, 28, 28));\n") != 0) return -1;
    if (cg_gi(&g, 2, "XFlush(dpy);\n") != 0) return -1;
    if (cg_gi(&g, 2, "{ struct timespec tr_sl; tr_sl.tv_sec = 0; tr_sl.tv_nsec = 16000000L; nanosleep(&tr_sl, NULL); }\n") != 0)
        return -1;
    if (cg_gi(&g, 1, "}\n") != 0) return -1;
    if (cg_gi(&g, 1, "nk_xfont_del(dpy, font);\n") != 0) return -1;
    if (cg_gi(&g, 1, "nk_xlib_shutdown();\n") != 0) return -1;
    if (cg_gi(&g, 1, "XDestroyWindow(dpy, win);\n") != 0) return -1;
    if (cg_gi(&g, 1, "XCloseDisplay(dpy);\n") != 0) return -1;
    if (cg_gi(&g, 1, "return 0;\n}\n") != 0) return -1;
    if (cg_gp(&g, "#elif TCCRAD_BACKEND == TCCRAD_BACKEND_LIBUI\n") != 0) return -1;
    if (cg_gp(&g, "#include <ui.h>\n#include \"%s\"\n", include_name) != 0) return -1;
    if (cg_gp(&g, "int main(void)\n{\n") != 0) return -1;
    if (cg_gi(&g, 1, "%s ui;\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "%s_init(&ui);\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "if (%s_build(&ui, NULL) != 0) return 1;\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "uiMain();\n") != 0) return -1;
    if (cg_gi(&g, 1, "uiUninit();\n") != 0) return -1;
    if (cg_gi(&g, 1, "return 0;\n}\n") != 0) return -1;
    if (cg_gp(&g, "#elif TCCRAD_BACKEND == TCCRAD_BACKEND_LVGL\n") != 0) return -1;
    if (cg_gp(&g, "#include <lvgl.h>\n#include \"%s\"\n", include_name) != 0) return -1;
    if (cg_gp(&g, "int main(void)\n{\n") != 0) return -1;
    if (cg_gi(&g, 1, "%s ui;\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "lv_display_t *disp;\n") != 0) return -1;
    if (cg_gi(&g, 1, "lv_obj_t *screen;\n") != 0) return -1;
    if (cg_gi(&g, 1, "int32_t ww = %d;\n",
            doc->widgets[0].w > 64 ? doc->widgets[0].w : 480) != 0)
        return -1;
    if (cg_gi(&g, 1, "int32_t hh = %d;\n",
            doc->widgets[0].h > 64 ? doc->widgets[0].h : 320) != 0)
        return -1;
    if (cg_gi(&g, 1, "%s_init(&ui);\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "lv_init();\n") != 0) return -1;
    if (cg_gi(&g, 1, "disp = lv_x11_window_create(%s_os_title(&ui), ww, hh);\n", cls) != 0)
        return -1;
    if (cg_gi(&g, 1, "if (!disp) { fprintf(stderr, \"No DISPLAY\\n\"); return 1; }\n") != 0)
        return -1;
    if (cg_gi(&g, 1, "lv_x11_inputs_create(disp, NULL);\n") != 0) return -1;
    if (cg_gi(&g, 1, "screen = lv_display_get_screen_active(disp);\n") != 0) return -1;
    if (cg_gi(&g, 1, "if (!screen) return 1;\n") != 0) return -1;
    if (cg_gi(&g, 1, "if (%s_build(&ui, screen) != 0) return 1;\n", cls) != 0) return -1;
    if (cg_gi(&g, 1, "for (;;) lv_timer_handler();\n") != 0) return -1;
    if (cg_gi(&g, 1, "return 0;\n}\n") != 0) return -1;
    if (cg_gp(&g, "#endif\n") != 0) return -1;
    return 0;
}
