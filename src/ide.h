#ifndef TCCRAD_IDE_H
#define TCCRAD_IDE_H

struct TrRope;

#include "model.h"
#include "project.h"
#include "report.h"
#include "style.h"

#include <stddef.h>
#include <time.h>

struct nk_context;

#define IDE_UNDO 24
#define IDE_BODY 8192
#define IDE_LOG 6000

enum {
    IDE_MODAL_NONE = 0,
    IDE_MODAL_OPEN,
    IDE_MODAL_SAVE,
    IDE_MODAL_CODE,
    IDE_MODAL_EXPLORE,
    IDE_MODAL_FIELDS,
    IDE_MODAL_CONFIG,
    IDE_MODAL_STYLE,
    IDE_MODAL_ABOUT
};

enum {
    IDE_SCHEME_DARK = 0,
    IDE_SCHEME_LIGHT = 1
};

typedef struct Ide {
    TrDoc *doc;
    TrStyle style;
    char home[TR_PATH_MAX];
    char path[TR_PATH_MAX];
    int dirty;
    time_t mtime;
    int selected;
    int modal;
    int view;
    int click_id;
    long long click_ms;
    char jump[TR_NAME_MAX];
    int show_all_cfg;
    int gesture;
    char status[192];
    char log[IDE_LOG];
    pid_t child;
    char *undo_s[IDE_UNDO];
    int undo_n;
    char *redo_s[IDE_UNDO];
    int redo_n;
    char *committed;
    char browse[TR_PATH_MAX];
    char pick[256];
    char names[96][128];
    int name_dir[96];
    int nnames;
    char body[IDE_BODY];
    int body_len;
    int body_id;
    int body_ev;
    char field_type[TR_TYPE_MAX];
    char field_name[TR_NAME_MAX];
    char field_init[TR_TEXT_MAX];
    char config_name[TR_NAME_MAX];
    int code_scroll;
    int code_caret;
    char code_ext[TR_PATH_MAX];
    struct TrRope *code_rope;
    size_t code_at;
    int code_live;
    int code_focus;
    int code_hold;
    int code_spaced;
    char *code_lens;
    size_t code_lens_n;
    unsigned code_lens_sig;
    char code_find[64];
    int explore_sel;
    char explore_filter[48];
    TrFinding findings[TR_FIND_MAX];
    int nfindings;
    int checked;
    int explore_hold;
    TrBuf preview;
    int ui_pct;
    int scheme;
} Ide;

Ide *ide_new(void);
void ide_free(Ide *ide);
void ide_set_status(Ide *ide, const char *msg);
void ide_select(Ide *ide, int id);
void ide_add(Ide *ide, TrKind kind);
void ide_delete(Ide *ide);
void ide_move(Ide *ide, int dir);
void ide_duplicate(Ide *ide);
void ide_undo(Ide *ide);
void ide_redo(Ide *ide);
void ide_after_frame(Ide *ide, int active);
int ide_save(Ide *ide);
int ide_open_path(Ide *ide, const char *path);
void ide_blank(Ide *ide);
void ide_build(Ide *ide, int run);
void ide_stop(Ide *ide);
void ide_poll_child(Ide *ide);
void ide_set_backend(Ide *ide, TrBackend backend);
void ide_note_disk(Ide *ide);
void ide_browse(Ide *ide, const char *dir);
int ide_visible(const Ide *ide, const TrWidget *w);
void ide_set_scene(Ide *ide, const char *name);
void ide_show_all_scenes(Ide *ide);
void ide_frame(struct nk_context *ctx, struct Ide *ide, int width, int height);
int ide_routine_name(const char *line, char *dst, size_t cap);
int ide_routine_index(const int *lines, int n, int at);
int ide_find_line(const char *text, const char *needle, int start);
int ide_code_jump_line(const char *text, const char *handler, int id, const char *ev);
int ide_def_line(const char *text, const char *name);
int ide_ensure_handler(Ide *ide, int id);
int ide_open_named_event(Ide *ide, int id, const char *evname);
int ide_user_writable(const char *text, size_t at, int del);
int ide_code_token(const char *s, int at, int spaced, int *next);
int ide_code_set_spaced(Ide *ide, int spaced, const char *text, size_t n);
int ide_code_lens(Ide *ide, const char *host, size_t n, const char **shown);
int ide_text_has(const char *hay, const char *needle);
int ide_ui_clamp(int pct);
int ide_ui_default_pct(int screen_h, int dpi);
void ide_ui_load(Ide *ide, int screen_h, int dpi);
int ide_scope_show(const char *line, int *depth);
int ide_scope_gutter(const char *line, int *depth, char *dst, size_t cap);
int ide_find_def(const char *home, const char *docpath, const char *name,
    char *path, size_t pathcap, int *lineno);
int ide_primary_event(const TrWidget *w);
int ide_handler_name(const TrDoc *doc, const TrWidget *w, char *dst, size_t cap);
void ide_open_code(Ide *ide, int id);
void ide_apply_code(Ide *ide);
void ide_ui_set(Ide *ide, int pct);
void ide_ui_bump(Ide *ide, int delta);
int ide_scheme_from_text(const char *text);
void ide_scheme_set(Ide *ide, int scheme);
void ide_scheme_bg(int scheme, unsigned char *r, unsigned char *g, unsigned char *b);

#endif
