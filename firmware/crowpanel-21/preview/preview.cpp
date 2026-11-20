// Renders the panel's screens on a PC with the firmware's own code: panel_feed.h parses the sample feeds, panel_nav.h is
// driven with the same knob calls the device makes, panel_view.h makes the text, panel_ui.h copies it onto LVGL widgets
// that are created exactly as crowpanel-21.yaml declares them (same fonts, sizes, positions, colours, LVGL 9.5, RGB565).
// Output: one P6 .ppm and one .txt (the View text) per shot. See preview/README-preview in run.sh.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "lvgl.h"
#include "panel_helpers.h"

using namespace panel;

static lv_display_t *disp;
alignas(64) static uint8_t draw_buf[480 * 480 * 2];
static uint8_t rgb[480 * 480 * 3];

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  int w = a->x2 - a->x1 + 1;
  for (int y = a->y1; y <= a->y2; y++) {
    const uint16_t *s = (const uint16_t *) (px + (size_t) (y - a->y1) * w * 2);
    for (int x = a->x1; x <= a->x2; x++) {
      uint16_t c = s[x - a->x1];
      uint8_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
      uint8_t *o = rgb + ((size_t) y * 480 + x) * 3;
      o[0] = (r << 3) | (r >> 2); o[1] = (g << 2) | (g >> 4); o[2] = (b << 3) | (b >> 2);   // RGB565 -> 888 like the panel
    }
  }
  lv_display_flush_ready(d);
}

// Same calls ESPHome generates for a `label:` with these keys (see build/.../main.cpp of the firmware).
static lv_obj_t *mk_label(lv_obj_t *parent, int y, int w, const lv_font_t *font, uint32_t color) {
  lv_obj_t *o = lv_label_create(parent);
  lv_obj_set_style_align(o, LV_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(o, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_font(o, font, LV_PART_MAIN);
  lv_obj_set_style_width(o, w, LV_PART_MAIN);
  lv_obj_set_style_y(o, y, LV_PART_MAIN);
  lv_label_set_text(o, "");
  lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
  return o;
}

static Widgets W;

static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
  W.title = mk_label(scr, -168, 360, &lv_font_montserrat_20, 0x4DA3FF);
  W.big = mk_label(scr, -105, 380, &lv_font_montserrat_48, 0xFFFFFF);
  lv_label_set_text(W.big, "WAIT");
  W.bar = lv_bar_create(scr);
  lv_obj_set_style_align(W.bar, LV_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(W.bar, lv_color_hex(0x202020), LV_PART_MAIN);
  lv_obj_set_style_height(W.bar, 8, LV_PART_MAIN);
  lv_obj_set_style_width(W.bar, 240, LV_PART_MAIN);
  lv_obj_set_style_y(W.bar, -62, LV_PART_MAIN);
  lv_obj_set_style_bg_color(W.bar, lv_color_hex(0x4DA3FF), LV_PART_INDICATOR);
  lv_obj_add_flag(W.bar, LV_OBJ_FLAG_HIDDEN);
  lv_bar_set_range(W.bar, 0, 100);
  lv_bar_set_value(W.bar, 0, LV_ANIM_OFF);
  W.l[0] = mk_label(scr, -25, 390, &lv_font_montserrat_20, 0xFFFFFF);
  W.l[1] = mk_label(scr, 17, 390, &lv_font_montserrat_20, 0xA0A6B0);
  W.l[2] = mk_label(scr, 59, 390, &lv_font_montserrat_20, 0xA0A6B0);
  W.l[3] = mk_label(scr, 101, 390, &lv_font_montserrat_20, 0xA0A6B0);
  W.foot = mk_label(scr, 160, 320, &lv_font_montserrat_16, 0xA0A6B0);
  // alert badge (yaml: w_badge)
  W.badge = lv_label_create(scr);
  lv_obj_set_style_align(W.badge, LV_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(W.badge, lv_color_hex(0xFF3B30), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(W.badge, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_height(W.badge, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_top(W.badge, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(W.badge, 13, LV_PART_MAIN);
  lv_obj_set_style_text_align(W.badge, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(W.badge, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(W.badge, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_width(W.badge, 116, LV_PART_MAIN);
  lv_obj_set_style_y(W.badge, -204, LV_PART_MAIN);
  lv_label_set_text(W.badge, "");
  lv_label_set_long_mode(W.badge, LV_LABEL_LONG_CLIP);
  lv_obj_add_flag(W.badge, LV_OBJ_FLAG_HIDDEN);
  // ring in the top layer
  W.ring = lv_obj_create(lv_display_get_layer_top(disp));
  lv_obj_set_style_align(W.ring, LV_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(W.ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(W.ring, lv_color_hex(0x606060), LV_PART_MAIN);
  lv_obj_set_style_border_width(W.ring, 14, LV_PART_MAIN);
  lv_obj_set_style_height(W.ring, 480, LV_PART_MAIN);
  lv_obj_set_style_radius(W.ring, 240, LV_PART_MAIN);
  lv_obj_set_style_width(W.ring, 480, LV_PART_MAIN);
  lv_obj_remove_flag(W.ring, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
}

static std::string slurp(const std::string &p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void write_ppm(const std::string &p) {
  FILE *f = fopen(p.c_str(), "wb");
  fprintf(f, "P6\n480 480\n255\n");
  fwrite(rgb, 1, sizeof rgb, f);
  fclose(f);
}

// manifest line: name \t feed file or - \t age in seconds \t actions (R+ R- P L) \t unused
int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: preview manifest.tsv feeds_dir out_dir\n"); return 2; }
  std::string feeds = argv[2], out = argv[3];
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { static uint32_t t = 0; return t += 20; });
  disp = lv_display_create(480, 480);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof draw_buf, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, flush_cb);
  build_ui();

  std::ifstream mf(argv[1]);
  std::string line;
  int bad = 0, n = 0;
  while (std::getline(mf, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> c;
    std::stringstream ls(line);
    std::string t;
    while (std::getline(ls, t, '\t')) c.push_back(t);
    if (c.size() < 4) continue;
    const std::string &name = c[0];
    g_feed = Feed(); g_nav = Nav();
    const uint32_t T0 = 100000;
    uint32_t now = (uint32_t) (atof(c[2].c_str()) * 1000);
    if (c[1] != "-") {
      JsonDocument doc;
      if (deserializeJson(doc, slurp(feeds + "/" + c[1] + ".json"))) { fprintf(stderr, "bad json %s\n", c[1].c_str()); bad++; continue; }
      if (!parse_feed(doc.as<JsonObjectConst>(), g_feed, T0)) { fprintf(stderr, "feed rejected %s\n", c[1].c_str()); bad++; continue; }
      on_feed(g_nav, g_feed, T0);
      uint32_t at = T0 + 1000;
      std::stringstream as(c[3]);
      std::string a;
      while (as >> a) {
        if (a == "R+") on_rotate(g_nav, g_feed, 1, at);
        else if (a == "R-") on_rotate(g_nav, g_feed, -1, at);
        else if (a == "P") on_press(g_nav, g_feed, at);
        else if (a == "L") on_long_press(g_nav, g_feed, at);
        else if (a == "F") on_feed(g_nav, g_feed, at);
        at += 500;
      }
      now = T0 + now;   // age since the feed arrived
    }
    on_tick(g_nav, g_feed, now);
    Clock ck; ck.valid = true; snprintf(ck.time, sizeof ck.time, "21:47"); snprintf(ck.date, sizeof ck.date, "Fri 20 Nov");
    static View v;
    make_view(g_feed, g_nav, ck, now, v);
    apply(W, v);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_display_get_layer_top(disp));
    for (int i = 0; i < 3; i++) lv_timer_handler();
    lv_refr_now(disp);
    write_ppm(out + "/" + name + ".ppm");
    std::ofstream tf(out + "/" + name + ".txt");
    tf << "title|" << v.title << "\nbig|" << v.big << "\nbar|" << v.bar << "\n";
    for (int i = 0; i < 4; i++) tf << "l" << i << "|" << v.l[i] << "\n";
    tf << "foot|" << v.foot << "\nbadge|" << v.badge << "\nring|" << std::hex << v.ring << std::dec << "\n";
    n++;
  }
  fprintf(stderr, "rendered %d shots, %d failed\n", n, bad);
  return bad ? 1 : 0;
}
