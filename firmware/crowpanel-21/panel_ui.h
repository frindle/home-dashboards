#pragma once
// LVGL glue: copies a View (panel_view.h) onto the widgets declared in crowpanel-21.yaml. Only compiled into the
// firmware; the host tests do not include this file.
#include "lvgl.h"
#include "panel_view.h"

namespace panel {

struct Widgets {
  lv_obj_t *title, *big, *bar, *l[4], *foot, *ring;
};

inline void set_text(lv_obj_t *o, const char *s, uint32_t color) {
  if (!o) return;
  if (strcmp(lv_label_get_text(o), s) != 0) lv_label_set_text(o, s);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
}

// Only touches LVGL objects whose content changed, so a repeat call every second costs next to nothing.
inline void apply(const Widgets &w, const View &v) {
  set_text(w.title, v.title, v.c_title);
  set_text(w.big, v.big, v.c_big);
  for (int i = 0; i < 4; i++) set_text(w.l[i], v.l[i], v.c_l[i]);
  set_text(w.foot, v.foot, v.c_foot);
  if (w.bar) {
    if (v.bar < 0) lv_obj_add_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
    else {
      lv_obj_clear_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(w.bar, v.bar, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(w.bar, lv_color_hex(v.c_bar), LV_PART_INDICATOR);
    }
  }
  if (w.ring) lv_obj_set_style_border_color(w.ring, lv_color_hex(v.ring), 0);
}

}  // namespace panel
