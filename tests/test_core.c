#include "model.h"
#include "codegen.h"
#include "style.h"
#include "ide.h"
#include "rope.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

static void
expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        fails++;
    }
}

static void
test_buf(void)
{
    TrBuf b;
    expect(tr_buf_init(&b) == 0, "buf init");
    expect(tr_buf_append_cstr(&b, "ab") == 0, "append");
    expect(tr_buf_printf(&b, "-%d", 3) == 0, "printf");
    expect(strcmp(b.data, "ab-3") == 0, "buf contents");
    tr_buf_free(&b);
    expect(tr_is_ident("show_all") == 1, "ident");
    expect(tr_is_ident("row") == 0, "keyword");
    expect(tr_is_ident("a b") == 0, "space ident");
    expect(tr_expr_ok("show_all + 1") == 1, "expr");
    expect(tr_expr_ok("a*/b") == 0, "expr close");
}

static void
test_roundtrip(void)
{
    TrDoc *a = tr_doc_new();
    TrDoc *b = tr_doc_new();
    TrBuf s;
    char err[256];
    const TrWidget *chk;
    expect(a && b, "docs");
    tr_doc_blank(a);
    expect(tr_buf_init(&s) == 0, "ser buf");
    expect(tr_doc_serialize(a, &s) == 0, "serialize");
    err[0] = '\0';
    expect(tr_doc_parse(b, s.data, s.len, err, sizeof err) == 0, err[0] ? err : "parse");
    tr_buf_clear(&s);
    expect(tr_doc_serialize(b, &s) == 0, "serialize again");
    {
        TrBuf s2;
        tr_buf_init(&s2);
        tr_doc_serialize(a, &s2);
        expect(strcmp(s.data, s2.data) == 0, "stable serialize");
        tr_buf_free(&s2);
    }
    chk = tr_doc_widget_c(b, 4);
    expect(chk && chk->kind == TR_KIND_CHECKBOX, "checkbox survived");
    expect(chk && tr_prop(chk, "checked") && tr_prop(chk, "checked")->twoway, "two-way flag");
    tr_buf_free(&s);
    tr_doc_free(a);
    tr_doc_free(b);
}

static void
test_codegen(const char *dir)
{
    TrDoc *doc = tr_doc_new();
    TrStyle style;
    TrBuf h, c, m;
    char err[256];
    char path[512];
    int id;
    TrWidget *w;
    tr_doc_blank(doc);
    {
        int wi;
        for (wi = 0; wi < doc->nwidgets; wi++) {
            if (doc->widgets[wi].kind == TR_KIND_BUTTON && doc->widgets[wi].nevents > 0) {
                tr_copy_cap(doc->widgets[wi].events[0].handler,
                    sizeof doc->widgets[wi].events[0].handler, "OkClick");
                break;
            }
        }
    }
    w = tr_doc_widget(doc, 3);
    if (w) tr_prop_set(w, "password", "1");
    id = tr_doc_add_widget(doc, 1, TR_KIND_SLIDER, "volume");
    w = tr_doc_widget(doc, id);
    expect(w != NULL, "slider added");
    if (w) w->newline = 1;
    id = tr_doc_add_widget(doc, 1, TR_KIND_TABLE, "grid");
    expect(id > 0, "table added");
    tr_style_defaults(&style, "default");
    expect(tr_buf_init(&h) == 0 && tr_buf_init(&c) == 0 && tr_buf_init(&m) == 0, "bufs");
    err[0] = '\0';
    expect(tr_codegen(doc, &style, "app.h", &h, &c, err, sizeof err) == 0, err[0] ? err : "codegen");
    err[0] = '\0';
    expect(tr_codegen_main(doc, &style, "app.h", &m, err, sizeof err) == 0, err[0] ? err : "main");
    expect(strstr(c.data, "nk_button_label") != NULL, "nk button");
    expect(strstr(c.data, "ui->show_all") != NULL, "binding");
    expect(strstr(c.data, "uiNewButton") != NULL, "libui button");
    expect(strstr(c.data, "lv_button_create") != NULL, "lvgl button");
    expect(strstr(c.data, "/* @tccrad user event") != NULL, "user marker");
    expect(strstr(c.data, "tr_remain") != NULL, "negative-size helper present");
    expect(strstr(c.data, "tr_mask") != NULL, "password mask");
    expect(strstr(c.data, "tr_cols = 2") != NULL, "table columns");
    snprintf(path, sizeof path, "%s/app.h", dir);
    expect(tr_write_file(path, h.data, h.len) == 0, "write h");
    snprintf(path, sizeof path, "%s/app.c", dir);
    expect(tr_write_file(path, c.data, c.len) == 0, "write c");
    snprintf(path, sizeof path, "%s/main.c", dir);
    expect(tr_write_file(path, m.data, m.len) == 0, "write main");
    {
        char *text = NULL;
        size_t n = 0;
        TrDoc *loaded = tr_doc_new();
        snprintf(path, sizeof path, "%s/app.c", dir);
        expect(tr_read_file(path, &text, &n, TR_FILE_MAX) == 0, "reread");
        err[0] = '\0';
        expect(tr_doc_load_text(loaded, text, n, err, sizeof err) == 0, err[0] ? err : "load generated");
        expect(loaded->nwidgets == doc->nwidgets, "widget count");
        snprintf(path, sizeof path, "%s/app.h", dir);
        free(text);
        text = NULL;
        expect(tr_read_file(path, &text, &n, TR_FILE_MAX) == 0, "reread h");
        err[0] = '\0';
        expect(tr_doc_overlay_text(loaded, text, err, sizeof err) == 0, "overlay h");
        free(text);
        tr_doc_free(loaded);
    }
    tr_buf_free(&h);
    tr_buf_free(&c);
    tr_buf_free(&m);
    tr_doc_free(doc);
}

static void
test_nav(void)
{
    char name[48];
    int lines[3];
    const char *src = "/* head */\nvoid App_init(App *ui)\n{\n}\nstatic void OkClick(App *ui)\n{\n}\n";
    expect(ide_routine_name("void App_init(App *ui)", name, sizeof name) == 1, "routine init");
    expect(strcmp(name, "App_init") == 0, "routine init name");
    expect(ide_routine_name("static void Ok_click(App *ui)", name, sizeof name) == 1, "routine click");
    expect(strcmp(name, "Ok_click") == 0, "routine click name");
    expect(ide_routine_name("    if (n) {", name, sizeof name) == 0, "skip indented");
    expect(ide_routine_name("#include <stdio.h>", name, sizeof name) == 0, "skip include");
    expect(ide_routine_name("if (ready())", name, sizeof name) == 0, "skip if");
    lines[0] = 10;
    lines[1] = 40;
    lines[2] = 80;
    expect(ide_routine_index(lines, 3, 0) == -1, "declarations");
    expect(ide_routine_index(lines, 3, 10) == 0, "first routine");
    expect(ide_routine_index(lines, 3, 50) == 1, "middle routine");
    expect(ide_routine_index(lines, 3, 100) == 2, "last routine");
    expect(ide_find_line(src, "OkClick", 0) == 4, "find handler");
    expect(ide_find_line(src, "OkClick", 5) == -1, "find past it");
    expect(ide_find_line(src, "", 0) == -1, "empty find");
    expect(ide_text_has("button ok_btn", "OK") == 1, "filter folds");
    expect(ide_text_has("button ok_btn", "slider") == 0, "filter miss");
    expect(ide_text_has("form root", "") == 1, "empty filter");
    {
        char *host = NULL;
        size_t hn = 0;
        char why[160];
        const char *spaced = "if (n):\n    return 1\n";
        why[0] = '\0';
        if (tr_spaced_emit(spaced, strlen(spaced), &host, &hn, why, sizeof why) == 0) {
            expect(host && strstr(host, "{") != NULL, "spaced brace");
            expect(host && strstr(host, "return 1;") != NULL, "spaced semi");
            free(host);
        } else {
            expect(strstr(why, "offside") != NULL, "missing offside is named");
        }
    }
}

static void
test_rope(void)
{
    struct TrRope *r = tr_rope_new("abcd", 4);
    char *s;
    size_t n = 0;
    expect(tr_rope_len(r) == 4, "rope len");
    expect(tr_rope_insert(&r, 2, "XY", 2) == 0, "rope insert");
    s = tr_rope_flat(r, &n);
    expect(s && n == 6 && strcmp(s, "abXYcd") == 0, "rope flat");
    free(s);
    expect(tr_rope_delete(&r, 2, 2) == 0, "rope delete");
    s = tr_rope_flat(r, &n);
    expect(s && strcmp(s, "abcd") == 0, "rope restored");
    free(s);
    tr_rope_free(r);
}

static void
test_scope(void)
{
    int depth = 0;
    expect(ide_scope_show("void f(void)", &depth) == 0, "header depth");
    expect(ide_scope_show("{", &depth) == 0, "open stays on the brace line");
    expect(depth == 1, "open increases");
    expect(ide_scope_show("    return 1;", &depth) == 1, "body is inside");
    expect(ide_scope_show("}", &depth) == 0, "close steps out");
    expect(depth == 0, "close decreases");
    expect(ide_scope_show("char s[] = \"{\";", &depth) == 0, "string brace ignored");
    expect(depth == 0, "string does not nest");
    {
        char g[48];
        depth = 0;
        expect(ide_scope_gutter("void f(void) {", &depth, g, sizeof g) == 0, "gutter open");
        expect(depth == 1, "gutter depth");
        expect(strstr(g, "\xE2\x94\x8C") != NULL, "ida top");
        expect(ide_scope_gutter("    return;", &depth, g, sizeof g) == 0, "gutter mid");
        expect(strstr(g, "\xE2\x94\x82") != NULL, "ida bar");
        expect(ide_scope_gutter("}", &depth, g, sizeof g) == 0, "gutter close");
        expect(depth == 0, "gutter closed");
        expect(strstr(g, "\xE2\x94\x94") != NULL, "ida end");
    }
    {
        char dir[] = "/tmp/tccrad-def";
        char rt[128];
        char path[256];
        int ln = -1;
        FILE *fp;
        mkdir("/tmp/tccrad-def", 0700);
        mkdir("/tmp/tccrad-def/runtime", 0700);
        snprintf(rt, sizeof rt, "/tmp/tccrad-def/runtime/tccrad.h");
        fp = fopen(rt, "w");
        expect(fp != NULL, "def file");
        if (fp) {
            fputs("int foo(int n)\n{\n    return n;\n}\n", fp);
            fclose(fp);
        }
        expect(ide_find_def(dir, NULL, "foo", path, sizeof path, &ln) == 1, "found foo");
        expect(ln == 0, "foo line");
        expect(ide_find_def(dir, NULL, "missing_zz", path, sizeof path, &ln) == 0, "missing def");
    }
}

static void
test_jump(void)
{
    TrDoc *doc = tr_doc_new();
    const TrWidget *w = NULL;
    char name[64];
    int i;
    expect(doc != NULL, "jump doc");
    if (!doc) return;
    tr_doc_blank(doc);
    for (i = 0; i < doc->nwidgets; i++) {
        if (strcmp(doc->widgets[i].name, "ok_btn") == 0) w = &doc->widgets[i];
    }
    expect(w != NULL, "ok button");
    expect(w && ide_primary_event(w) >= 0, "click is the handler");
    expect(w && ide_handler_name(doc, w, name, sizeof name) == 0, "handler name");
    expect(w && strcmp(name, "App_ok_btn_click") == 0, "default handler");
    {
        int nd = 0;
        const char *const *ev = tr_event_defs(TR_KIND_LABEL, &nd);
        int lid;
        TrWidget *lab;
        expect(ev && nd >= 1 && strcmp(ev[0], "click") == 0, "label can click");
        lid = tr_doc_add_widget(doc, doc->widgets[0].id, TR_KIND_LABEL, "cap_lbl");
        lab = tr_doc_widget(doc, lid);
        expect(lab && ide_primary_event(lab) >= 0, "label has a click slot");
        expect(lab && ide_handler_name(doc, lab, name, sizeof name) == 0, "label handler");
        expect(lab && strcmp(name, "App_cap_lbl_click") == 0, "label click name");
    }
    {
        Ide *ide = ide_new();
        expect(ide != NULL, "ensure ide");
        if (ide) {
            tr_doc_free(ide->doc);
            ide->doc = doc;
            doc = NULL;
            expect(ide_ensure_handler(ide, w->id) == 0, "create click");
            expect(strcmp(w->events[ide_primary_event(w)].handler, "App_ok_btn_click") == 0,
                "handler stored");
            expect(tr_span_ptr(ide->doc, w->events[ide_primary_event(w)].user)[0] != '\0',
                "empty body created");
            ide_free(ide);
        }
    }
    {
        const char *src =
            "void on_run(Bench *ui);\n"
            "/* @tccrad w 25 */\n"
            "nk_label(ctx, \"Operator\", NK_TEXT_LEFT);\n"
            "void on_run(Bench *ui)\n"
            "{\n"
            "/* @tccrad user event 36 click */\n"
            "    ui->passes++;\n"
            "/* @end user */\n"
            "}\n";
        const char *body = strstr(src, "ui->passes");
        expect(ide_code_jump_line(src, "on_run", 36, "click") == 6, "jump user body");
        expect(ide_code_jump_line(src, "on_run", 0, NULL) == 5, "jump inside definition");
        expect(ide_code_jump_line(src, NULL, 25, NULL) == 1, "jump widget");
        expect(ide_def_line(src, "on_run") == 3, "definition not prototype");
        expect(body && ide_user_writable(src, (size_t)(body - src), 0) == 1, "body writable");
        expect(ide_user_writable(src, 0, 0) == 0, "prototype locked");
        expect(ide_user_writable("int x;\n", 0, 0) == 1, "plain file writable");
        expect(ide_user_writable(src, (size_t)(strstr(src, "/* @end") - src), 1) == 0,
            "marker not deleted");
    }
    tr_doc_free(doc);
}

static void
test_ui_pct(void)
{
    expect(ide_ui_clamp(0) == 100, "clamp low");
    expect(ide_ui_clamp(100) == 100, "clamp 100");
    expect(ide_ui_clamp(140) == 150, "snap 140");
    expect(ide_ui_clamp(150) == 150, "clamp 150");
    expect(ide_ui_clamp(400) == 300, "clamp high");
    expect(ide_ui_default_pct(1080, 96) == 100, "1080 at 96dpi");
    expect(ide_ui_default_pct(1440, 96) == 150, "1440 at 96dpi");
    expect(ide_ui_default_pct(800, 0) == 100, "short screen");
    expect(ide_ui_default_pct(1080, 192) == 200, "192 dpi");
    expect(ide_ui_default_pct(2160, 96) == 250, "2160");
    expect(ide_scheme_from_text("dark") == IDE_SCHEME_DARK, "scheme dark");
    expect(ide_scheme_from_text("light") == IDE_SCHEME_LIGHT, "scheme light");
    expect(ide_scheme_from_text("Light") < 0, "scheme case");
    expect(ide_scheme_from_text("dark\n") < 0, "scheme newline");
    expect(ide_scheme_from_text("") < 0, "scheme empty");
    expect(ide_scheme_from_text(NULL) < 0, "scheme null");
    {
        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;
        ide_scheme_bg(IDE_SCHEME_DARK, &r, &g, &b);
        expect(r == 18 && g == 18 && b == 18, "dark clear");
        ide_scheme_bg(IDE_SCHEME_LIGHT, &r, &g, &b);
        expect(r == 245 && g == 245 && b == 245, "light clear");
    }
}

static void
test_build_relative(void)
{
    TrDoc *doc = tr_doc_new();
    TrStyle style;
    char back[TR_PATH_MAX];
    char runtime[TR_PATH_MAX];
    char third[TR_PATH_MAX];
    char log[2048];
    int n;
    log[0] = '\0';
    expect(getcwd(back, sizeof back) != NULL, "cwd");
    expect(doc != NULL, "relative doc");
    if (!doc || back[0] == '\0') {
        tr_doc_free(doc);
        return;
    }
    tr_doc_blank(doc);
    tr_style_defaults(&style, "default");
    n = snprintf(runtime, sizeof runtime, "%s/runtime", back);
    expect(n > 0 && (size_t)n < sizeof runtime, "runtime path");
    n = snprintf(third, sizeof third, "%s/third_party", back);
    expect(n > 0 && (size_t)n < sizeof third, "third path");
    if (mkdir("/tmp/tccrad-relcwd", 0700) != 0 && errno != EEXIST) {
        expect(0, "mkdir relative out");
        tr_doc_free(doc);
        return;
    }
    unlink("/tmp/tccrad-relcwd/app");
    expect(chdir("/tmp") == 0, "chdir tmp");
    expect(tr_project_build(doc, &style, "tccrad-relcwd", runtime, third, "tcc",
            log, sizeof log, 0, NULL) == 0, "relative out builds");
    if (log[0] != '\0') fprintf(stderr, "relative build: %s\n", log);
    expect(access("/tmp/tccrad-relcwd/app", X_OK) == 0, "relative out binary");
    expect(chdir(back) == 0, "restore cwd");
    tr_doc_free(doc);
}

static void
test_report(void)
{
    const char *src =
        "void foo(void)\n"
        "{\n"
        "    if (x) gets(b);\n"
        "}\n"
        "void bar(void)\n"
        "{\n"
        "    foo();\n"
        "    foo();\n"
        "    printf(\"ok\");\n"
        "}\n";
    const char *nest =
        "void nest(void)\n"
        "{\n"
        "    if (a) {\n"
        "        while (b) {\n"
        "        }\n"
        "    }\n"
        "}\n";
    TrExplore ex[4];
    TrExplore one[1];
    TrFinding f[8];
    int n;
    int m;
    int gets_line = -1;
    int i;
    n = tr_explore(src, ex, 4);
    expect(n == 2, "two routines");
    expect(n == 2 && ex[0].complexity >= 2, "foo decides");
    expect(n == 2 && ex[0].lines == 4, "foo lines");
    expect(n == 2 && ex[0].depth == 1, "foo depth");
    expect(n == 2 && ex[1].fan_out == 1, "bar calls out");
    expect(n == 2 && ex[0].fan_in == 1, "foo is called");
    expect(n == 2 && ex[0].ncallers == 1 && strcmp(ex[ex[0].callers[0]].name, "bar") == 0,
        "caller is bar");
    expect(n == 2 && ex[0].caller_line[0] == 6, "call site line");
    expect(n == 2 && ex[1].ncallees == 1 && ex[1].callee_line[0] == 6, "callee line");
    expect(tr_explore(src, one, 1) == 1, "cap one routine");
    n = tr_explore(nest, ex, 4);
    expect(n == 1 && ex[0].depth == 3, "nest depth");
    expect(n == 1 && ex[0].complexity >= 3, "nest decides");
    expect(n == 1 && ex[0].ndecide >= 2 && strcmp(ex[0].decide[0], "if") == 0, "first decision");
    expect(n == 1 && strcmp(ex[0].decide[1], "while") == 0 && ex[0].decide_line[1] == 3,
        "while decision");
    expect(n == 1 && ex[0].decide_depth[0] == 0 && ex[0].decide_depth[1] == 1,
        "decision nesting");
    {
        char many[320];
        int k;
        TrFinding hit;
        char row[96];
        snprintf(many, sizeof many, "void many(void)\n{\n    ");
        for (k = 0; k < 17; k++) snprintf(many + strlen(many), sizeof many - strlen(many), "if(a)");
        snprintf(many + strlen(many), sizeof many - strlen(many), ";\n}\n");
        n = tr_explore(many, ex, 4);
        expect(n == 1 && ex[0].ndecide == 16 && ex[0].ndecide_all == 17, "decision cap");
        memset(&hit, 0, sizeof hit);
        hit.line = 1;
        hit.severity = 3;
        memcpy(hit.checker, "security.gets", 14);
        expect(tr_find_row(&hit, "void f(void)\n    gets(b);\n", row, sizeof row) == 0 &&
            strcmp(row, "find  high  security.gets  2  gets(b);") == 0, "find row");
    }
    {
        const char *hands =
            "void Bench_build(void)\n"
            "{\n"
            "    if (a) return;\n"
            "}\n"
            "void on_run(void)\n"
            "{\n"
            "    /* @tccrad user event 1 click */\n"
            "}\n";
        n = tr_explore(hands, ex, 4);
        expect(n == 2 && ex[0].user == 0, "build is generated");
        expect(n == 2 && ex[1].user == 1, "on_run is a handler");
    }
    m = tr_check(src, f, 8);
    for (i = 0; i < m; i++) {
        if (strcmp(f[i].checker, "security.gets") == 0) gets_line = f[i].line;
    }
    expect(gets_line == 2, "gets line");
    expect(tr_check("void f(void)\n{\n    printf(\"ok\");\n}\n", f, 8) == 0, "literal format");
    expect(tr_check("void f(void)\n{\n    printf(buf);\n}\n", f, 8) == 1, "open format");
    expect(tr_check("void f(void)\n{\n    fprintf(stderr, \"ok\");\n}\n", f, 8) == 0,
        "fprintf literal");
    expect(tr_check("void f(void)\n{\n    snprintf(d, n, \"ok\");\n}\n", f, 8) == 0,
        "snprintf literal");
    expect(tr_check("void f(void)\n{\n    snprintf(d, n, fmt);\n}\n", f, 8) == 1,
        "snprintf format");
    expect(tr_check("void f(void)\n{\n    snprintf(d, n,\n        \"ok\");\n}\n", f, 8) == 0,
        "snprintf split literal");
    expect(tr_check("void f(void)\n{\n    scanf(\"%s\", b);\n}\n", f, 8) == 1, "scanf s");
    expect(tr_check("void f(void)\n{\n    scanf(\"%12s\", b);\n}\n", f, 8) == 0, "scanf width");
    expect(tr_check("void f(void)\n{\n    // gets(b);\n}\n", f, 8) == 0, "comment gets");
    expect(tr_check("    // gets(b);\n", f, 8) == 0, "indented comment");
    expect(tr_check("void f(void)\n{\n    strcpy(a, b); printf(buf);\n}\n", f, 8) == 2,
        "two findings");
    expect(tr_check("void f(void)\n{\n    gets(b); /* @tccrad ignore security.gets */\n}\n",
        f, 8) == 0, "ignore gets");
    expect(tr_check("void f(void)\n{\n    // @tccrad ignore\n    gets(b);\n}\n", f, 8) == 0,
        "ignore next line");
    expect(tr_check("void f(void)\n{\n    gets(b); /* @tccrad ignore security.strcpy */\n}\n",
        f, 8) == 1, "ignore other checker");
    expect(tr_check("void f(void)\n{\n    gets(b); /* @tccrad ignore */\n    strcpy(a, b);\n}\n",
        f, 8) == 1, "ignore stays on its line");
    {
        const char *both =
            "void shared(void)\n"
            "{\n"
            "}\n"
            "#if defined(__GNUC__) || defined(__clang__)\n"
            "#define TR_MAYBE_UNUSED\n"
            "#else\n"
            "#define TR_MAYBE_UNUSED\n"
            "#endif\n"
            "#if TCCRAD_BACKEND == TCCRAD_BACKEND_NUKLEAR\n"
            "void nk_only(void)\n"
            "{\n"
            "}\n"
            "#elif TCCRAD_BACKEND == TCCRAD_BACKEND_LIBUI\n"
            "void ui_only(void)\n"
            "{\n"
            "    gets(b);\n"
            "}\n"
            "#endif\n";
        char *nk = tr_backend_view(both, 1);
        char *ui = tr_backend_view(both, 2);
        char line[32];
        expect(nk != NULL && ui != NULL, "backend view");
        if (nk && ui) {
            n = tr_explore(nk, ex, 4);
            expect(n == 2 && strcmp(ex[1].name, "nk_only") == 0, "nuklear routines");
            n = tr_explore(ui, ex, 4);
            expect(n == 2 && strcmp(ex[1].name, "ui_only") == 0, "libui routines");
            expect(n == 2 && ex[1].line == 13, "libui line");
            expect(tr_check(nk, f, 8) == 0, "nuklear has no gets");
            expect(tr_check(ui, f, 8) == 1 && f[0].line == 15, "libui gets line");
            expect(tr_line_text(ui, f[0].line, line, sizeof line) == 0 &&
                strcmp(line, "gets(b);") == 0, "finding source");
        }
        free(nk);
        free(ui);
    }
}

static void
test_spaced_view(void)
{
    const char *host =
        "int main(void)\n"
        "{\n"
        "    if (x)\n"
        "        return 1;\n"
        "    return 0;\n"
        "}\n";
    char *spaced = NULL;
    char *back = NULL;
    size_t sn = 0;
    size_t bn = 0;
    char err[160];
    int next = 0;
    err[0] = '\0';
    expect(tr_spaced_space(host, strlen(host), &spaced, &sn, err, sizeof err) == 0, "space host");
    expect(spaced != NULL && strchr(spaced, ':') != NULL, "space has a colon");
    expect(spaced && strstr(spaced, "return") != NULL, "space keeps return");
    if (spaced)
        expect(tr_spaced_emit(spaced, strlen(spaced), &back, &bn, err, sizeof err) == 0,
            "emit spaced");
    expect(back != NULL && strchr(back, '{') != NULL, "emit has a brace");
    expect(back && strstr(back, "main") != NULL, "emit keeps main");
    expect(ide_code_token("if (x):", 0, 1, &next) == 4, "if is a keyword");
    expect(ide_code_token("if (x):", 6, 1, &next) == 5 && next == 7, "spaced colon");
    expect(ide_code_token("if (x):", 6, 0, &next) == 0, "host colon stays plain");
    expect(ide_code_token("/* note */ x", 0, 1, &next) == 1 && next == 10, "block comment");
    expect(ide_code_token("#include <stdio.h>", 0, 0, &next) == 6, "directive");
    expect(ide_code_token("\"hi\"", 0, 1, &next) == 2, "string in spaced");
    free(spaced);
    free(back);
}

static void
test_scene(void)
{
    Ide *ide = ide_new();
    TrWidget *w;
    int id;
    expect(ide != NULL, "scene ide");
    if (!ide) return;
    tr_copy_cap(ide->doc->configs[0], TR_NAME_MAX, "default");
    tr_copy_cap(ide->doc->configs[1], TR_NAME_MAX, "night");
    ide->doc->nconfigs = 2;
    tr_copy_cap(ide->doc->active_config, sizeof ide->doc->active_config, "default");
    id = tr_doc_add_widget(ide->doc, ide->doc->widgets[0].id, TR_KIND_LABEL, "night_lbl");
    w = tr_doc_widget(ide->doc, id);
    expect(w != NULL, "scene widget");
    if (w) tr_copy_cap(w->config, sizeof w->config, "night");
    expect(w && ide_visible(ide, w) == 0, "night hidden on default");
    ide_set_scene(ide, "night");
    expect(strcmp(ide->doc->active_config, "night") == 0, "scene night");
    expect(ide->show_all_cfg == 0, "one scene");
    expect(w && ide_visible(ide, w) == 1, "night shown");
    ide_show_all_scenes(ide);
    expect(ide->show_all_cfg == 1 && w && ide_visible(ide, w) == 1, "all shows night");
    ide_set_scene(ide, "missing");
    expect(strcmp(ide->doc->active_config, "night") == 0, "unknown scene stays");
    ide_free(ide);
}

static void
test_spaced_switch(void)
{
    Ide *ide = ide_new();
    char *buf;
    expect(ide != NULL, "spaced ide");
    if (!ide) return;
    buf = malloc(8);
    expect(buf != NULL, "spaced buf");
    if (!buf) {
        ide_free(ide);
        return;
    }
    memcpy(buf, "int x;\n", 8);
    ide->code_spaced = 1;
    ide->code_lens = buf;
    ide->code_lens_n = 7;
    ide->code_tie = buf;
    ide->code_tied = 1;
    expect(ide_code_set_spaced(ide, 0, "int x;\n", 7) == 0, "leave spaced");
    expect(ide->code_lens == NULL, "lens dropped");
    expect(ide->code_tie == NULL, "tie dropped");
    expect(ide->code_tied == 0, "untied");
    expect(ide->code_spaced == 0, "back to C");
    ide_free(ide);
}

static void
test_ident(void)
{
    const char *s = "int main(void)\n{\n  main();\n  other(main);\n}\n";
    char name[48];
    int lines[8];
    int n;
    size_t len = strlen(s);
    expect(ide_ident_at(s, len, 4, name, sizeof name) == 1, "ident at main");
    expect(strcmp(name, "main") == 0, "ident is main");
    expect(ide_ident_at(s, len, 0, name, sizeof name) == 1, "ident at int");
    expect(strcmp(name, "int") == 0, "ident is int");
    n = ide_ident_uses(s, len, "main", lines, 8);
    expect(n == 3, "three uses of main");
    expect(lines[0] == 0 && lines[1] == 2 && lines[2] == 3, "use lines");
    expect(ide_ident_uses(s, len, "mai", lines, 8) == 0, "no prefix match");
}

static void
test_event_catalog(void)
{
    int n = 0;
    int i;
    int saw_key = 0;
    const char *const *e = tr_event_defs(TR_KIND_BUTTON, &n);
    expect(n >= 11 && e && strcmp(e[0], "click") == 0, "button events");
    for (i = 0; e && i < n; i++) {
        if (strcmp(e[i], "keydown") == 0) saw_key = 1;
    }
    expect(saw_key == 1, "button OnKeyDown");
    e = tr_event_defs(TR_KIND_CHECKBOX, &n);
    expect(n >= 12 && e && strcmp(e[0], "click") == 0, "checkbox events");
    expect(e && strcmp(e[n - 1], "change") == 0, "checkbox OnChange");
    e = tr_event_defs(TR_KIND_WINDOW, &n);
    expect(n >= 14 && e && strcmp(e[0], "appear") == 0 && strcmp(e[1], "close") == 0,
        "form events");
    e = tr_event_defs(TR_KIND_INPUT, &n);
    expect(n >= 12 && e && strcmp(e[0], "change") == 0, "input OnChange");
    e = tr_event_defs(TR_KIND_TABLE, &n);
    expect(n >= 12 && e && strcmp(e[0], "select") == 0, "table events");
    e = tr_event_defs(TR_KIND_CUSTOM, &n);
    expect(n >= 12 && e && strcmp(e[0], "draw") == 0, "custom OnPaint");
}

static void
test_span_write(void)
{
    const char *t = "/* @tccrad user event 1 click */\nhello;\n/* @end user */\n";
    const char *nl = strchr(t, '\n');
    const char *end = strstr(t, "/* @end user */");
    char buf[8];
    char g[32];
    int depth = 0;
    size_t begin;
    expect(nl != NULL && end != NULL && end > nl, "span markers");
    if (!nl || !end || end < nl) return;
    begin = (size_t)(nl + 1 - t);
    expect(ide_span_writable(t, begin, begin) == 1, "insert in handler");
    expect(ide_span_writable(t, begin, (size_t)(end - t)) == 1, "body is writable");
    expect(ide_span_writable(t, 0, 1) == 0, "marker line stays locked");
    expect(ide_span_writable(t, begin, (size_t)(end - t) + 1) == 0, "span stops at the marker");
    expect(ide_span_writable("int x;\n", 0, 6) == 1, "unmarked file is writable");
    expect(ide_span_writable(t, 4, 2) == 0, "inverted span rejected");
    buf[0] = '{';
    buf[1] = '\n';
    buf[2] = '}';
    buf[3] = '\0';
    expect(ide_scope_gutter_n(buf, 1, &depth, g, sizeof g) == 0, "gutter span");
    expect(depth == 1, "gutter span does not read the next line");
}

static void
test_span_stable(void)
{
    TrDoc *doc = tr_doc_new();
    size_t before;
    int i;
    const char *got;
    expect(doc != NULL, "span doc");
    if (!doc) return;
    expect(tr_span_set(doc, &doc->user_header, "abc", 3) == 0, "span header");
    expect(tr_span_set(doc, &doc->user_fields, "xyz", 3) == 0, "span fields");
    before = doc->arena_len;
    for (i = 0; i < 1000; i++) {
        if (tr_span_set(doc, &doc->user_header, "abc", 3) != 0) break;
    }
    expect(i == 1000, "span repeat");
    expect(doc->arena_len == before, "span set does not grow the arena");
    expect(tr_span_set(doc, &doc->user_header, "abcd", 4) == 0, "span change");
    got = tr_span_ptr(doc, doc->user_header);
    expect(got && strcmp(got, "abcd") == 0, "span bytes");
    expect(doc->user_header.len == 4, "span len");
    tr_doc_free(doc);
}

static void
test_reject(void)
{
    TrDoc *doc = tr_doc_new();
    char err[128];
    expect(tr_field_add(doc, "int", "row", "0") < 0, "reject keyword field");
    expect(tr_field_add(doc, "char[3]", "nm", "abcd") < 0, "reject long init");
    expect(tr_style_name_ok("../x") == 0, "reject style traversal");
    expect(tr_style_name_ok("night") == 1, "style name");
    err[0] = '\0';
    expect(tr_doc_parse(doc, "version 1\n", 10, err, sizeof err) != 0, "short doc fails");
    tr_doc_free(doc);
}

int
main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/tccrad-test";
    test_buf();
    test_span_stable();
    test_spaced_switch();
    test_ident();
    test_event_catalog();
    test_span_write();
    test_roundtrip();
    test_codegen(dir);
    test_reject();
    test_ui_pct();
    test_nav();
    test_jump();
    test_scope();
    test_rope();
    test_report();
    test_scene();
    test_spaced_view();
    test_build_relative();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
