#define _POSIX_C_SOURCE 200809L
#define NK_IMPLEMENTATION
#define NK_XLIB_IMPLEMENTATION
#define NK_XLIB_USE_XFT
#include "nk_glue.h"
#include "nuklear_xlib.h"

#include "ide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

static int
shortcut(Ide *ide, XEvent *ev)
{
    KeySym sym;
    if (ev->type != KeyPress) return 0;
    sym = XLookupKeysym(&ev->xkey, 0);
    if (sym == XK_F4) {
        ide->view = 2;
        if (ide->modal == IDE_MODAL_CODE || ide->modal == IDE_MODAL_EXPLORE)
            ide->modal = IDE_MODAL_NONE;
        return 1;
    }
    if (sym == XK_F12) {
        ide->view = ide->view == 0 ? 1 : 0;
        if (ide->modal == IDE_MODAL_CODE || ide->modal == IDE_MODAL_EXPLORE)
            ide->modal = IDE_MODAL_NONE;
        return 1;
    }
    if ((ev->xkey.state & ControlMask) == 0) return 0;
    if (sym == XK_s) {
        if (ide->path[0] == '\0') {
            ide->modal = IDE_MODAL_SAVE;
            ide_browse(ide, ide->browse);
        } else ide_save(ide);
        return 1;
    }
    if (sym == XK_o) {
        ide->modal = IDE_MODAL_OPEN;
        ide_browse(ide, ide->browse);
        return 1;
    }
    if (sym == XK_n) {
        ide_blank(ide);
        return 1;
    }
    if (sym == XK_z) {
        ide_undo(ide);
        return 1;
    }
    if (sym == XK_y) {
        ide_redo(ide);
        return 1;
    }
    if (sym == XK_b) {
        ide_build(ide, 1);
        return 1;
    }
    if (sym == XK_q) {
        ide->modal = -1;
        return 1;
    }
    if (sym == XK_minus || sym == XK_KP_Subtract) {
        ide_ui_bump(ide, -25);
        return 1;
    }
    if (sym == XK_plus || sym == XK_equal || sym == XK_KP_Add) {
        ide_ui_bump(ide, 25);
        return 1;
    }
    if (sym == XK_0 || sym == XK_KP_0) {
        ide_ui_set(ide, 100);
        return 1;
    }
    return 0;
}

static int
screen_dpi(Display *dpy, int screen)
{
    int mm = DisplayWidthMM(dpy, screen);
    int pxw = DisplayWidth(dpy, screen);
    const char *xdpi;
    int dpi = 0;
    int i;
    if (mm > 0 && pxw > 0) {
        long v = (long)pxw * 254L / ((long)mm * 10L);
        if (v > 0 && v < 1000) dpi = (int)v;
    }
    xdpi = XGetDefault(dpy, "Xft", "dpi");
    if (xdpi) {
        int from_x = 0;
        int any = 0;
        for (i = 0; xdpi[i] >= '0' && xdpi[i] <= '9' && i < 4; i++) {
            any = 1;
            from_x = from_x * 10 + (xdpi[i] - '0');
        }
        if (any && from_x > dpi && from_x < 1000) dpi = from_x;
    }
    return dpi;
}

static XFont *
ui_font(Display *dpy, int pct)
{
    static const char *const faces[] = {
        "DejaVu Sans",
        "Liberation Sans",
        "Ubuntu",
        "Sans"
    };
    char name[160];
    int pixels;
    int i;
    if (pct < 100) pct = 100;
    if (pct > 300) pct = 300;
    pixels = 13 * pct / 100;
    if (pixels < 13) pixels = 13;
    if (pixels > 40) pixels = 40;
    for (i = 0; i < 4; i++) {
        XFont *font;
        int n = snprintf(name, sizeof name, "%s:pixelsize=%d", faces[i], pixels);
        if (n < 0 || (size_t)n >= sizeof name) continue;
        font = nk_xfont_create(dpy, name);
        if (font && font->ft && font->height > 0) return font;
        if (font && font->ft) nk_xfont_del(dpy, font);
        else free(font);
    }
    fprintf(stderr, "TCCRAD: outline font not found\n");
    return NULL;
}

int
main(int argc, char **argv)
{
    Display *dpy;
    int screen;
    Window root;
    Window win;
    XFont *font;
    struct nk_context *ctx;
    Ide *ide;
    int running = 1;
    int font_pct = 100;
    int font_failed = 0;
    unsigned width = 1280;
    unsigned height = 800;
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "TCCRAD: no DISPLAY\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    ide = ide_new();
    if (!ide) {
        XCloseDisplay(dpy);
        return 1;
    }
    ide_ui_load(ide, DisplayHeight(dpy, screen), screen_dpi(dpy, screen));
    font_pct = ide->ui_pct > 0 ? ide->ui_pct : 100;
    width = 1280u * (unsigned)font_pct / 100u;
    height = 800u * (unsigned)font_pct / 100u;
    {
        int sw = DisplayWidth(dpy, screen);
        int sh = DisplayHeight(dpy, screen);
        if (sw > 80 && width > (unsigned)sw - 48u) width = (unsigned)sw - 48u;
        if (sh > 80 && height > (unsigned)sh - 48u) height = (unsigned)sh - 48u;
    }
    if (width < 640u) width = 640u;
    if (height < 480u) height = 480u;
    win = XCreateSimpleWindow(dpy, root, 40, 40, width, height, 0, 0, 0);
    XStoreName(dpy, win, "TCCRAD");
    {
        Atom wm = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dpy, win, &wm, 1);
    }
    XSelectInput(dpy, win,
        ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
        ButtonReleaseMask | ButtonMotionMask | PointerMotionMask | StructureNotifyMask);
    XMapWindow(dpy, win);
    {
        XSetWindowAttributes attr;
        attr.bit_gravity = NorthWestGravity;
        attr.background_pixel = BlackPixel(dpy, screen);
        XChangeWindowAttributes(dpy, win, CWBitGravity | CWBackPixel, &attr);
    }
    font = ui_font(dpy, font_pct);
    ctx = font ? nk_xlib_init(font, dpy, screen, win,
        DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), width, height) : NULL;
    if (!ctx || !font) {
        fprintf(stderr, "TCCRAD: nuklear init failed\n");
        ide_free(ide);
        nk_xfont_del(dpy, font);
        XCloseDisplay(dpy);
        return 1;
    }
    if (argc > 1 && argv[1][0] != '-') ide_open_path(ide, argv[1]);
    while (running && ide->modal != -1) {
        XEvent ev;
        XWindowAttributes attr;
        nk_input_begin(ctx);
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == ClientMessage) {
                running = 0;
                break;
            }
            if (shortcut(ide, &ev)) continue;
            nk_xlib_handle_event(dpy, screen, win, &ev);
        }
        nk_input_end(ctx);
        if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DEL) && !nk_item_is_any_active(ctx))
            ide_delete(ide);
        XGetWindowAttributes(dpy, win, &attr);
        if (attr.width > 0 && attr.height > 0) {
            width = (unsigned)attr.width;
            height = (unsigned)attr.height;
        }
        if (ide->ui_pct != font_pct && ide->ui_pct != font_failed) {
            XFont *next = ui_font(dpy, ide->ui_pct);
            if (next && next->height > 0) {
                nk_xfont_del(dpy, font);
                font = next;
                nk_xlib_set_font(font);
                font_pct = ide->ui_pct;
                font_failed = 0;
            } else {
                nk_xfont_del(dpy, next);
                font_failed = ide->ui_pct;
            }
        }
        ide_poll_child(ide);
        ide_note_disk(ide);
        ide_frame(ctx, ide, (int)width, (int)height);
        ide_after_frame(ide, nk_item_is_any_active(ctx) ? 1 : 0);
        nk_xlib_render(win, nk_rgb(18, 18, 18));
        XFlush(dpy);
        {
            struct timespec tr_sl;
            tr_sl.tv_sec = 0;
            tr_sl.tv_nsec = 16000000L;
            nanosleep(&tr_sl, NULL);
        }
    }
    ide_free(ide);
    nk_xfont_del(dpy, font);
    nk_xlib_shutdown();
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
