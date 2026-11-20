#pragma once
// LVGL glue: draws a Scene (panel_scene.h) on pooled LVGL objects. Only compiled into the firmware and the preview
// harness; the host tests do not include this file. All objects are created once (arcs, rectangles, lines, labels), so
// switching screens only changes properties: no heap churn on a long-running panel. Needs LV_USE_ARC and LV_USE_LINE
// (crowpanel-21.yaml declares hidden `arc:` and `line:` widgets so ESPHome turns them on).
#include <cmath>
#include "lvgl.h"
#include "panel_scene.h"

namespace panel {

constexpr int UI_ARCS = 4, UI_RECTS = 72, UI_LINES = 32, UI_LABELS = 44, UI_PTS = 72;

struct Ui {
  const lv_font_t *font[4] = {nullptr, nullptr, nullptr, nullptr};   // F96, F40, F22, F15
  bool inited = false, animate = true;
  lv_obj_t *root = nullptr, *ring = nullptr, *fade = nullptr;
  lv_obj_t *arc[UI_ARCS], *rect[UI_RECTS], *ln[UI_LINES], *lab[UI_LABELS];
  lv_point_precise_t pts[UI_LINES][UI_PTS];
  uint32_t hash = 0, key = 0;
  bool have = false;
  int phase = 0;        // 0 idle, 1 fading to black, 2 fading back
  Scene pending;
};
inline Ui g_ui;

namespace ui {

inline void plain(lv_obj_t *o) {
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));
  lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
}

inline void init(Ui &u, lv_obj_t *scr) {
  if (u.inited) return;
  u.inited = true;
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
  u.root = lv_obj_create(scr);
  plain(u.root);
  lv_obj_set_size(u.root, 480, 480);
  lv_obj_set_pos(u.root, 0, 0);
  for (int i = 0; i < UI_ARCS; i++) {
    lv_obj_t *a = u.arc[i] = lv_arc_create(u.root);
    plain(a);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_opa(a, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_arc_set_rotation(a, 0);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < UI_RECTS; i++) {
    lv_obj_t *r = u.rect[i] = lv_obj_create(u.root);
    plain(r);
    lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < UI_LINES; i++) {
    lv_obj_t *l = u.ln[i] = lv_line_create(u.root);
    lv_obj_remove_flag(l, (lv_obj_flag_t) LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_rounded(l, true, LV_PART_MAIN);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < UI_LABELS; i++) {
    lv_obj_t *l = u.lab[i] = lv_label_create(u.root);
    lv_obj_remove_flag(l, (lv_obj_flag_t) LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
  }
  // black overlay used for the fade, then the status ring above everything
  u.fade = lv_obj_create(lv_display_get_layer_top(nullptr));
  plain(u.fade);
  lv_obj_set_size(u.fade, 480, 480);
  lv_obj_set_pos(u.fade, 0, 0);
  lv_obj_set_style_bg_color(u.fade, lv_color_hex(C_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(u.fade, LV_OPA_TRANSP, LV_PART_MAIN);
  u.ring = lv_obj_create(lv_display_get_layer_top(nullptr));
  plain(u.ring);
  lv_obj_set_size(u.ring, 480, 480);
  lv_obj_set_pos(u.ring, 0, 0);
  lv_obj_set_style_radius(u.ring, 240, LV_PART_MAIN);
  lv_obj_set_style_border_side(u.ring, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
}

inline void setpt(lv_point_precise_t &p, int x, int y) { p.x = (lv_value_precise_t) x; p.y = (lv_value_precise_t) y; }

inline uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
inline uint32_t hash_scene(const Scene &s) {
  uint32_t h = 2166136261u;
  h = mix(h, s.ring); h = mix(h, (uint32_t) s.ringw); h = mix(h, s.n); h = mix(h, s.ng);
  for (int i = 0; i < s.n; i++) {
    const Item &t = s.it[i];
    h = mix(h, t.k | t.font << 8 | t.al << 16 | t.icon << 24);
    h = mix(h, (uint32_t) (t.x & 0xFFFF) | (uint32_t) (t.y & 0xFFFF) << 16);
    h = mix(h, (uint32_t) (t.w & 0xFFFF) | (uint32_t) (t.h & 0xFFFF) << 16);
    h = mix(h, (uint32_t) (t.r & 0xFFFF) | (uint32_t) (t.a0 & 0xFFFF) << 16);
    h = mix(h, (uint32_t) (t.a1 & 0xFFFF) | (uint32_t) (t.ls & 0xFF) << 16 | (uint32_t) t.grp << 24);
    h = mix(h, t.col); h = mix(h, t.bg); h = mix(h, t.rnd | t.lh << 8 | (uint32_t) (t.bw & 0xFF) << 16);
    for (const char *c = t.s; *c; c++) h = mix(h, (uint8_t) *c);
  }
  for (int i = 0; i < s.ng; i++) {
    const Group &g = s.g[i];
    h = mix(h, (uint32_t) (g.cx & 0xFFFF) | (uint32_t) (g.gap & 0xFFFF) << 16);
    h = mix(h, (uint32_t) g.pill | (uint32_t) (g.py & 0xFFFF) << 8); h = mix(h, g.pbg);
  }
  return h;
}

inline int text_w(const Ui &u, const Item &t) {
  lv_point_t sz;
  lv_text_get_size(&sz, t.s, u.font[t.font], t.ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}

// Draws the scene on the pooled objects (no animation).
inline void draw(Ui &u, const Scene &s) {
  int na = 0, nr = 0, nl = 0, nb = 0;
  // ring
  if (s.ringw > 0) {
    lv_obj_remove_flag(u.ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(u.ring, s.ringw, LV_PART_MAIN);
    lv_obj_set_style_border_color(u.ring, lv_color_hex(s.ring), LV_PART_MAIN);
  } else lv_obj_add_flag(u.ring, LV_OBJ_FLAG_HIDDEN);

  // widths and group layout
  int wd[MAXI];
  int gsum[MAXG] = {0}, gcnt[MAXG] = {0}, gstart[MAXG] = {0}, gcur[MAXG] = {0};
  for (int i = 0; i < s.n; i++) {
    const Item &t = s.it[i];
    wd[i] = t.k == K_TEXT ? text_w(u, t) : (t.k == K_ICON || t.k == K_RECT) ? t.w : 0;
    if (t.grp && t.grp <= s.ng) { gsum[t.grp - 1] += wd[i]; gcnt[t.grp - 1]++; }
  }
  for (int g = 0; g < s.ng; g++) {
    const Group &gr = s.g[g];
    int total = gsum[g] + (gcnt[g] > 1 ? gr.gap * (gcnt[g] - 1) : 0) + (gr.pill ? gr.padl + gr.padr : 0);
    gstart[g] = gr.cx - total / 2;
    gcur[g] = gstart[g] + (gr.pill ? gr.padl : 0);
    if (gr.pill && gcnt[g] > 0 && nr < UI_RECTS) {
      lv_obj_t *r = u.rect[nr++];
      lv_obj_remove_flag(r, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(r, gstart[g], gr.py);
      lv_obj_set_size(r, total, gr.ph);
      lv_obj_set_style_radius(r, gr.pr, LV_PART_MAIN);
      lv_obj_set_style_bg_color(r, lv_color_hex(gr.pbg), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
    }
  }
  // items (rectangles first so text sits above them; rects come before labels in z order anyway)
  for (int i = 0; i < s.n; i++) {
    const Item &t = s.it[i];
    int x = t.x;
    if (t.grp && t.grp <= s.ng) {
      x = gcur[t.grp - 1];
      gcur[t.grp - 1] += wd[i] + s.g[t.grp - 1].gap;
    }
    switch (t.k) {
      case K_RECT: {
        if (nr >= UI_RECTS) break;
        lv_obj_t *r = u.rect[nr++];
        lv_obj_remove_flag(r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(r, x, t.y);
        lv_obj_set_size(r, t.w, t.h);
        lv_obj_set_style_radius(r, t.r, LV_PART_MAIN);
        lv_obj_set_style_bg_color(r, lv_color_hex(t.col), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(r, t.bw, LV_PART_MAIN);
        lv_obj_set_style_border_color(r, lv_color_hex(t.bg), LV_PART_MAIN);
        break;
      }
      case K_ARC: {
        if (t.rnd == 2) {   // limit tick: line across the arc, radius 187 to 213, 4 px wide
          if (nl >= UI_LINES) break;
          float a = t.a0 * 3.14159265f / 180.0f, c = cosf(a), sn = sinf(a);
          int x0 = (int) lroundf(240 + 187 * c), y0 = (int) lroundf(240 + 187 * sn);
          int x1 = (int) lroundf(240 + 213 * c), y1 = (int) lroundf(240 + 213 * sn);
          int mx = (x0 < x1 ? x0 : x1), my = (y0 < y1 ? y0 : y1);
          lv_obj_t *l = u.ln[nl];
          setpt(u.pts[nl][0], x0 - mx, y0 - my);
          setpt(u.pts[nl][1], x1 - mx, y1 - my);
          lv_line_set_points(l, u.pts[nl], 2);
          lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_pos(l, mx, my);
          lv_obj_set_size(l, (x0 > x1 ? x0 : x1) - mx + 1, (y0 > y1 ? y0 : y1) - my + 1);
          lv_obj_set_style_line_width(l, 4, LV_PART_MAIN);
          lv_obj_set_style_line_color(l, lv_color_hex(t.col), LV_PART_MAIN);
          lv_obj_set_style_line_rounded(l, false, LV_PART_MAIN);
          nl++;
          break;
        }
        if (na >= UI_ARCS) break;
        lv_obj_t *a = u.arc[na++];
        int cx = t.x ? t.x : 240, cy = t.y ? t.y : 240;
        lv_obj_remove_flag(a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(a, t.w, t.w);
        lv_obj_set_pos(a, cx - t.w / 2, cy - t.w / 2);
        lv_arc_set_bg_angles(a, ((t.a0 % 360) + 360) % 360, ((t.a1 % 360) + 360) % 360);
        lv_obj_set_style_arc_width(a, t.h, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, lv_color_hex(t.col), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(a, t.rnd != 0, LV_PART_MAIN);
        break;
      }
      case K_ICON: {
        const IconDef &d = ICONS[t.icon];
        if (!d.d) break;
        float sc_ = t.w / 24.0f / 8.0f;
        const uint8_t *p = d.d;
        int nlines = *p++;
        for (int k = 0; k < nlines && nl < UI_LINES; k++) {
          int np = *p++;
          if (np > UI_PTS) np = UI_PTS;
          int mnx = 99999, mny = 99999, mxx = -99999, mxy = -99999;
          for (int q = 0; q < np; q++) {
            int px = x + (int) lroundf(p[q * 2] * sc_), py = t.y + (int) lroundf(p[q * 2 + 1] * sc_);
            setpt(u.pts[nl][q], px, py);
            if (px < mnx) mnx = px; if (px > mxx) mxx = px;
            if (py < mny) mny = py; if (py > mxy) mxy = py;
          }
          for (int q = 0; q < np; q++) setpt(u.pts[nl][q], (int) u.pts[nl][q].x - mnx, (int) u.pts[nl][q].y - mny);
          p += np * 2;
          lv_obj_t *l = u.ln[nl];
          lv_line_set_points(l, u.pts[nl], np);
          lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_pos(l, mnx, mny);
          lv_obj_set_size(l, mxx - mnx + 1, mxy - mny + 1);
          float sw = d.stroke10 / 10.0f * t.w / 24.0f;
          lv_obj_set_style_line_width(l, (int) lroundf(sw), LV_PART_MAIN);
          lv_obj_set_style_line_color(l, lv_color_hex(t.col), LV_PART_MAIN);
          lv_obj_set_style_line_rounded(l, true, LV_PART_MAIN);
          nl++;
        }
        break;
      }
      case K_TEXT: {
        if (nb >= UI_LABELS) break;
        lv_obj_t *o = u.lab[nb++];
        const lv_font_t *f = u.font[t.font];
        int w = t.w ? t.w : wd[i] + 2;
        int left = x;
        if (!(t.grp && t.grp <= s.ng)) {
          if (t.al == 0) left = x - w / 2;
          else if (t.al == 2) left = x - w;
        } else w = wd[i] + 2;
        int top = t.y - (f->line_height - f->base_line);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(o, f, LV_PART_MAIN);
        lv_obj_set_style_text_color(o, lv_color_hex(t.col), LV_PART_MAIN);
        lv_obj_set_style_text_letter_space(o, t.ls, LV_PART_MAIN);
        lv_obj_set_style_text_align(o, t.al == 0 ? LV_TEXT_ALIGN_CENTER : t.al == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        if (t.h > 0) {
          lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
          lv_obj_set_style_text_line_space(o, t.lh ? t.lh - f->line_height : 0, LV_PART_MAIN);
          lv_obj_set_size(o, w, t.h);
        } else {
          lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
          lv_obj_set_style_text_line_space(o, 0, LV_PART_MAIN);
          lv_obj_set_size(o, w, f->line_height);
        }
        lv_label_set_text(o, t.s);
        lv_obj_set_pos(o, left, top);
        break;
      }
    }
  }
  for (int i = na; i < UI_ARCS; i++) lv_obj_add_flag(u.arc[i], LV_OBJ_FLAG_HIDDEN);
  for (int i = nr; i < UI_RECTS; i++) lv_obj_add_flag(u.rect[i], LV_OBJ_FLAG_HIDDEN);
  for (int i = nl; i < UI_LINES; i++) lv_obj_add_flag(u.ln[i], LV_OBJ_FLAG_HIDDEN);
  for (int i = nb; i < UI_LABELS; i++) lv_obj_add_flag(u.lab[i], LV_OBJ_FLAG_HIDDEN);
}

inline void fade_exec(void *, int32_t v) { lv_obj_set_style_bg_opa(g_ui.fade, (lv_opa_t) v, LV_PART_MAIN); }
inline void fade_done(lv_anim_t *) {
  Ui &u = g_ui;
  if (u.phase == 1) {
    draw(u, u.pending);
    u.phase = 2;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &u);
    lv_anim_set_exec_cb(&a, fade_exec);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_duration(&a, 60);
    lv_anim_set_completed_cb(&a, fade_done);
    lv_anim_start(&a);
  } else u.phase = 0;
}

}  // namespace ui

// Call from the LVGL thread (ESPHome lambdas already run there). `fonts` = F96, F40, F22, F15 in that order.
inline void apply(Ui &u, const Scene &s, const lv_font_t *const fonts[4]) {
  for (int i = 0; i < 4; i++) u.font[i] = fonts[i];
  if (!u.inited) ui::init(u, lv_screen_active());
  uint32_t h = ui::hash_scene(s);
  if (u.have && h == u.hash && u.phase == 0) return;
  if (u.phase != 0) {   // mid-fade: just replace what will be drawn at the midpoint
    u.pending = s;
    u.hash = h; u.key = s.key;
    return;
  }
  bool change = u.have && s.key != u.key && u.animate;
  u.hash = h; u.key = s.key; u.have = true;
  if (!change) { ui::draw(u, s); return; }
  u.pending = s;
  u.phase = 1;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &u);
  lv_anim_set_exec_cb(&a, ui::fade_exec);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_duration(&a, 60);
  lv_anim_set_completed_cb(&a, ui::fade_done);
  lv_anim_start(&a);
}

}  // namespace panel
