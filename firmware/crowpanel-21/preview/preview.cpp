// Renders the panel's screens on a PC with the firmware's own code: panel_feed.h parses the sample feeds, panel_nav.h is
// driven with the same knob calls the device makes, panel_scene.h lays out the screen and panel_ui.h draws it on LVGL
// objects (LVGL 9.5, RGB565). Fonts are the design's Montserrat TTFs rendered by tiny_ttf; the device uses ESPHome `font:`
// bitmaps of the same files, so only the anti-aliasing can differ.
// Output: one P6 .ppm and one .txt (the scene's text items) per shot.
#include <cstdio>
#include <ctime>
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

static std::string slurp(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
static const lv_font_t *ttf(const std::string &path, int px) {
  std::string *d = new std::string(slurp(path));   // lives for the whole run
  if (d->empty()) { fprintf(stderr, "missing font %s\n", path.c_str()); exit(2); }
  const lv_font_t *f = lv_tiny_ttf_create_data(d->data(), d->size(), px);
  if (!f) { fprintf(stderr, "font load failed %s\n", path.c_str()); exit(2); }
  return f;
}

static void write_ppm(const std::string &p) {
  FILE *f = fopen(p.c_str(), "wb");
  fprintf(f, "P6\n480 480\n255\n");
  fwrite(rgb, 1, sizeof rgb, f);
  fclose(f);
}

// manifest line: name \t feed file or - \t age in seconds \t actions (R+ R- P L F) \t options
// options: b=<brightness>  noclock  sys=boot|wifi|ota  pct=<n>
int main(int argc, char **argv) {
  if (argc < 5) { fprintf(stderr, "usage: preview manifest.tsv feeds_dir out_dir fonts_dir\n"); return 2; }
  std::string feeds = argv[2], out = argv[3], fdir = argv[4];
  setenv("TZ", "UTC", 1); tzset();
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { static uint32_t t = 0; return t += 20; });
  disp = lv_display_create(480, 480);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof draw_buf, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, flush_cb);
  const lv_font_t *fonts[4] = {ttf(fdir + "/Montserrat-SemiBold.ttf", 96), ttf(fdir + "/Montserrat-SemiBold.ttf", 40),
                               ttf(fdir + "/Montserrat-Medium.ttf", 22), ttf(fdir + "/Montserrat-SemiBold.ttf", 15)};
  g_ui.animate = false;

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
    std::string opts = c.size() > 4 ? c[4] : "";
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
    // fixed clock: Friday 13 Nov 2026, 9:58 (the design's sample); TZ is UTC on the sandbox
    Clock ck;
    if (opts.find("noclock") == std::string::npos) {
      struct tm tmv = {};
      tmv.tm_year = 2026 - 1900; tmv.tm_mon = 10; tmv.tm_mday = 13; tmv.tm_hour = 9; tmv.tm_min = 58;
      ck.valid = true; ck.t = timegm(&tmv);
    }
    int br = brightness(g_feed, g_nav, now);
    size_t p = opts.find("b=");
    if (p != std::string::npos) br = atoi(opts.c_str() + p + 2);
    Sys sy;
    std::string mode = "";
    p = opts.find("sys=");
    if (p != std::string::npos) mode = opts.substr(p + 4, opts.find(' ', p) - p - 4);
    p = opts.find("pct=");
    if (p != std::string::npos) sy.pct = atoi(opts.c_str() + p + 4);
    snprintf(sy.version, sizeof sy.version, "1.5.0");
    snprintf(sy.ap, sizeof sy.ap, "crowpanel-21 setup");
    if (mode == "boot") sy.mode = SYS_BOOT; else if (mode == "wifi") sy.mode = SYS_WIFI; else if (mode == "ota") sy.mode = SYS_OTA;
    if (sy.mode) make_system(sy, g_scene); else make_scene(g_feed, g_nav, ck, now, br, g_scene);
    apply(g_ui, g_scene, fonts);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_display_get_layer_top(disp));
    for (int i = 0; i < 3; i++) lv_timer_handler();
    lv_refr_now(disp);
    write_ppm(out + "/" + name + ".ppm");
    std::ofstream tf(out + "/" + name + ".txt");
    for (int i = 0; i < g_scene.n; i++) if (g_scene.it[i].k == K_TEXT) tf << g_scene.it[i].s << "\n";
    n++;
  }
  fprintf(stderr, "rendered %d shots, %d failed\n", n, bad);
  return bad ? 1 : 0;
}
