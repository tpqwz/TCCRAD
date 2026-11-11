#ifndef LVGL_STUB_H
#define LVGL_STUB_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct lv_obj_t lv_obj_t;
typedef struct lv_event_t lv_event_t;
typedef struct { uint8_t unused; } lv_color_t;
#define LV_EVENT_CLICKED 1
#define LV_EVENT_VALUE_CHANGED 2
#define LV_STATE_CHECKED 1
#define LV_ANIM_OFF 0
#define LV_FLEX_FLOW_COLUMN 0
#define LV_FLEX_FLOW_ROW 1
lv_obj_t *lv_obj_create(lv_obj_t *parent);
void lv_obj_set_size(lv_obj_t *o, int w, int h);
void lv_obj_set_width(lv_obj_t *o, int w);
void lv_obj_set_height(lv_obj_t *o, int h);
void lv_obj_set_flex_flow(lv_obj_t *o, int flow);
void lv_obj_set_flex_grow(lv_obj_t *o, unsigned grow);
void lv_obj_add_state(lv_obj_t *o, int state);
int lv_obj_has_state(const lv_obj_t *o, int state);
void lv_obj_add_event_cb(lv_obj_t *o, void (*cb)(lv_event_t *), int filter, void *ud);
void lv_obj_set_style_bg_color(lv_obj_t *o, lv_color_t c, int sel);
lv_color_t lv_color_hex(unsigned hex);
int lv_pct(int v);
lv_obj_t *lv_label_create(lv_obj_t *parent);
void lv_label_set_text(lv_obj_t *o, const char *t);
lv_obj_t *lv_button_create(lv_obj_t *parent);
lv_obj_t *lv_checkbox_create(lv_obj_t *parent);
void lv_checkbox_set_text(lv_obj_t *o, const char *t);
lv_obj_t *lv_textarea_create(lv_obj_t *parent);
void lv_textarea_set_text(lv_obj_t *o, const char *t);
void lv_textarea_set_one_line(lv_obj_t *o, int on);
void lv_textarea_set_password_mode(lv_obj_t *o, int on);
const char *lv_textarea_get_text(const lv_obj_t *o);
lv_obj_t *lv_slider_create(lv_obj_t *parent);
void lv_slider_set_range(lv_obj_t *o, int min, int max);
void lv_slider_set_value(lv_obj_t *o, int v, int anim);
int lv_slider_get_value(const lv_obj_t *o);
lv_obj_t *lv_dropdown_create(lv_obj_t *parent);
void lv_dropdown_set_options(lv_obj_t *o, const char *opts);
void lv_dropdown_set_selected(lv_obj_t *o, uint32_t i);
uint32_t lv_dropdown_get_selected(const lv_obj_t *o);
lv_obj_t *lv_bar_create(lv_obj_t *parent);
void lv_bar_set_range(lv_obj_t *o, int min, int max);
void lv_bar_set_value(lv_obj_t *o, int v, int anim);
lv_obj_t *lv_table_create(lv_obj_t *parent);
void lv_table_set_column_count(lv_obj_t *o, uint32_t n);
void lv_table_set_row_count(lv_obj_t *o, uint32_t n);
void lv_table_set_cell_value(lv_obj_t *o, uint32_t row, uint32_t col, const char *t);
lv_obj_t *lv_tabview_create(lv_obj_t *parent);
lv_obj_t *lv_tabview_add_tab(lv_obj_t *tv, const char *name);
lv_obj_t *lv_image_create(lv_obj_t *parent);
void lv_image_set_src(lv_obj_t *o, const char *src);
lv_obj_t *lv_layer_top(void);
void *lv_event_get_user_data(lv_event_t *e);
lv_obj_t *lv_event_get_target(lv_event_t *e);
void lv_init(void);
void lv_screen_load(lv_obj_t *s);
uint32_t lv_timer_handler(void);
#endif
