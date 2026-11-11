#include "model.h"
#include "codegen.h"
#include "style.h"

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
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
