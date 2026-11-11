/**
 * Desktop config for programs tccrad builds against the vendored LVGL 9.4 tree.
 * Anything not set here keeps the default in src/lv_conf_internal.h.
 * The X11 driver is on so a generated program can open a window with libX11.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

#define LV_USE_X11 1
#define LV_X11_DIRECT_EXIT 1
#define LV_X11_DOUBLE_BUFFER 1
#define LV_X11_RENDER_MODE_PARTIAL 1

#endif
