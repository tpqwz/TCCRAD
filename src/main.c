#define NK_IMPLEMENTATION
#define NK_XLIB_IMPLEMENTATION
#include "nk_glue.h"
#include "nuklear_xlib.h"

#include "ide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

static int
shortcut(Ide *ide, XEvent *ev)
{
    KeySym sym;
    if (ev->type != KeyPress || (ev->xkey.state & ControlMask) == 0) return 0;
    sym = XLookupKeysym(&ev->xkey, 0);
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
    return 0;
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
    unsigned width = 1280;
    unsigned height = 800;
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "tccrad: no DISPLAY\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    win = XCreateSimpleWindow(dpy, root, 40, 40, width, height, 0, 0, 0);
    XStoreName(dpy, win, "tccrad");
    {
        Atom wm = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dpy, win, &wm, 1);
    }
    XSelectInput(dpy, win,
        ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
        ButtonReleaseMask | ButtonMotionMask | PointerMotionMask | StructureNotifyMask);
    XMapWindow(dpy, win);
    font = nk_xfont_create(dpy, "fixed");
    ctx = nk_xlib_init(font, dpy, screen, win, width, height);
    if (!ctx || !font) {
        fprintf(stderr, "tccrad: nuklear init failed\n");
        XCloseDisplay(dpy);
        return 1;
    }
    ide = ide_new();
    if (!ide) return 1;
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
        ide_poll_child(ide);
        ide_note_disk(ide);
        ide_frame(ctx, ide, (int)width, (int)height);
        ide_after_frame(ide, nk_item_is_any_active(ctx) ? 1 : 0);
        XClearWindow(dpy, win);
        nk_xlib_render(win, nk_rgb(18, 18, 18));
        XFlush(dpy);
    }
    ide_free(ide);
    nk_xfont_del(dpy, font);
    nk_xlib_shutdown();
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
