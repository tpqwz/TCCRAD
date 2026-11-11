#include "ui.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_KIDS 48
#define UI_ITEMS 24
#define UI_TEXT 240
#define UI_ALL 256
#define UI_WINS 8

enum {
    UI_WIN = 1,
    UI_VBOX,
    UI_HBOX,
    UI_LABEL,
    UI_BUTTON,
    UI_CHECK,
    UI_ENTRY,
    UI_MLENTRY,
    UI_SLIDER,
    UI_COMBO,
    UI_RADIO,
    UI_PROGRESS,
    UI_SPIN,
    UI_SEP,
    UI_GROUP,
    UI_TAB,
    UI_MENU,
    UI_ITEM
};

struct uiControl {
    int kind;
    int x, y, w, h;
    int stretch;
    int shown;
    int password;
    int horiz;
    int padded;
    int has_menu;
    int borderless;
    int fullscreen;
    int value;
    int vmin;
    int vmax;
    char text[UI_TEXT];
    char items[UI_ITEMS][UI_TEXT];
    int nitems;
    Window win;
    int (*on_close)(uiWindow *, void *);
    void *close_data;
    void (*on_click)(void *, void *);
    void *click_data;
    void (*on_menu)(void *, void *, void *);
    void *menu_data;
    struct uiControl *child;
    struct uiControl *kids[UI_KIDS];
    int nkids;
};

static Display *g_dpy;
static int g_screen;
static int g_quit;
static XFontStruct *g_font;
static GC g_gc;
static Atom g_wm_delete;
static struct uiControl *g_all[UI_ALL];
static int g_nall;
static struct uiControl *g_wins[UI_WINS];
static int g_nwins;
static struct uiControl *g_menus[8];
static int g_nmenus;
static struct uiControl *g_focus;

static struct uiControl *
as_ctl(void *p)
{
    return (struct uiControl *)p;
}

static void
copy_cap(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    for (i = 0; i + 1 < cap && src[i] != '\0'; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static struct uiControl *
make(int kind)
{
    struct uiControl *c;
    if (g_nall >= UI_ALL) return NULL;
    c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->kind = kind;
    c->vmax = 100;
    g_all[g_nall++] = c;
    return c;
}

static int
add_kid(struct uiControl *p, struct uiControl *c, int stretch)
{
    if (!p || !c || p->nkids >= UI_KIDS) return -1;
    c->stretch = stretch > 0 ? stretch : 0;
    p->kids[p->nkids++] = c;
    return 0;
}

static int
pref_h(const struct uiControl *c)
{
    int i;
    int sum;
    int gap;
    if (!c) return 0;
    switch (c->kind) {
    case UI_LABEL: return 22;
    case UI_BUTTON: case UI_ENTRY: case UI_MLENTRY: case UI_COMBO:
    case UI_SPIN: case UI_SLIDER: return 28;
    case UI_CHECK: return 24;
    case UI_SEP: return 10;
    case UI_PROGRESS: return 18;
    case UI_RADIO: return (c->nitems > 0 ? c->nitems : 1) * 22;
    case UI_GROUP:
        return 26 + pref_h(c->child);
    case UI_TAB:
        sum = 0;
        for (i = 0; i < c->nkids; i++) {
            int ph = pref_h(c->kids[i]);
            if (ph > sum) sum = ph;
        }
        return 30 + sum;
    case UI_VBOX:
        sum = 0;
        gap = c->padded ? 6 : 0;
        for (i = 0; i < c->nkids; i++) sum += pref_h(c->kids[i]);
        if (c->nkids > 1) sum += gap * (c->nkids - 1);
        return sum > 0 ? sum : 8;
    case UI_HBOX:
        sum = 8;
        for (i = 0; i < c->nkids; i++) {
            int ph = pref_h(c->kids[i]);
            if (ph > sum) sum = ph;
        }
        return sum;
    default:
        return 22;
    }
}

static void layout(struct uiControl *c, int x, int y, int w, int h);

static void
layout_box(struct uiControl *b, int x, int y, int w, int h)
{
    int i;
    int fixed = 0;
    int stretch = 0;
    int extra;
    int pos;
    int gap = b->padded ? 6 : 0;
    int n = b->nkids;
    int along = b->horiz ? w : h;
    if (n < 1 || along < 1) return;
    for (i = 0; i < n; i++) {
        if (b->kids[i]->stretch > 0) stretch += b->kids[i]->stretch;
        else fixed += b->horiz ? 80 : pref_h(b->kids[i]);
    }
    if (n > 1) fixed += gap * (n - 1);
    extra = along - fixed;
    if (extra < 0) extra = 0;
    pos = b->horiz ? x : y;
    for (i = 0; i < n; i++) {
        struct uiControl *k = b->kids[i];
        int share = 0;
        int sz;
        if (stretch > 0 && k->stretch > 0)
            share = extra * k->stretch / stretch;
        sz = k->stretch > 0 ? share : (b->horiz ? 80 : pref_h(k));
        if (sz < 8) sz = 8;
        if (b->horiz) layout(k, pos, y, sz, h);
        else layout(k, x, pos, w, sz);
        pos += sz + gap;
    }
}

static void
layout(struct uiControl *c, int x, int y, int w, int h)
{
    if (!c) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    if (c->kind == UI_VBOX || c->kind == UI_HBOX) layout_box(c, x, y, w, h);
    else if (c->kind == UI_GROUP && c->child)
        layout(c->child, x + 8, y + 24, w - 16, h - 32);
    else if (c->kind == UI_TAB && c->nkids > 0) {
        int sel = c->value;
        if (sel < 0 || sel >= c->nkids) sel = 0;
        layout(c->kids[sel], x + 4, y + 30, w - 8, h - 34);
    } else if (c->kind == UI_WIN && c->child) {
        int top = c->has_menu && g_nmenus > 0 ? 28 : 4;
        layout(c->child, x + 8, y + top, w - 16, h - top - 8);
    }
}

static int
hit(const struct uiControl *c, int x, int y)
{
    return c && x >= c->x && y >= c->y && x < c->x + c->w && y < c->y + c->h;
}

static void
set_color(unsigned long pixel)
{
    XSetForeground(g_dpy, g_gc, pixel);
}

static unsigned long
rgb(unsigned r, unsigned g, unsigned b)
{
    XColor c;
    c.red = (unsigned short)(r * 257U);
    c.green = (unsigned short)(g * 257U);
    c.blue = (unsigned short)(b * 257U);
    c.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(g_dpy, DefaultColormap(g_dpy, g_screen), &c))
        return BlackPixel(g_dpy, g_screen);
    return c.pixel;
}

static void
text_in(Window win, int x, int y, const char *s)
{
    if (!s || s[0] == '\0' || !win) return;
    XDrawString(g_dpy, win, g_gc, x, y, s, (int)strlen(s));
}

static void draw_tree(struct uiControl *c, Window win);

static void
fill(Window win, int x, int y, int w, int h)
{
    if (w < 1 || h < 1) return;
    XFillRectangle(g_dpy, win, g_gc, x, y, (unsigned)w, (unsigned)h);
}

static void
frame(Window win, int x, int y, int w, int h)
{
    if (w < 1 || h < 1) return;
    XDrawRectangle(g_dpy, win, g_gc, x, y, (unsigned)(w - 1), (unsigned)(h - 1));
}

static void
draw_tree(struct uiControl *c, Window win)
{
    int i;
    if (!c || !win) return;
    switch (c->kind) {
    case UI_LABEL:
        set_color(rgb(230, 230, 230));
        text_in(win, c->x, c->y + 16, c->text);
        break;
    case UI_BUTTON:
        set_color(rgb(70, 70, 70));
        fill(win, c->x, c->y, c->w, c->h);
        set_color(rgb(180, 180, 180));
        frame(win, c->x, c->y, c->w, c->h);
        set_color(rgb(240, 240, 240));
        text_in(win, c->x + 8, c->y + 18, c->text);
        break;
    case UI_CHECK:
        set_color(rgb(220, 220, 220));
        frame(win, c->x, c->y + 4, 14, 14);
        if (c->value) text_in(win, c->x + 3, c->y + 16, "x");
        text_in(win, c->x + 20, c->y + 16, c->text);
        break;
    case UI_ENTRY:
    case UI_MLENTRY: {
        char shown[UI_TEXT];
        set_color(rgb(30, 30, 30));
        fill(win, c->x, c->y, c->w, c->h);
        set_color(rgb(160, 160, 160));
        frame(win, c->x, c->y, c->w, c->h);
        set_color(rgb(240, 240, 240));
        if (c->password) {
            size_t n = strlen(c->text);
            size_t k;
            if (n >= sizeof shown) n = sizeof shown - 1;
            for (k = 0; k < n; k++) shown[k] = '*';
            shown[n] = '\0';
            text_in(win, c->x + 6, c->y + 18, shown);
        } else {
            text_in(win, c->x + 6, c->y + 18, c->text);
        }
        break;
    }
    case UI_SLIDER:
    case UI_PROGRESS: {
        int span = c->vmax - c->vmin;
        int rel = c->value - c->vmin;
        int fw;
        if (span < 1) span = 1;
        if (rel < 0) rel = 0;
        if (rel > span) rel = span;
        fw = (c->w - 4) * rel / span;
        set_color(rgb(50, 50, 50));
        fill(win, c->x, c->y + 8, c->w, 10);
        set_color(rgb(80, 140, 220));
        fill(win, c->x + 2, c->y + 10, fw, 6);
        break;
    }
    case UI_COMBO: {
        const char *label = c->nitems > 0 && c->value >= 0 && c->value < c->nitems
            ? c->items[c->value] : "";
        set_color(rgb(40, 40, 40));
        fill(win, c->x, c->y, c->w, c->h);
        set_color(rgb(180, 180, 180));
        frame(win, c->x, c->y, c->w, c->h);
        set_color(rgb(230, 230, 230));
        text_in(win, c->x + 6, c->y + 18, label);
        break;
    }
    case UI_RADIO:
        set_color(rgb(230, 230, 230));
        for (i = 0; i < c->nitems; i++) {
            int yy = c->y + i * 22;
            text_in(win, c->x, yy + 14, i == c->value ? "(*)" : "( )");
            text_in(win, c->x + 28, yy + 14, c->items[i]);
        }
        break;
    case UI_SPIN: {
        char num[32];
        snprintf(num, sizeof num, "%d", c->value);
        set_color(rgb(40, 40, 40));
        fill(win, c->x, c->y, c->w, c->h);
        set_color(rgb(180, 180, 180));
        frame(win, c->x, c->y, c->w, c->h);
        set_color(rgb(230, 230, 230));
        text_in(win, c->x + 8, c->y + 18, num);
        break;
    }
    case UI_SEP:
        set_color(rgb(90, 90, 90));
        XDrawLine(g_dpy, win, g_gc, c->x, c->y + c->h / 2, c->x + c->w, c->y + c->h / 2);
        break;
    case UI_GROUP:
        set_color(rgb(140, 140, 140));
        frame(win, c->x, c->y + 10, c->w, c->h - 10);
        set_color(rgb(230, 230, 230));
        text_in(win, c->x + 8, c->y + 14, c->text);
        draw_tree(c->child, win);
        break;
    case UI_TAB:
        set_color(rgb(230, 230, 230));
        for (i = 0; i < c->nkids; i++) {
            int xx = c->x + i * 90;
            if (i == c->value) {
                set_color(rgb(80, 140, 220));
                fill(win, xx, c->y, 84, 22);
                set_color(rgb(20, 20, 20));
            }
            text_in(win, xx + 6, c->y + 16, c->kids[i]->text);
            set_color(rgb(230, 230, 230));
        }
        if (c->nkids > 0) {
            int sel = c->value;
            if (sel < 0 || sel >= c->nkids) sel = 0;
            draw_tree(c->kids[sel], win);
        }
        break;
    case UI_VBOX:
    case UI_HBOX:
        for (i = 0; i < c->nkids; i++) draw_tree(c->kids[i], win);
        break;
    case UI_WIN:
        draw_tree(c->child, win);
        break;
    default:
        break;
    }
}

static void
redraw(struct uiControl *win)
{
    int i;
    int x;
    if (!win || !win->win || !g_gc) return;
    set_color(rgb(32, 32, 32));
    fill(win->win, 0, 0, win->w, win->h);
    if (win->has_menu && g_nmenus > 0) {
        x = 8;
        set_color(rgb(50, 50, 50));
        fill(win->win, 0, 0, win->w, 26);
        set_color(rgb(230, 230, 230));
        for (i = 0; i < g_nmenus; i++) {
            struct uiControl *menu = g_menus[i];
            int k;
            text_in(win->win, x, 18, menu->text);
            x += 16 + (int)strlen(menu->text) * 7;
            for (k = 0; k < menu->nkids; k++) {
                text_in(win->win, x, 18, menu->kids[k]->text);
                menu->kids[k]->x = x;
                menu->kids[k]->y = 4;
                menu->kids[k]->w = 12 + (int)strlen(menu->kids[k]->text) * 7;
                menu->kids[k]->h = 20;
                x += menu->kids[k]->w + 8;
            }
        }
    }
    draw_tree(win->child, win->win);
}

static struct uiControl *
find_at(struct uiControl *c, int x, int y)
{
    int i;
    struct uiControl *deep = NULL;
    if (!hit(c, x, y) && c && c->kind != UI_WIN) return NULL;
    if (!c) return NULL;
    if (c->kind == UI_WIN) {
        deep = find_at(c->child, x, y);
        return deep ? deep : c;
    }
    if (c->kind == UI_GROUP) return find_at(c->child, x, y);
    if (c->kind == UI_VBOX || c->kind == UI_HBOX || c->kind == UI_TAB) {
        int n = c->kind == UI_TAB ? 1 : c->nkids;
        if (c->kind == UI_TAB) {
            if (y < c->y + 28) return c;
            if (c->nkids > 0) {
                int sel = c->value;
                if (sel < 0 || sel >= c->nkids) sel = 0;
                deep = find_at(c->kids[sel], x, y);
            }
            return deep ? deep : c;
        }
        for (i = 0; i < n; i++) {
            deep = find_at(c->kids[i], x, y);
            if (deep) return deep;
        }
        return NULL;
    }
    return c;
}

static void
click_ctl(struct uiControl *win, struct uiControl *c, int x, int y)
{
    if (!c) return;
    if (c->kind == UI_BUTTON && c->on_click) c->on_click(c, c->click_data);
    else if (c->kind == UI_CHECK) {
        c->value = c->value ? 0 : 1;
        if (c->on_click) c->on_click(c, c->click_data);
    } else if (c->kind == UI_ENTRY || c->kind == UI_MLENTRY) {
        g_focus = c;
    } else if (c->kind == UI_SLIDER) {
        int span = c->vmax - c->vmin;
        if (span < 1) span = 1;
        if (c->w < 2) c->w = 2;
        c->value = c->vmin + (x - c->x) * span / c->w;
        if (c->value < c->vmin) c->value = c->vmin;
        if (c->value > c->vmax) c->value = c->vmax;
        if (c->on_click) c->on_click(c, c->click_data);
    } else if (c->kind == UI_COMBO) {
        if (c->nitems > 0) c->value = (c->value + 1) % c->nitems;
        if (c->on_click) c->on_click(c, c->click_data);
    } else if (c->kind == UI_RADIO) {
        int idx = (y - c->y) / 22;
        if (idx < 0) idx = 0;
        if (idx >= c->nitems) idx = c->nitems - 1;
        if (idx >= 0) c->value = idx;
        if (c->on_click) c->on_click(c, c->click_data);
    } else if (c->kind == UI_SPIN) {
        if (x < c->x + c->w / 2) c->value--;
        else c->value++;
        if (c->value < c->vmin) c->value = c->vmin;
        if (c->value > c->vmax) c->value = c->vmax;
        if (c->on_click) c->on_click(c, c->click_data);
    } else if (c->kind == UI_TAB && y < c->y + 28) {
        int idx = (x - c->x) / 90;
        if (idx < 0) idx = 0;
        if (idx >= c->nkids) idx = c->nkids - 1;
        if (idx >= 0) c->value = idx;
        layout(win, 0, 0, win->w, win->h);
    }
}

static void
on_key(struct uiControl *c, XKeyEvent *ev)
{
    char buf[8];
    KeySym sym = 0;
    int n;
    size_t len;
    if (!c) return;
    n = XLookupString(ev, buf, (int)sizeof buf, &sym, NULL);
    len = strlen(c->text);
    if (sym == XK_BackSpace) {
        if (len > 0) c->text[len - 1] = '\0';
    } else if (n == 1 && buf[0] >= 32 && buf[0] < 127 && len + 1 < UI_TEXT) {
        if (c->kind == UI_MLENTRY && (sym == XK_Return || buf[0] == '\r')) buf[0] = '\n';
        c->text[len] = buf[0];
        c->text[len + 1] = '\0';
    }
    if (c->on_click) c->on_click(c, c->click_data);
}

static void
layout_win(struct uiControl *win)
{
    XWindowAttributes a;
    if (!win || !win->win) return;
    if (XGetWindowAttributes(g_dpy, win->win, &a)) {
        win->w = a.width;
        win->h = a.height;
    }
    layout(win, 0, 0, win->w, win->h);
}

static void
ensure_xwin(struct uiControl *c)
{
    int scr;
    int width;
    int height;
    XSetWindowAttributes at;
    if (!c || c->kind != UI_WIN || !g_dpy || c->win) return;
    scr = g_screen;
    width = c->w > 64 ? c->w : 480;
    height = c->h > 64 ? c->h : 320;
    if (c->fullscreen) {
        width = DisplayWidth(g_dpy, scr);
        height = DisplayHeight(g_dpy, scr);
    }
    memset(&at, 0, sizeof at);
    at.background_pixel = rgb(32, 32, 32);
    at.override_redirect = c->borderless ? True : False;
    at.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask;
    c->win = XCreateWindow(g_dpy, RootWindow(g_dpy, scr), 40, 40,
        (unsigned)width, (unsigned)height, 0, CopyFromParent, InputOutput, CopyFromParent,
        CWBackPixel | CWOverrideRedirect | CWEventMask, &at);
    if (!c->win) return;
    XStoreName(g_dpy, c->win, c->text[0] ? c->text : "libui");
    if (!g_wm_delete) g_wm_delete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, c->win, &g_wm_delete, 1);
    c->w = width;
    c->h = height;
}

const char *
uiInit(uiInitOptions *o)
{
    (void)o;
    if (g_dpy) return NULL;
    g_dpy = XOpenDisplay(NULL);
    if (!g_dpy) {
        char *err = malloc(12);
        if (!err) return NULL;
        memcpy(err, "No DISPLAY", 11);
        return err;
    }
    g_screen = DefaultScreen(g_dpy);
    g_font = XLoadQueryFont(g_dpy, "fixed");
    g_gc = XCreateGC(g_dpy, RootWindow(g_dpy, g_screen), 0, NULL);
    if (g_font) XSetFont(g_dpy, g_gc, g_font->fid);
    g_quit = 0;
    return NULL;
}

void
uiFreeInitError(const char *e)
{
    free((void *)e);
}

void
uiUninit(void)
{
    int i;
    for (i = 0; i < g_nall; i++) {
        free(g_all[i]);
        g_all[i] = NULL;
    }
    g_nall = 0;
    g_nwins = 0;
    g_nmenus = 0;
    g_focus = NULL;
    if (g_font) {
        XFreeFont(g_dpy, g_font);
        g_font = NULL;
    }
    if (g_gc) {
        XFreeGC(g_dpy, g_gc);
        g_gc = NULL;
    }
    if (g_dpy) {
        XCloseDisplay(g_dpy);
        g_dpy = NULL;
    }
}

void
uiQuit(void)
{
    g_quit = 1;
}

uiWindow *
uiNewWindow(const char *title, int width, int height, int has_menu)
{
    struct uiControl *c = make(UI_WIN);
    if (!c || g_nwins >= UI_WINS) return NULL;
    copy_cap(c->text, sizeof c->text, title);
    c->w = width;
    c->h = height;
    c->has_menu = has_menu ? 1 : 0;
    g_wins[g_nwins++] = c;
    return (uiWindow *)c;
}

void
uiWindowSetMargined(uiWindow *w, int margined)
{
    (void)w;
    (void)margined;
}

void
uiWindowOnClosing(uiWindow *w, int (*f)(uiWindow *, void *), void *data)
{
    struct uiControl *c = as_ctl(w);
    if (!c) return;
    c->on_close = f;
    c->close_data = data;
}

void
uiWindowSetChild(uiWindow *w, uiControl *child)
{
    struct uiControl *c = as_ctl(w);
    if (!c) return;
    c->child = as_ctl(child);
}

void
uiWindowSetBorderless(uiWindow *w, int borderless)
{
    struct uiControl *c = as_ctl(w);
    if (c) c->borderless = borderless ? 1 : 0;
}

void
uiWindowSetFullscreen(uiWindow *w, int fullscreen)
{
    struct uiControl *c = as_ctl(w);
    if (c) c->fullscreen = fullscreen ? 1 : 0;
}

void
uiControlShow(uiControl *ctl)
{
    struct uiControl *c = as_ctl(ctl);
    if (!c) return;
    c->shown = 1;
    if (c->kind == UI_WIN) {
        ensure_xwin(c);
        if (c->win) {
            XMapRaised(g_dpy, c->win);
            layout_win(c);
        }
    }
}

uiBox *
uiNewVerticalBox(void)
{
    struct uiControl *c = make(UI_VBOX);
    return (uiBox *)c;
}

uiBox *
uiNewHorizontalBox(void)
{
    struct uiControl *c = make(UI_HBOX);
    if (c) c->horiz = 1;
    return (uiBox *)c;
}

void
uiBoxSetPadded(uiBox *b, int padded)
{
    struct uiControl *c = as_ctl(b);
    if (c) c->padded = padded ? 1 : 0;
}

void
uiBoxAppend(uiBox *b, uiControl *child, int stretch)
{
    add_kid(as_ctl(b), as_ctl(child), stretch);
}

uiLabel *
uiNewLabel(const char *text)
{
    struct uiControl *c = make(UI_LABEL);
    if (c) copy_cap(c->text, sizeof c->text, text);
    return (uiLabel *)c;
}

uiButton *
uiNewButton(const char *text)
{
    struct uiControl *c = make(UI_BUTTON);
    if (c) copy_cap(c->text, sizeof c->text, text);
    return (uiButton *)c;
}

void
uiButtonOnClicked(uiButton *b, void (*f)(uiButton *, void *), void *data)
{
    struct uiControl *c = as_ctl(b);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

uiCheckbox *
uiNewCheckbox(const char *text)
{
    struct uiControl *c = make(UI_CHECK);
    if (c) copy_cap(c->text, sizeof c->text, text);
    return (uiCheckbox *)c;
}

void
uiCheckboxSetChecked(uiCheckbox *box, int checked)
{
    struct uiControl *c = as_ctl(box);
    if (c) c->value = checked ? 1 : 0;
}

int
uiCheckboxChecked(uiCheckbox *box)
{
    struct uiControl *c = as_ctl(box);
    return c && c->value ? 1 : 0;
}

void
uiCheckboxOnToggled(uiCheckbox *box, void (*f)(uiCheckbox *, void *), void *data)
{
    struct uiControl *c = as_ctl(box);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

static uiEntry *
new_entry(int password, int multi)
{
    struct uiControl *c = make(multi ? UI_MLENTRY : UI_ENTRY);
    if (c) c->password = password ? 1 : 0;
    return (uiEntry *)c;
}

uiEntry *
uiNewEntry(void)
{
    return new_entry(0, 0);
}

uiEntry *
uiNewPasswordEntry(void)
{
    return new_entry(1, 0);
}

void
uiEntrySetText(uiEntry *e, const char *text)
{
    struct uiControl *c = as_ctl(e);
    if (c) copy_cap(c->text, sizeof c->text, text);
}

char *
uiEntryText(uiEntry *e)
{
    struct uiControl *c = as_ctl(e);
    size_t n;
    char *p;
    if (!c) return NULL;
    n = strlen(c->text);
    p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, c->text, n + 1);
    return p;
}

void
uiEntryOnChanged(uiEntry *e, void (*f)(uiEntry *, void *), void *data)
{
    struct uiControl *c = as_ctl(e);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

uiMultilineEntry *
uiNewMultilineEntry(void)
{
    return (uiMultilineEntry *)new_entry(0, 1);
}

void
uiMultilineEntrySetText(uiMultilineEntry *e, const char *text)
{
    uiEntrySetText((uiEntry *)e, text);
}

char *
uiMultilineEntryText(uiMultilineEntry *e)
{
    return uiEntryText((uiEntry *)e);
}

void
uiMultilineEntryOnChanged(uiMultilineEntry *e,
    void (*f)(uiMultilineEntry *, void *), void *data)
{
    struct uiControl *c = as_ctl(e);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

void
uiFreeText(char *text)
{
    free(text);
}

uiSlider *
uiNewSlider(int min, int max)
{
    struct uiControl *c = make(UI_SLIDER);
    if (!c) return NULL;
    c->vmin = min;
    c->vmax = max;
    c->value = min;
    return (uiSlider *)c;
}

void
uiSliderSetValue(uiSlider *s, int value)
{
    struct uiControl *c = as_ctl(s);
    if (!c) return;
    if (value < c->vmin) value = c->vmin;
    if (value > c->vmax) value = c->vmax;
    c->value = value;
}

int
uiSliderValue(uiSlider *s)
{
    struct uiControl *c = as_ctl(s);
    return c ? c->value : 0;
}

void
uiSliderOnChanged(uiSlider *s, void (*f)(uiSlider *, void *), void *data)
{
    struct uiControl *c = as_ctl(s);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

static int
append_item(struct uiControl *c, const char *text)
{
    if (!c || c->nitems >= UI_ITEMS) return -1;
    copy_cap(c->items[c->nitems], UI_TEXT, text);
    c->nitems++;
    return 0;
}

uiCombobox *
uiNewCombobox(void)
{
    return (uiCombobox *)make(UI_COMBO);
}

void
uiComboboxAppend(uiCombobox *box, const char *text)
{
    append_item(as_ctl(box), text);
}

void
uiComboboxSetSelected(uiCombobox *box, int n)
{
    struct uiControl *c = as_ctl(box);
    if (!c) return;
    if (n < 0) n = 0;
    if (c->nitems > 0 && n >= c->nitems) n = c->nitems - 1;
    c->value = n;
}

int
uiComboboxSelected(uiCombobox *box)
{
    struct uiControl *c = as_ctl(box);
    return c ? c->value : 0;
}

void
uiComboboxOnSelected(uiCombobox *box, void (*f)(uiCombobox *, void *), void *data)
{
    struct uiControl *c = as_ctl(box);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

uiRadioButtons *
uiNewRadioButtons(void)
{
    return (uiRadioButtons *)make(UI_RADIO);
}

void
uiRadioButtonsAppend(uiRadioButtons *r, const char *text)
{
    append_item(as_ctl(r), text);
}

void
uiRadioButtonsSetSelected(uiRadioButtons *r, int n)
{
    uiComboboxSetSelected((uiCombobox *)r, n);
}

int
uiRadioButtonsSelected(uiRadioButtons *r)
{
    return uiComboboxSelected((uiCombobox *)r);
}

void
uiRadioButtonsOnSelected(uiRadioButtons *r, void (*f)(uiRadioButtons *, void *), void *data)
{
    struct uiControl *c = as_ctl(r);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

uiProgressBar *
uiNewProgressBar(void)
{
    struct uiControl *c = make(UI_PROGRESS);
    if (c) {
        c->vmin = 0;
        c->vmax = 100;
    }
    return (uiProgressBar *)c;
}

void
uiProgressBarSetValue(uiProgressBar *p, int n)
{
    struct uiControl *c = as_ctl(p);
    if (!c) return;
    if (n < 0) n = 0;
    if (n > 100) n = 100;
    c->value = n;
}

uiSpinbox *
uiNewSpinbox(int min, int max)
{
    struct uiControl *c = make(UI_SPIN);
    if (!c) return NULL;
    c->vmin = min;
    c->vmax = max;
    c->value = min;
    return (uiSpinbox *)c;
}

void
uiSpinboxSetValue(uiSpinbox *s, int value)
{
    uiSliderSetValue((uiSlider *)s, value);
}

int
uiSpinboxValue(uiSpinbox *s)
{
    return uiSliderValue((uiSlider *)s);
}

void
uiSpinboxOnChanged(uiSpinbox *s, void (*f)(uiSpinbox *, void *), void *data)
{
    struct uiControl *c = as_ctl(s);
    if (!c) return;
    c->on_click = (void (*)(void *, void *))f;
    c->click_data = data;
}

void *
uiNewHorizontalSeparator(void)
{
    return make(UI_SEP);
}

uiGroup *
uiNewGroup(const char *title)
{
    struct uiControl *c = make(UI_GROUP);
    if (c) copy_cap(c->text, sizeof c->text, title);
    return (uiGroup *)c;
}

void
uiGroupSetMargined(uiGroup *g, int margined)
{
    (void)g;
    (void)margined;
}

void
uiGroupSetChild(uiGroup *g, uiControl *child)
{
    struct uiControl *c = as_ctl(g);
    if (c) c->child = as_ctl(child);
}

uiTab *
uiNewTab(void)
{
    return (uiTab *)make(UI_TAB);
}

void
uiTabAppend(uiTab *t, const char *name, uiControl *page)
{
    struct uiControl *c = as_ctl(page);
    if (c) copy_cap(c->text, sizeof c->text, name);
    add_kid(as_ctl(t), c, 1);
}

uiMenu *
uiNewMenu(const char *name)
{
    struct uiControl *c = make(UI_MENU);
    if (!c || g_nmenus >= 8) return NULL;
    copy_cap(c->text, sizeof c->text, name);
    g_menus[g_nmenus++] = c;
    return (uiMenu *)c;
}

uiMenuItem *
uiMenuAppendItem(uiMenu *m, const char *name)
{
    struct uiControl *item = make(UI_ITEM);
    if (!item) return NULL;
    copy_cap(item->text, sizeof item->text, name);
    add_kid(as_ctl(m), item, 0);
    return (uiMenuItem *)item;
}

void
uiMenuItemOnClicked(uiMenuItem *item, void (*f)(uiMenuItem *, uiWindow *, void *), void *data)
{
    struct uiControl *c = as_ctl(item);
    if (!c) return;
    c->on_menu = (void (*)(void *, void *, void *))f;
    c->menu_data = data;
}

static struct uiControl *
win_of(Window w)
{
    int i;
    for (i = 0; i < g_nwins; i++) {
        if (g_wins[i] && g_wins[i]->win == w) return g_wins[i];
    }
    return g_nwins > 0 ? g_wins[0] : NULL;
}

void
uiMain(void)
{
    if (!g_dpy) return;
    while (!g_quit) {
        XEvent ev;
        struct uiControl *win;
        if (!XPending(g_dpy)) {
            int i;
            for (i = 0; i < g_nwins; i++) {
                if (g_wins[i] && g_wins[i]->shown) redraw(g_wins[i]);
            }
            XFlush(g_dpy);
        }
        XNextEvent(g_dpy, &ev);
        win = win_of(ev.xany.window);
        if (ev.type == Expose && win) redraw(win);
        else if (ev.type == ConfigureNotify && win) {
            layout_win(win);
            redraw(win);
        } else if (ev.type == ButtonPress && win) {
            struct uiControl *hitc;
            int i;
            int x = ev.xbutton.x;
            int y = ev.xbutton.y;
            if (win->has_menu && y < 26) {
                for (i = 0; i < g_nmenus; i++) {
                    int k;
                    for (k = 0; k < g_menus[i]->nkids; k++) {
                        struct uiControl *item = g_menus[i]->kids[k];
                        if (x >= item->x && x < item->x + item->w && item->on_menu)
                            item->on_menu(item, win, item->menu_data);
                    }
                }
            }
            hitc = find_at(win, x, y);
            click_ctl(win, hitc, x, y);
            redraw(win);
        } else if (ev.type == KeyPress && win) {
            on_key(g_focus, &ev.xkey);
            redraw(win);
        } else if (ev.type == ClientMessage && win &&
            (Atom)ev.xclient.data.l[0] == g_wm_delete) {
            int allow = 1;
            if (win->on_close) allow = win->on_close((uiWindow *)win, win->close_data);
            if (allow) {
                XDestroyWindow(g_dpy, win->win);
                win->win = 0;
                win->shown = 0;
            }
        }
    }
}
