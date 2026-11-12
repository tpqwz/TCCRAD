#include "model.h"
#include "codegen.h"
#include "style.h"
#include "ide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    test_roundtrip();
    test_codegen(dir);
    test_reject();
    test_ui_pct();
    test_nav();
    test_jump();
    test_scope();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
