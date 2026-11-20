#pragma once
// Turns (feed, navigation state, clock, backlight) into ONE screen as a flat list of drawing items (text, icon, rectangle,
// arc) in panel pixels. Plain C++, no LVGL: test/panel_test.cpp checks the content on a normal machine, panel_ui.h draws
// the items on LVGL objects. Layout follows the designer's artboards (480x480 round, black ground, zones: marker y 52-82,
// content y 96-368 x 50-430, hint y 372, dots y 404).
//
// Text is placed by BASELINE (item y). Items that share a `grp` form a row: the UI measures them and centres the row on the
// group's cx (so "56" + "%" or an icon + a label sit together like the design's flex rows).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>
#include "panel_icons.h"
#include "panel_nav.h"

namespace panel {

constexpr uint32_t C_BG = 0x000000, C_WHITE = 0xFFFFFF, C_LABEL = 0xA0A6B0, C_BLUE = 0x6FA8FF, C_IDLE = 0x3A3F47,
                   C_TRACK = 0x1E2126, C_OK = 0x3DD68C, C_WARN = 0xFFB547, C_BAD = 0xFF5A5A, C_TILE = 0x111317,
                   P_AMBER = 0x2A1F0A, P_BLUE = 0x13243D, P_GREEN = 0x0F2A1C, P_GREY = 0x1A1D22;

enum Font : uint8_t { F96, F40, F22, F15 };
enum Kind : uint8_t { K_NONE, K_TEXT, K_ICON, K_RECT, K_ARC };

struct Item {
  uint8_t k = K_NONE, font = F22, al = 0 /*0 centre, 1 left, 2 right*/, icon = 0, grp = 0 /*group index + 1*/;
  int8_t ls = 0;            // letter spacing, px
  uint8_t lh = 0;           // wrapped text: line height px (0 = font default)
  uint8_t rnd = 0;          // arc: round caps
  int16_t x = 0, y = 0, w = 0, h = 0, r = 0;
  int16_t a0 = 0, a1 = 0;   // arc angles, degrees clockwise from 3 o'clock
  int8_t bw = 0;            // rect border width
  uint32_t col = 0, bg = 0; // colour; rect/pill border colour in bg
  char s[44] = "";
};
// text: x anchor (centre/left/right per al), y baseline, w box width (0 = natural), h > 0 = may wrap to this height
// icon: x left, y top, w size, col stroke.   rect: x y w h r(radius) col bw bg(border).   arc: w diameter, h stroke

struct Group {
  int16_t cx = 240, gap = 0;
  bool pill = false;
  int16_t py = 0, ph = 0, padl = 0, padr = 0, pr = 0;
  uint32_t pbg = 0;
};

constexpr int MAXI = 128, MAXG = 16;
// LVGL object pools used by panel_ui.h (created once). Items past a pool are silently dropped, so every scene must fit:
// the host tests check it with pool_use().
constexpr int UI_ARCS = 4, UI_RECTS = 72, UI_LINES = 32, UI_LABELS = 44;
struct Scene {
  Item it[MAXI];
  int n = 0;
  Group g[MAXG];
  int ng = 0;
  uint32_t ring = C_OK;
  int ringw = 4;
  uint32_t key = 0;    // changes when the screen is a different page (the UI fades through black)
  bool night = false;
};

// How many pooled LVGL objects panel_ui.h needs for a scene: arcs, rects (group pills count), lines (icon strokes and
// limit ticks), labels. Must stay in step with ui::draw().
struct PoolUse { int arcs = 0, rects = 0, lines = 0, labels = 0; };
inline PoolUse pool_use(const Scene &s) {
  PoolUse p;
  int gcnt[MAXG] = {0};
  for (int i = 0; i < s.n; i++) if (s.it[i].grp && s.it[i].grp <= s.ng) gcnt[s.it[i].grp - 1]++;
  for (int g = 0; g < s.ng; g++) if (s.g[g].pill && gcnt[g] > 0) p.rects++;
  for (int i = 0; i < s.n; i++) {
    const Item &t = s.it[i];
    switch (t.k) {
      case K_RECT: p.rects++; break;
      case K_ARC: if (t.rnd == 2) p.lines++; else p.arcs++; break;
      case K_ICON: if (ICONS[t.icon].d) p.lines += ICONS[t.icon].d[0]; break;
      case K_TEXT: p.labels++; break;
    }
  }
  return p;
}

struct Clock { bool valid = false; time_t t = 0; };   // t: seconds since 1970; local time via localtime_r

// Updating / Wi-Fi setup / boot screens. mode 0 = normal.
struct Sys { int mode = 0; int pct = 0; char version[16] = ""; char ap[32] = ""; };
enum { SYS_NONE = 0, SYS_BOOT, SYS_WIFI, SYS_OTA };

// ---------------------------------------------------------------------------------------------------------------- helpers
namespace sc {

// CSS-like metrics of Montserrat: line box height for line-height normal, baseline from the top of a box of `size`.
constexpr float H15 = 18.3f, H22 = 26.8f;
constexpr float BASE15 = 14.5f, BASE22 = 21.3f, BASE96 = 82.4f, BASE40_11 = 36.3f, BASE40_N = 38.7f, BASE22_13 = 22.2f;

struct Stack {   // a vertical flex column centred on [top, bot]
  float h[24], mt[24], y[24];
  int n = 0;
  int add(float height, float margin = 0) { h[n] = height; mt[n] = margin; return n++; }
  float total() const { float t = 0; for (int i = 0; i < n; i++) t += mt[i] + h[i]; return t; }
  void go(float top, float bot) {
    float t = 0;
    for (int i = 0; i < n; i++) t += mt[i] + h[i];
    float c = top + ((bot - top) - t) / 2;
    for (int i = 0; i < n; i++) { c += mt[i]; y[i] = c; c += h[i]; }
  }
};

struct B {
  Scene &s;
  char oldtxt[32] = "";   // text of the old-data pill for the screen being built (set by make_scene)
  explicit B(Scene &sc_) : s(sc_) {}
  Item *add(uint8_t k) {
    if (s.n >= MAXI) return &s.it[MAXI - 1];
    Item &i = s.it[s.n++];
    i = Item();
    i.k = k;
    return &i;
  }
  int group(int cx, int gap) {
    if (s.ng >= MAXG) return MAXG;
    Group &g = s.g[s.ng++];
    g = Group();
    g.cx = cx; g.gap = gap;
    return s.ng;   // index + 1
  }
  Item *text(const char *str, Font f, uint32_t col, float x, float base, int al = 0, int w = 0, int ls = 0, int grp = 0) {
    Item *i = add(K_TEXT);
    i->font = f; i->col = col; i->x = (int16_t) lroundf(x); i->y = (int16_t) lroundf(base); i->al = al; i->w = w; i->ls = ls;
    i->grp = grp;
    cp(i->s, sizeof i->s, str);
    return i;
  }
  Item *icon(uint8_t ic, int size, uint32_t col, float x, float y, int grp = 0) {
    Item *i = add(K_ICON);
    i->icon = ic; i->w = size; i->col = col; i->x = (int16_t) lroundf(x); i->y = (int16_t) lroundf(y); i->grp = grp;
    return i;
  }
  Item *rect(float x, float y, float w, float h, int r, uint32_t col) {
    Item *i = add(K_RECT);
    i->x = (int16_t) lroundf(x); i->y = (int16_t) lroundf(y); i->w = (int16_t) lroundf(w); i->h = (int16_t) lroundf(h);
    i->r = r; i->col = col;
    return i;
  }
  void bar(float x, float y, float w, float h, int pct, uint32_t fill, uint32_t track = C_TRACK) {
    rect(x, y, w, h, (int) (h / 2), track);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int fw = (int) lroundf(w * pct / 100.0f);
    if (fw > 0) rect(x, y, fw < h ? h : fw, h, (int) (h / 2), fill);
  }
  Item *arc(int a0, int a1, int dia, int stroke, uint32_t col, bool round) {
    Item *i = add(K_ARC);
    i->a0 = (int16_t) a0; i->a1 = (int16_t) a1; i->w = (int16_t) dia; i->h = (int16_t) stroke; i->col = col; i->rnd = round;
    return i;
  }
};

inline void cut(char *dst, size_t n, const char *src, size_t maxlen) {   // copy, shortening to maxlen chars with ".."
  size_t l = strlen(src);
  if (l <= maxlen) { cp(dst, n, src); return; }
  snprintf(dst, n, "%.*s..", (int) maxlen - 2, src);
}

inline void dur_hm(char *b, size_t n, int sec) {   // "1 h 48 min", "12 min", "2 d 3 h" (never a bare "m")
  if (sec < 0) { snprintf(b, n, "--"); return; }
  int m = (sec + 30) / 60;
  if (m < 1) snprintf(b, n, "under 1 min");
  else if (m < 60) snprintf(b, n, "%d min", m);
  else if (m < 48 * 60) { if (m % 60) snprintf(b, n, "%d h %d min", m / 60, m % 60); else snprintf(b, n, "%d h", m / 60); }
  else snprintf(b, n, "%d d %d h", m / 1440, (m / 60) % 24);
}
// An age as the design writes it: "12 MIN", "2 H 14 MIN", "3 D". Never a bare "M". Seconds under a minute read "1 MIN".
inline void age_text(char *b, size_t n, int sec) {
  if (sec < 0) { snprintf(b, n, "—"); return; }
  int m = (sec + 30) / 60;
  if (m < 1) m = 1;
  if (m < 60) snprintf(b, n, "%d MIN", m);
  else if (m < 48 * 60) { if (m % 60) snprintf(b, n, "%d H %d MIN", m / 60, m % 60); else snprintf(b, n, "%d H", m / 60); }
  else snprintf(b, n, "%d D", m / 1440);
}
inline void dur_short(char *b, size_t n, int sec) {   // "2 h", "1 d", "5 min"
  if (sec < 0) { snprintf(b, n, "--"); return; }
  if (sec < 3600) snprintf(b, n, "%d min", sec / 60 < 1 ? 1 : sec / 60);
  else if (sec < 86400) snprintf(b, n, "%d h", sec / 3600);
  else snprintf(b, n, "%d d", sec / 86400);
}
inline void mbps(char *b, size_t n, float v) {
  if (v < 0) snprintf(b, n, "—");
  else if (v >= 999.5f) snprintf(b, n, "%.1f G", v / 1000);   // 1000 and up reads as Gbps, one decimal
  else if (v < 9.95f) snprintf(b, n, "%.1f", v);
  else snprintf(b, n, "%.0f", v);
}
// Rough width of text in px (Montserrat averages), to decide wrapping and columns before drawing.
inline float txtw(const char *t, Font f, int ls = 0) {
  static const float per[] = {52.0f, 23.0f, 12.6f, 9.6f};
  return (float) strlen(t) * (per[f] + ls);
}
// Splits `t` into at most `max` lines of whole words that fit `width`. A third line is never started: the last line ends
// on a whole word and "…". Returns the line count; lines are written to out[i].
inline int wrap_words(const char *t, Font f, float width, int max, char out[][44]) {
  int nl = 0;
  char cur[44] = "";
  const char *p = t;
  bool cut = false;
  while (*p && !cut) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *q = p;
    while (*q && *q != ' ') q++;
    char w[44];
    size_t wl = (size_t) (q - p) < sizeof w - 1 ? (size_t) (q - p) : sizeof w - 1;
    memcpy(w, p, wl); w[wl] = 0;
    {   // a "·" separator stays on the line of the word before it, so a line never starts with a dot
      const char *p2 = q;
      while (*p2 == ' ') p2++;
      if ((unsigned char) p2[0] == 0xC2 && (unsigned char) p2[1] == 0xB7 && (p2[2] == ' ' || !p2[2]) && wl + 3 < sizeof w) { memcpy(w + wl, " \xC2\xB7", 4); w[wl + 3] = 0; wl += 3; q = p2 + 2; }
    }
    char tryl[88];
    snprintf(tryl, sizeof tryl, "%s%s%s", cur, cur[0] ? " " : "", w);
    if (cur[0] && txtw(tryl, f) > width) {
      if (nl + 1 >= max) {   // out of lines: end this one on a whole word and "…"
        cut = true;
        while (cur[0] && txtw(cur, f) + txtw("…", f) > width) { char *sp = strrchr(cur, ' '); if (!sp) break; *sp = 0; }
        size_t l = strlen(cur);
        if (l + 4 < sizeof cur) memcpy(cur + l, "…", 4);
        break;
      }
      cp(out[nl++], 44, cur);
      cur[0] = 0;
      snprintf(tryl, sizeof tryl, "%s", w);
    }
    cp(cur, sizeof cur, tryl);
    p = q;
  }
  if (cur[0] || nl == 0) cp(out[nl++], 44, cur);
  return nl;
}
inline void data_gb(char *b, size_t n, float g, bool have) {
  if (!have) snprintf(b, n, "—");
  else if (g <= 0) snprintf(b, n, "0 GB");
  else if (g < 1) snprintf(b, n, "%.1f GB", g);
  else if (g < 1000) snprintf(b, n, "%.0f GB", g);
  else snprintf(b, n, "%.2f TB", g / 1000);
}
inline uint32_t pct_col(int p, int sev) { return sev >= 2 ? C_BAD : (sev == 1 || p > 90) ? C_WARN : C_BLUE; }

}  // namespace sc

// ---------------------------------------------------------------------------------------------------------------- topics
inline const char *topic_name(int t) {
  static const char *n[] = {"DARKBLOOM", "NETWORK", "STORAGE", "EV", "CLAUDE", "LAB", "CLOCK"};
  return t >= 0 && t < T_COUNT ? n[t] : "";
}
inline uint8_t topic_icon(int t) {
  static const uint8_t ic[] = {IC_CHIP, IC_NET, IC_DISK, IC_BOLT, IC_GAUGE, IC_FLASK, IC_NONE};
  return t >= 0 && t < T_COUNT ? ic[t] : 0;
}

inline bool link_up(const Feed &f, uint32_t now) { return f.valid && (uint32_t) (now - f.at_ms) <= STALE_MS; }

enum { S_OK = 0, S_OFFLINE, S_NODATA, S_OLD };
// Data is shown as old (grey values, "UPDATED n MIN AGO") only once it is 10 minutes old; the feed says how old in "sa".
constexpr int OLD_AFTER_S = 600;
inline bool old_now(bool st, int sa, const Feed &f, uint32_t now) {
  if (!st) return false;
  if (sa < 0) return true;   // an older server that does not say how old: treat as old
  return sa + (int) ((uint32_t) (now - f.at_ms) / 1000) >= OLD_AFTER_S;
}
inline int topic_age(const Feed &f, int t, uint32_t now) {
  int sa = t == T_BL ? f.bl.sa : t == T_NW ? f.nw.sa : t == T_SD ? f.sd.sa : t == T_EV ? f.ev.sa : t == T_US ? f.us.sa : -1;
  return sa < 0 ? -1 : sa + (int) ((uint32_t) (now - f.at_ms) / 1000);
}
inline int topic_state(const Feed &f, int t, uint32_t now) {
  if (t == T_CLOCK) return S_OK;
  if (!link_up(f, now)) return S_OFFLINE;
  switch (t) {
    case T_BL: return !f.bl.ok ? S_NODATA : old_now(f.bl.st, f.bl.sa, f, now) ? S_OLD : S_OK;
    case T_NW: return !f.nw.ok ? S_NODATA : old_now(f.nw.st, f.nw.sa, f, now) ? S_OLD : S_OK;
    case T_SD: return !f.sd.ok ? S_NODATA : old_now(f.sd.st, f.sd.sa, f, now) ? S_OLD : S_OK;
    case T_EV: return !f.ev.ok ? S_NODATA : old_now(f.ev.st, f.ev.sa, f, now) ? S_OLD : S_OK;
    case T_US: return !f.us.ok ? S_NODATA : old_now(f.us.st, f.us.sa, f, now) ? S_OLD : S_OK;
    case T_LAB: return !f.lb.ok ? S_NODATA : f.lb.st ? S_OLD : S_OK;
  }
  return S_OK;
}

// 2 red, 1 amber, 0 none for the alerts of a topic ("bl", "nw", ...).
inline int topic_sev(const Feed &f, const char *tp) {
  int s = 0;
  for (int i = 0; i < f.nal; i++)
    if (strcmp(f.al[i].tp, tp) == 0) { int v = f.al[i].sv == 'r' ? 2 : 1; if (v > s) s = v; }
  return s;
}
inline int key_sev(const Feed &f, const char *prefix) {
  int s = 0;
  for (int i = 0; i < f.nal; i++)
    if (strncmp(f.al[i].k, prefix, strlen(prefix)) == 0) { int v = f.al[i].sv == 'r' ? 2 : 1; if (v > s) s = v; }
  return s;
}

// ---------------------------------------------------------------------------------------------------------------- chrome
namespace sc {

inline void dots(B &b, int n, int cur, uint32_t curcol, float y = 404) {
  if (n < 1) return;
  float total = (n - 1) * 10 + 22 + (n - 1) * 8;
  float x = 240 - total / 2;
  for (int i = 0; i < n; i++) {
    bool c = i == cur;
    b.rect(x, y, c ? 22 : 10, 10, 5, c ? curcol : C_IDLE);
    x += (c ? 22 : 10) + 8;
  }
}
// Alert dots: one per visible alert and a hollow one for the Done card, 7 at most. More than that: no dots (the "1 OF n" counter stays).
inline void alert_dots(B &b, int n, int cur, uint32_t col, float y = 404) {
  if (n < 1 || n > 6) return;   // at most 7 dots (the Done card is one); beyond that the "n OF m" counter stands alone
  int total = n + 1;                       // cur == n is the Done card
  float w = (total - 1) * 10 + 22 + (total - 1) * 8;
  float x = 240 - w / 2;
  for (int i = 0; i < total; i++) {
    bool c = i == cur, done = i == n;
    Item *r = b.rect(x, y, c ? 22 : 10, 10, 5, done ? 0x000000 : (c ? col : C_IDLE));
    if (done) { r->bw = 2; r->bg = c ? col : C_IDLE; }
    x += (c ? 22 : 10) + 8;
  }
}
inline void hint(B &b, const char *t, float y = 372) { b.text(t, F15, C_LABEL, 240, y + BASE15, 0, 380); }

// Alert marker: pill with "!" and the count, top centre. Colour of the worst alert. `y` 52, or 56 on arc screens.
inline void marker(B &b, int count, char worst, float y) {
  if (count <= 0) return;
  uint32_t col = worst == 'r' ? C_BAD : C_WARN;
  int g = b.group(240, 2);
  if (g > MAXG) return;
  Group &gr = b.s.g[g - 1];
  gr.pill = true; gr.py = (int16_t) y; gr.ph = 30; gr.padl = 8; gr.padr = 12; gr.pr = 15; gr.pbg = col;
  b.icon(IC_BANG, 18, C_BG, 0, y + 6, g);
  char t[8];
  snprintf(t, sizeof t, "%d", count > 99 ? 99 : count);
  b.text(t, F15, C_BG, 0, y + 15 + 5, 1, 0, 0, g);
}

// Row "icon + label" (topic header), centred, top of the 18 px box at `top`.
inline void header(B &b, uint8_t ic, const char *label, uint32_t col, float top) {
  int g = b.group(240, 8);
  if (ic) b.icon(ic, 18, col, 0, top, g);
  b.text(label, F15, col, 0, top + BASE15, 1, 0, 2, g);
}

// Pill with icon + text (22 px), 35 px high, centred on x=240 with its top at `top`.
inline void pill(B &b, uint8_t ic, const char *text, uint32_t fg, uint32_t bg, float top) {
  int g = b.group(240, 8);
  if (g > MAXG) return;
  Group &gr = b.s.g[g - 1];
  gr.pill = true; gr.py = (int16_t) top; gr.ph = 35; gr.padl = 16; gr.padr = 16; gr.pr = 17; gr.pbg = bg;
  if (ic) b.icon(ic, 18, fg, 0, top + 8.5f, g);
  b.text(text, F22, fg, 0, top + 4 + BASE22, 1, 0, 0, g);
}

// Old-data pill ("18 MIN OLD"), 26 px high.
inline void oldpill(B &b, const char *text, float top) {
  if (b.oldtxt[0]) text = b.oldtxt;
  int g = b.group(240, 6);
  if (g > MAXG) return;
  Group &gr = b.s.g[g - 1];
  gr.pill = true; gr.py = (int16_t) top; gr.ph = 26; gr.padl = 12; gr.padr = 12; gr.pr = 13; gr.pbg = C_TRACK;
  b.icon(IC_HISTORY, 16, C_WHITE, 0, top + 5, g);
  b.text(text, F15, C_WHITE, 0, top + 4 + BASE15, 1, 0, 2, g);
}

// 96 px value (+ unit at 40 px), baseline-aligned, top of the 96 px box at `top`.
inline void hero(B &b, float top, const char *val, const char *unit, uint32_t col, int ls = -2, int cx = 240) {
  int g = b.group(cx, 2);
  b.text(val, F96, col, 0, top + BASE96, 1, 0, ls, g);
  if (unit && *unit) b.text(unit, F40, col, 0, top + BASE96, 1, 0, 0, g);
}

// 280 degree hero arc, open at the bottom: r 200, 14 px.
inline void hero_arc(B &b, int pct, uint32_t fill) {
  if (pct < 0) return;   // no value: no track either, the screen shows "—" alone
  b.arc(130, 130 + 280, 414, 14, C_TRACK, true);
  if (pct > 100) pct = 100;
  if (pct > 0) b.arc(130, 130 + (int) lroundf(2.8f * pct), 414, 14, fill, true);
}

}  // namespace sc

// ---------------------------------------------------------------------------------------------------------------- screens
namespace sc {

constexpr int TOP = 96, BOT = 368;

// Old-data colour rules: values grey, arcs and bars #3A3F47.
struct Look { bool old; uint32_t val, fill; };
inline Look look(bool old, uint32_t fill) { return old ? Look{true, C_LABEL, C_IDLE} : Look{false, C_WHITE, fill}; }

inline void offline_screen(B &b, const Feed &f, int topic, uint32_t now) {
  Stack s;
  int a = s.add(18);
  int ic = s.add(56, 18);
  int t = s.add(44, 12);
  int u = s.add(26.8f, 6);
  int r = s.add(18, 16);
  s.go(TOP, BOT);
  header(b, 0, topic_name(topic), C_LABEL, s.y[a]);
  b.icon(IC_CLOUDOFF, 56, C_WHITE, 240 - 28, s.y[ic]);
  b.text("No connection", F40, C_WHITE, 240, s.y[t] + BASE40_11, 0, 380);
  b.text("Can't reach the dashboard", F22, C_LABEL, 240, s.y[u] + BASE22, 0, 380);
  char tx[48];
  if (f.valid) {
    char d[16];
    dur_hm(d, sizeof d, (int) ((uint32_t) (now - f.at_ms) / 1000));
    for (char *p = d; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    snprintf(tx, sizeof tx, "RETRYING · LAST DATA %s AGO", d);
  } else snprintf(tx, sizeof tx, "RETRYING");
  b.text(tx, F15, C_LABEL, 240, s.y[r] + BASE15, 0, 380, 2);
}

inline void nodata_screen(B &b, int topic) {
  static const char *who[] = {"DARKBLOOM", "THE ROUTER", "UNRAID", "THE CAR", "ANTHROPIC USAGE", "THE LAB", ""};
  Stack s;
  int a = s.add(18);
  int h = s.add(96, 8);
  int t = s.add(26.8f, 8);
  int w = s.add(18, 10);
  s.go(TOP, BOT);
  header(b, topic_icon(topic), topic_name(topic), C_BLUE, s.y[a]);
  b.text("—", F96, C_IDLE, 240, s.y[h] + BASE96, 0, 380);
  b.text("No data yet", F22, C_WHITE, 240, s.y[t] + BASE22, 0, 380);
  char tx[40];
  snprintf(tx, sizeof tx, "%s HASN'T REPORTED", who[topic]);
  b.text(tx, F15, C_LABEL, 240, s.y[w] + BASE15, 0, 380, 2);
}

// Darkbloom overview. Mac names sit centred in two columns and wrap onto a second line, never cut.
inline void bl_overview(B &b, const Feed &f, bool old) {
  const auto &m = f.bl;
  int td = -1, tm = -1, serving = 0;
  for (int i = 0; i < m.n; i++) {
    if (m.m[i].td >= 0) td = (td < 0 ? 0 : td) + m.m[i].td;
    if (m.m[i].tm >= 0) tm = (tm < 0 ? 0 : tm) + m.m[i].tm;
    if (m.m[i].on && strcmp(m.m[i].s, "serving") == 0) serving++;
  }
  Look lk = look(old, C_BLUE);
  char ln[MAX_MACS][2][44];
  int nl[MAX_MACS];
  for (int i = 0; i < m.n; i++) nl[i] = wrap_words(m.m[i].n, F22, 150, 2, ln[i]);
  int rows = (m.n + 1) / 2;
  float rh[2] = {0, 0}, gh = 0;
  for (int r = 0; r < rows && r < 2; r++) {
    int mx = nl[r * 2];
    if (r * 2 + 1 < m.n && nl[r * 2 + 1] > mx) mx = nl[r * 2 + 1];
    rh[r] = mx * 26.8f;
    gh += rh[r] + (r ? 4 : 0);
  }
  bool showlab = !(old && rows > 1);   // room: with the old-data pill and two rows the "n of m serving" line goes
  Stack s;
  int hd = s.add(18);
  int pl = old ? s.add(26, 8) : -1;
  int hv = s.add(96, old ? 4 : 8);
  int sub = s.add(26.8f, old ? 2 : 6);
  int lab = showlab ? s.add(18, old ? 10 : 16) : -1;
  int grid = rows ? s.add(gh, showlab ? 8 : 12) : -1;
  s.go(TOP, BOT);
  header(b, IC_CHIP, "DARKBLOOM", C_BLUE, s.y[hd]);
  if (old) oldpill(b, "OLD DATA", s.y[pl]);
  char v[16], line[40], a[16];
  if (td < 0) snprintf(v, sizeof v, "—"); else snprintf(v, sizeof v, "$%d.%02d", td / 100, td % 100);
  hero(b, s.y[hv], v, "", td < 0 ? C_IDLE : lk.val, -2);
  if (tm >= 0) { snprintf(a, sizeof a, "$%d.%02d", tm / 100, tm % 100); snprintf(line, sizeof line, "today · %s this month", a); }
  else snprintf(line, sizeof line, "today · — this month");
  b.text(line, F22, C_LABEL, 240, s.y[sub] + BASE22, 0, 380);
  if (lab >= 0) {
    if (m.md[0]) snprintf(line, sizeof line, "SERVING %s", m.md);
    else snprintf(line, sizeof line, "%d OF %d SERVING", serving, m.n);
    for (char *p = line; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    b.text(line, F15, C_LABEL, 240, s.y[lab] + BASE15, 0, 380, 2);
  }
  for (int i = 0; i < m.n && grid >= 0 && i < 4; i++) {
    const Mac &k = m.m[i];
    bool sv = k.on && strcmp(k.s, "serving") == 0;
    uint32_t dot = (!k.on || k.sl) ? C_BAD : sv ? C_BLUE : C_LABEL;
    uint32_t tc = sv ? C_WHITE : C_LABEL;
    if (old) { dot = C_IDLE; tc = C_LABEL; }
    int r = i / 2;
    int cnt = (r == rows - 1 && (m.n & 1)) ? 1 : 2;
    float cx = cnt == 1 ? 240 : 240 + ((i & 1) ? 1 : -1) * 90;
    float y = s.y[grid] + (r ? rh[0] + 4 : 0);
    int g = b.group((int) cx, 8);
    b.rect(0, y + 8.9f, 10, 10, 5, dot)->grp = (uint8_t) g;
    b.text(ln[i][0], F22, tc, 0, y + BASE22, 1, 0, 0, g);
    if (nl[i] > 1) b.text(ln[i][1], F22, tc, cx, y + 26.8f + BASE22, 0, 170);
  }
}

// Network overview. Up to 3 links: one column per link. 4 or 5: one row per link. 6 or more: the 4 busiest rows and
// "+N MORE" (the real count). Fonts stay the fixed set; nothing shrinks or scrolls.
inline float traffic(const Link &l) { return (l.d > 0 ? l.d : 0) + (l.u > 0 ? l.u : 0); }
inline void nw_list(B &b, const Feed &f, bool old) {
  const auto &w = f.nw;
  Look lk = look(old, C_BLUE);
  int idx[MAX_LINKS], k = 0;
  for (int i = 0; i < w.n; i++) idx[k++] = i;
  int more = 0;
  if (w.n > 5) {
    for (int i = 1; i < k; i++)   // busiest first (stable insertion sort)
      for (int j = i; j > 0 && traffic(w.l[idx[j]]) > traffic(w.l[idx[j - 1]]); j--) { int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
    more = w.n - 4;
    k = 4;
  }
  Stack s;
  int hd = s.add(18);
  int pl = old ? s.add(26, 10) : -1;
  int rows = s.add(k * 26.8f + (k - 1) * 6, old ? 10 : 12);
  int mo = more ? s.add(18, 6) : -1;
  int ft = s.add(18, old ? 8 : 8);
  s.go(TOP, BOT);
  header(b, IC_NET, "NETWORK", C_BLUE, s.y[hd]);
  if (old) oldpill(b, "OLD DATA", s.y[pl]);
  for (int i = 0; i < k; i++) {
    const Link &l = w.l[idx[i]];
    float y = s.y[rows] + i * (26.8f + 6);
    b.text(l.n, F22, lk.val, 82, y + BASE22, 1, 130);
    char d[12], u[12];
    mbps(d, sizeof d, l.d);
    mbps(u, sizeof u, l.u);
    b.icon(IC_DOWN, 16, old ? C_IDLE : C_BLUE, 220, y + 5.4f);
    b.text(d, F22, lk.val, 242, y + BASE22, 1, 80);
    b.icon(IC_UP, 16, C_LABEL, 330, y + 5.4f);
    b.text(u, F22, C_LABEL, 352, y + BASE22, 1, 70);
  }
  if (more) {
    char t[16];
    snprintf(t, sizeof t, "+%d MORE", more);
    b.text(t, F15, C_LABEL, 240, s.y[mo] + BASE15, 0, 380, 2);
  }
  b.text("MBPS · LIVE", F15, C_LABEL, 240, s.y[ft] + BASE15, 0, 380, 2);
}

inline void nw_overview(B &b, const Feed &f, bool old) {
  if (f.nw.n > 3) { nw_list(b, f, old); return; }
  const auto &w = f.nw;
  Look lk = look(old, C_BLUE);
  Stack s;
  int hd = s.add(18);
  int pl = old ? s.add(26, 10) : -1;
  int cols = s.add(18 + 8 + 40 + 8 + 27, old ? 14 : 26);
  int ft = s.add(18, old ? 16 : 22);
  s.go(TOP, BOT);
  header(b, IC_NET, "NETWORK", C_BLUE, s.y[hd]);
  if (old) oldpill(b, "OLD DATA", s.y[pl]);
  int n = w.n;
  float cw = (340 - 20) / 3.0f;
  float left = 240 - (n * cw + (n - 1) * 10) / 2;
  for (int i = 0; i < n; i++) {
    const Link &l = w.l[i];
    float cx = left + i * (cw + 10) + cw / 2;
    float y = s.y[cols];
    char nm[14];
    snprintf(nm, sizeof nm, "%s", l.n);
    for (char *p = nm; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    b.text(nm, F15, C_LABEL, cx, y + BASE15, 0, (int) cw, 2);
    char d[12], u[12];
    mbps(d, sizeof d, l.d);
    mbps(u, sizeof u, l.u);
    int g1 = b.group((int) lroundf(cx), 2);
    b.icon(IC_DOWN, 20, old ? C_IDLE : C_BLUE, 0, y + 26 + 10, g1);
    b.text(d, F40, lk.val, 0, y + 26 + 10 + 34.3f + 0.0f, 1, 0, 0, g1);
    int g2 = b.group((int) lroundf(cx), 2);
    b.icon(IC_UP, 16, C_LABEL, 0, y + 26 + 10 + 40 + 8 + 5.5f, g2);
    b.text(u, F22, lk.val, 0, y + 26 + 10 + 40 + 8 + BASE22, 1, 0, 0, g2);
  }
  b.text("MBPS · LIVE", F15, C_LABEL, 240, s.y[ft] + BASE15, 0, 380, 2);
}

// Storage overview
inline void sd_overview(B &b, const Feed &f, bool old) {
  const auto &d = f.sd;
  int pct = d.ts > 0 ? (int) lroundf(100.0f * d.tu / d.ts) : -1;
  int sev = topic_sev(f, "sd");
  bool failing = d.pe > 0 || (d.arr[0] && strcasecmp(d.arr, "STARTED") != 0);
  Look lk = look(old, pct_col(pct, failing ? 2 : 0));
  hero_arc(b, pct, lk.fill);
  if (old && pct >= 0) { b.s.it[b.s.n - 1].col = C_IDLE; }
  // worst disk
  const Disk *worst = nullptr;
  for (int i = 0; i < d.n; i++) if (d.d[i].u >= 90 && (!worst || d.d[i].u > worst->u) && d.d[i].r != 'p') worst = &d.d[i];
  Stack s;
  int hd = s.add(18);
  int pl = old ? s.add(26, 10) : -1;
  int hv = s.add(96, old ? 6 : 8);
  int sub = s.add(26.8f, 4);
  int pillr = old ? -1 : s.add(35, 16);
  int ft = s.add(18, old ? 12 : 12);
  s.go(TOP, BOT);
  header(b, IC_DISK, "STORAGE", C_BLUE, s.y[hd]);
  if (old) oldpill(b, "OLD DATA", s.y[pl]);
  char v[8], line[48];
  if (pct < 0) snprintf(v, sizeof v, "—"); else snprintf(v, sizeof v, "%d", pct);
  hero(b, s.y[hv], v, pct < 0 ? "" : "%", pct < 0 ? C_IDLE : lk.val, -2);
  if (d.ts > 0) snprintf(line, sizeof line, "%.1f of %.1f TB", d.tu / 1000.0, d.ts / 1000.0);
  else snprintf(line, sizeof line, "— of — TB");
  b.text(line, F22, C_LABEL, 240, s.y[sub] + BASE22, 0, 380);
  if (pillr >= 0) {
    if (failing) {
      if (d.pe > 0) snprintf(line, sizeof line, "Parity: %d error%s", d.pe, d.pe == 1 ? "" : "s");
      else snprintf(line, sizeof line, "Array %s", d.arr);
      pill(b, IC_WARN, line, C_BAD, P_GREY, s.y[pillr]);
    } else if (worst) {
      snprintf(line, sizeof line, "%s %d%% full", worst->n, worst->u);
      pill(b, IC_WARN, line, C_WARN, P_AMBER, s.y[pillr]);
    } else if (sev > 0) pill(b, IC_WARN, "Needs attention", C_WARN, P_AMBER, s.y[pillr]);
    else pill(b, IC_CHECK, "All disks OK", C_OK, P_GREEN, s.y[pillr]);
  }
  if (d.par) snprintf(line, sizeof line, "%d DISKS · PARITY CHECK %d%%", d.n, d.pp);
  else snprintf(line, sizeof line, "%d DISKS", d.n);
  b.text(line, F15, C_LABEL, 240, s.y[ft] + BASE15, 0, 380, 2);
}

// EV overview
inline void ev_overview(B &b, const Feed &f, bool old) {
  const auto &e = f.ev;
  int sev = topic_sev(f, "ev");
  Look lk = look(old, pct_col(e.soc, sev >= 2 ? 2 : 0));
  hero_arc(b, e.soc, lk.fill);
  if (!old && e.lim > 0 && e.soc >= 0) {   // charge limit tick
    Item *tk = b.add(K_ARC);   // rnd 2 = limit tick: a 4 px by 26 px line across the arc at angle a0
    tk->rnd = 2; tk->a0 = (int16_t) (130 + (int) lroundf(2.8f * e.lim)); tk->col = C_WHITE;
  }
  Stack s;
  int hd = s.add(18);
  int pl = old ? s.add(26, 10) : -1;
  int hv = s.add(96, old ? 6 : 8);
  int sub = s.add(26.8f, 4);
  int pillr = old ? -1 : s.add(35, 18);
  s.go(TOP, BOT);
  header(b, IC_BOLT, "EV", C_BLUE, s.y[hd]);
  if (old) oldpill(b, "OLD DATA", s.y[pl]);
  char v[8], line[40];
  if (e.soc < 0) snprintf(v, sizeof v, "—"); else snprintf(v, sizeof v, "%d", e.soc);
  hero(b, s.y[hv], v, e.soc < 0 ? "" : "%", e.soc < 0 ? C_IDLE : lk.val, -2);
  if (e.lim > 0) snprintf(line, sizeof line, "limit %d%%", e.lim); else snprintf(line, sizeof line, "limit —");
  b.text(line, F22, C_LABEL, 240, s.y[sub] + BASE22, 0, 380);
  if (pillr >= 0) {
    bool charging = e.ch == 1;
    if (e.up) {   // the car's software update, shown inside the EV page (never a full-screen takeover)
      if (e.upp >= 0) snprintf(line, sizeof line, "Updating %d%%", e.upp); else snprintf(line, sizeof line, "Updating");
      pill(b, IC_DOWNLOAD, line, C_BLUE, P_BLUE, s.y[pillr]);
    } else if (e.plug == 0) pill(b, IC_PLUG, "Unplugged", C_WHITE, P_GREY, s.y[pillr]);
    else if (charging) pill(b, IC_BOLT, "Charging", C_BLUE, P_BLUE, s.y[pillr]);
    else if (e.plug == 1 && e.soc >= 0 && e.lim > 0 && e.soc >= e.lim) pill(b, IC_CHECK, "At limit", C_OK, P_GREEN, s.y[pillr]);
    else if (e.plug == 1) pill(b, IC_PLUG, "Plugged in", C_WHITE, P_GREY, s.y[pillr]);
    else pill(b, IC_PLUG, "—", C_LABEL, P_GREY, s.y[pillr]);
  }
}

inline void when_text(char *b, size_t n, const Clock &c, int secs_ahead, bool week_style) {
  if (!c.valid || secs_ahead < 0) { snprintf(b, n, "—"); return; }
  time_t t = c.t + secs_ahead;
  struct tm tmv;
  localtime_r(&t, &tmv);
  static const char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int h12 = tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12;
  (void) week_style;
  snprintf(b, n, "resets %s %d:%02d %s", wd[tmv.tm_wday], h12, tmv.tm_min, tmv.tm_hour < 12 ? "AM" : "PM");
}

// Claude overview: 5 hour arc plus a week bar
inline void us_overview(B &b, const Feed &f, const Clock &ck, bool old) {
  const auto &u = f.us;
  int p = u.h5.has ? u.h5.p : -1;
  int sev = key_sev(f, "usage:5h");
  Look lk = look(old, pct_col(p, sev));
  hero_arc(b, p, lk.fill);
  Stack s;
  int hd = s.add(18);
  int pl = old ? s.add(26, 10) : -1;
  int hv = s.add(96, old ? 6 : 8);
  int sub = s.add(26.8f, 4);
  int wk = s.add(18 + 6 + 8 + 6 + 18, 20);
  s.go(TOP, BOT);
  header(b, IC_GAUGE, "CLAUDE · 5 HOUR", C_BLUE, s.y[hd]);
  if (old) oldpill(b, "OLD DATA", s.y[pl]);
  char v[8], line[40], t[16];
  if (p < 0) snprintf(v, sizeof v, "—"); else snprintf(v, sizeof v, "%d", p);
  hero(b, s.y[hv], v, p < 0 ? "" : "%", p < 0 ? C_IDLE : lk.val, -2);
  if (u.h5.has && u.h5.r >= 0) { dur_hm(t, sizeof t, u.h5.r); snprintf(line, sizeof line, "resets in %s", t); }
  else snprintf(line, sizeof line, "resets in —");
  b.text(line, F22, C_LABEL, 240, s.y[sub] + BASE22, 0, 380);
  float y = s.y[wk];
  b.text("WEEK", F15, C_LABEL, 130, y + BASE15, 1, 0, 2);
  if (u.wk.has) snprintf(line, sizeof line, "%d%%", u.wk.p); else snprintf(line, sizeof line, "—");
  b.text(line, F22, old ? C_LABEL : C_WHITE, 350, y + 0.5f + BASE22 - 3.5f, 2, 0);
  b.bar(130, y + 18 + 6 + 0.0f + 3, 220, 8, u.wk.has ? u.wk.p : 0, old ? C_IDLE : pct_col(u.wk.has ? u.wk.p : 0, key_sev(f, "usage:wk")));
  if (u.wk.has && u.wk.r >= 0) when_text(line, sizeof line, ck, u.wk.r, true); else snprintf(line, sizeof line, "resets —");
  b.text(line, F15, C_LABEL, 130, y + 18 + 6 + 8 + 6 + BASE15, 1, 220);
}

// Lab overview
inline void lb_overview(B &b, const Feed &f, bool old) {
  const auto &l = f.lb;
  Stack s;
  int hd = s.add(18);
  int pl = old ? s.add(26, 10) : -1;
  int hv = s.add(96, old ? 6 : 8);
  int sub = s.add(26.8f, 4);
  int nurg = 0, first_u = 0;
  for (int i = 0; i < l.n; i++) if (l.i[i].w) { if (!nurg) first_u = i; nurg++; }
  int pu = (nurg > 0 && !old) ? s.add(35, 10) : -1;
  int dv = s.add(2, pu >= 0 ? 12 : 20);
  int q = s.add(35, 12);
  s.go(TOP, BOT);
  header(b, IC_FLASK, "LAB", C_BLUE, s.y[hd]);
  if (old) {
    char t[24];
    if (l.age >= 0) { char d[16]; dur_hm(d, sizeof d, l.age); snprintf(t, sizeof t, "%s OLD", d); for (char *p = t; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; }
    else snprintf(t, sizeof t, "OLD DATA");
    oldpill(b, t, s.y[pl]);
  }
  char v[8];
  snprintf(v, sizeof v, "%d", l.n);
  hero(b, s.y[hv], v, "", old ? C_LABEL : C_WHITE, -2);
  b.text(l.n == 0 ? "nothing waiting" : "waiting on you", F22, C_LABEL, 240, s.y[sub] + BASE22, 0, 380);
  if (nurg > 0 && !old) {
    char u[44], tt[44];
    wrap_words(l.i[first_u].t, F22, nurg > 1 ? 150 : 270, 1, (char (*)[44]) tt);
    if (nurg > 1) snprintf(u, sizeof u, "%d urgent · %s", nurg, tt); else snprintf(u, sizeof u, "%s", tt);
    pill(b, IC_WARN, u, C_WARN, P_AMBER, s.y[pu]);
  }
  b.rect(140, s.y[dv], 200, 2, 0, C_TRACK);
  int g = b.group(240, 10);
  char qt[28];
  if (l.q >= 0 && l.r >= 0) snprintf(qt, sizeof qt, "%d jobs · %d running", l.q, l.r);
  else if (l.q >= 0) snprintf(qt, sizeof qt, "%d jobs", l.q);
  else snprintf(qt, sizeof qt, "—");
  b.text("AI QUEUE", F15, C_LABEL, 0, s.y[q] + 29.f, 1, 0, 2, g);
  b.text(qt, F22, l.q >= 0 ? (old ? C_LABEL : C_WHITE) : C_IDLE, 0, s.y[q] + 29.f, 1, 0, 0, g);
}

// Clock overview
inline const char *topic_short(int t) {
  static const char *n[] = {"BLOOM", "NET", "DISKS", "EV", "CLAUDE", "LAB"};
  return t < 6 ? n[t] : "";
}
inline void clock_overview(B &b, const Feed &f, const Clock &c, uint32_t now) {
  Stack s;
  int hv = s.add(96);
  int dt = s.add(26.8f, 6);
  int cells = 0, nc = 0, order[6];
  for (int t = 0; t < 6; t++) if (topic_on(f, t)) order[nc++] = t;
  int rows = (nc + 2) / 3;
  cells = s.add(rows * 18 + (rows - 1) * 12, 28);
  s.go(TOP, BOT);
  char v[12] = "—", ap[4] = "", line[40] = "Waiting for the time";
  if (c.valid) {
    struct tm tmv;
    time_t t = c.t;
    localtime_r(&t, &tmv);
    int h12 = tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12;
    snprintf(v, sizeof v, "%d:%02d", h12, tmv.tm_min);
    snprintf(ap, sizeof ap, "%s", tmv.tm_hour < 12 ? "AM" : "PM");
    static const char *wd[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(line, sizeof line, "%s, %s %d", wd[tmv.tm_wday], mo[tmv.tm_mon], tmv.tm_mday);
  }
  int g = b.group(240, 6);
  b.text(v, F96, c.valid ? C_WHITE : C_IDLE, 0, s.y[hv] + BASE96, 1, 0, -3, g);
  if (ap[0]) b.text(ap, F22, C_LABEL, 0, s.y[hv] + BASE96, 1, 0, 0, g);
  b.text(line, F22, C_LABEL, 240, s.y[dt] + BASE22, 0, 380);
  for (int i = 0; i < nc; i++) {
    int t = order[i], st = topic_state(f, t, now), sev = topic_sev(f, t == T_BL ? "bl" : t == T_NW ? "nw" : t == T_SD ? "sd" : t == T_EV ? "ev" : t == T_US ? "us" : "lb");
    uint32_t col = st != S_OK ? C_IDLE : sev >= 2 ? C_BAD : sev == 1 ? C_WARN : C_OK;
    uint32_t tc = (st == S_OK && sev >= 2) ? C_BAD : (st == S_OK && sev == 1) ? C_WARN : C_LABEL;
    float cw = (300 - 16) / 3.0f;
    int cnt = (i / 3 == rows - 1) ? nc - (rows - 1) * 3 : 3;
    float left = 240 - (cnt * cw + (cnt - 1) * 8) / 2;
    float cx = left + (i % 3) * (cw + 8) + cw / 2;
    float y = s.y[cells] + (i / 3) * 30;
    int gg = b.group((int) lroundf(cx), 6);
    b.rect(0, y + 4, 10, 10, 5, col)->grp = gg;
    b.text(topic_short(t), F15, tc, 0, y + BASE15, 1, 0, 1, gg);
  }
}

// ----- detail pages
// Darkbloom detail: Macs grouped by cluster (blue bar, up to 3 clusters on one screen). Earnings are TODAY, job counts NOW.
// At 3 clusters the "DARKBLOOM · MACS" title goes and the model row's label turns from grey "MODEL" to blue "DARKBLOOM".
// Rows fall back to one line each (no status line) when the full layout would not fit between y 96 and 368.
inline void bl_detail(B &b, const Feed &f) {
  const auto &m = f.bl;
  // clusters in order of first appearance; a Mac with no cluster joins "OTHER" only when other Macs have one
  char cn[4][12] = {"", "", "", ""};
  int ncl = 0, of[MAX_MACS];
  bool anycl = false;
  for (int i = 0; i < m.n; i++) if (m.m[i].cl[0]) anycl = true;
  for (int i = 0; i < m.n; i++) {
    of[i] = -1;
    if (!anycl) continue;
    const char *nm = m.m[i].cl[0] ? m.m[i].cl : "other";
    for (int c = 0; c < ncl; c++) if (strcmp(cn[c], nm) == 0) of[i] = c;
    if (of[i] < 0 && ncl < 3) { cp(cn[ncl], sizeof cn[ncl], nm); of[i] = ncl++; }
  }
  bool three = ncl >= 3;
  bool has_md = m.md[0] != 0;
  // order: by cluster, then feed order
  int ord[MAX_MACS], no = 0;
  if (anycl) { for (int c = 0; c < ncl; c++) for (int i = 0; i < m.n; i++) if (of[i] == c) ord[no++] = i; }
  else for (int i = 0; i < m.n; i++) ord[no++] = i;
  for (int pass = 0; pass < 2; pass++) {
    bool compact = pass == 1;
    float rowh = compact ? 26.8f : 47.0f, gap = 8;
    Stack s;
    int hd = three ? -1 : s.add(18);
    int md = (has_md || three) ? s.add(26.8f, three ? 0 : 6) : -1;
    int tl = s.add(18, 6);
    int cl_at[3] = {-1, -1, -1}, mac_at[MAX_MACS];
    for (int k = 0; k < no; k++) mac_at[k] = -1;
    int lastc = -2;
    for (int k = 0; k < no; k++) {
      int c = of[ord[k]];
      if (anycl && c != lastc) { cl_at[c] = s.add(18, lastc == -2 ? 2 : 10); lastc = c; }
      bool fresh = k == 0 || (anycl && of[ord[k - 1]] != c);
      mac_at[k] = s.add(rowh, fresh ? 2 : gap);
    }
    if (!compact && s.total() > BOT - TOP) continue;
    s.go(TOP, BOT);
    if (hd >= 0) header(b, 0, "DARKBLOOM · MACS", C_BLUE, s.y[hd]);
    if (md >= 0) {
      int g = b.group(240, 8);
      b.text(three ? "DARKBLOOM" : "MODEL", F15, three ? C_BLUE : C_LABEL, 0, s.y[md] + 5.5f + BASE15, 1, 0, 2, g);
      if (has_md) b.text(m.md, F22, C_WHITE, 0, s.y[md] + BASE22, 1, 0, 0, g);
    }
    b.text("TODAY", F15, C_LABEL, 395, s.y[tl] + BASE15, 2, 100, 2);
    float bar_top = 0;
    int cur = -2;
    for (int k = 0; k < no; k++) {
      const Mac &mk = m.m[ord[k]];
      int c = of[ord[k]];
      float x0 = anycl ? 108 : 103, dx = anycl ? 90 : 85;
      if (anycl && c != cur) {
        cur = c;
        // cluster label: NAME · n NOW
        int sum = -1;
        for (int q = 0; q < m.n; q++) if (of[q] == c && m.m[q].ij >= 0) sum = (sum < 0 ? 0 : sum) + m.m[q].ij;
        char lb[32], nm[12];
        snprintf(nm, sizeof nm, "%s", cn[c]);
        for (char *p = nm; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        if (sum >= 0) snprintf(lb, sizeof lb, "%s · %d NOW", nm, sum); else snprintf(lb, sizeof lb, "%s", nm);
        b.text(lb, F15, C_BLUE, 90, s.y[cl_at[c]] + BASE15, 1, 260, 2);
        bar_top = s.y[cl_at[c]];
        int last = k;
        while (last + 1 < no && of[ord[last + 1]] == c) last++;
        float bot = s.y[mac_at[last]] + rowh;
        b.rect(78, bar_top, 3, bot - bar_top, 1, C_BLUE);
      }
      float y = s.y[mac_at[k]];
      bool sv = mk.on && strcmp(mk.s, "serving") == 0;
      uint32_t dot = (!mk.on || mk.sl) ? C_BAD : sv ? C_BLUE : C_LABEL;
      b.rect(dx, y + 8.9f, 10, 10, 5, dot);
      b.text(mk.n, F22, C_WHITE, x0, y + BASE22, 1, 160);
      char e[16] = "—";
      if (mk.td >= 0) snprintf(e, sizeof e, "$%d.%02d", mk.td / 100, mk.td % 100);
      b.text(e, F22, mk.td >= 0 ? C_WHITE : C_IDLE, 395, y + BASE22, 2, 120);
      if (compact) continue;
      char st[44];
      if (!mk.on || mk.sl) { char d[16]; dur_short(d, sizeof d, mk.age); snprintf(st, sizeof st, "offline %s", mk.age >= 0 ? d : "—"); }
      else if (sv && (mk.ij >= 0 || mk.jobs >= 0)) {
        char a1[12] = "—", a2[12] = "—";
        if (mk.ij >= 0) snprintf(a1, sizeof a1, "%d", mk.ij);
        if (mk.jobs >= 0) snprintf(a2, sizeof a2, "%d", mk.jobs);
        snprintf(st, sizeof st, "inference %s · sandbox %s now", a1, a2);
      } else if (sv) snprintf(st, sizeof st, "serving");
      else if (mk.w >= 0) snprintf(st, sizeof st, "idle · %d W", mk.w);
      else snprintf(st, sizeof st, "idle");
      b.text(st, F15, C_LABEL, x0, y + 26.8f + 2 + BASE15, 1, 280);
    }
    return;
  }
}

// Data used by one link in one period (GB down + up; 0 when the period has no totals). Same idea as the overview's
// traffic() (down + up), but for the period's totals instead of the live speed.
inline float used_in(const Link &l, int p) { return l.has_t[p] ? (l.t[p][0] > 0 ? l.t[p][0] : 0) + (l.t[p][1] > 0 ? l.t[p][1] : 0) : 0; }

// Network detail. Links are sorted by data used in THIS period (most first, ties keep feed order). Up to 7 links show
// all 7 rows; 8 or more show the 6 biggest and "+N MORE" (the real count) in the 7th row's place.
inline void nw_detail(B &b, const Feed &f, int period) {
  const auto &w = f.nw;
  static const char *pn[] = {"Last 24 h", "Last 7 days", "Last 30 days"};
  static const char *pc[] = {"Today", "7 days", "30 days"};
  int idx[MAX_LINKS];
  for (int i = 0; i < w.n; i++) idx[i] = i;
  for (int i = 1; i < w.n; i++)
    for (int j = i; j > 0 && used_in(w.l[idx[j]], period) > used_in(w.l[idx[j - 1]], period); j--) { int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
  bool many = w.n >= 4;              // 4 or more links: the big title becomes a small label and the rows tighten
  int more = w.n > 7 ? w.n - 6 : 0;  // 7 rows is all that fit inside the circle
  int shown = more ? 6 : w.n;
  int n = more ? 7 : w.n;            // rows drawn, the "+N MORE" row included
  float gap = !many ? 10 : (n <= 5 ? 6 : 2);
  float head = many ? 22 : 28;       // column heads plus the space under them
  Stack s;
  int hd = s.add(18);
  int pe = s.add(many ? 18 : 48.8f, many ? 8 : 4);
  int grid = s.add(head + n * 26.8f + (n - 1) * gap, many ? 10 : 18 - 0);
  int total_at = -1;
  const Link *prim = nullptr;
  for (int i = 0; i < w.n; i++) if (!same_name(w.l[i].n, "unraid")) { prim = &w.l[i]; break; }
  if (prim && n <= 6) total_at = s.add(18, 16);
  s.go(TOP, BOT);
  header(b, 0, "NETWORK · DATA USED", C_LABEL, s.y[hd]);
  if (many) {
    char lab[16];
    snprintf(lab, sizeof lab, "%s", w.rolling ? pn[period] : pc[period]);
    for (char *p = lab; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    b.text(lab, F15, C_BLUE, 240, s.y[pe] + BASE15, 0, 380, 2);
  } else b.text(w.rolling ? pn[period] : pc[period], F40, C_BLUE, 240, s.y[pe] + BASE40_N, 0, 380);
  float y = s.y[grid];
  // the name column takes whatever the number columns leave, so a name is never cut
  float dw = 60;
  for (int i = 0; i < shown; i++) {
    const Link &l = w.l[idx[i]];
    char d[16];
    data_gb(d, sizeof d, l.t[period][0], l.has_t[period]);
    float x = txtw(d, F22);
    if (x > dw) dw = x;
  }
  float namew = 295 - dw - 14 - 77;
  if (namew < 108) namew = 108;
  b.text("DOWN", F15, C_LABEL, 295, y + BASE15, 2, 100, 2);
  b.text("UP", F15, C_LABEL, 403, y + BASE15, 2, 100, 2);
  for (int i = 0; i < shown; i++) {
    const Link &l = w.l[idx[i]];
    float ry = y + 18 + (many ? 4 : 10) + i * (26.8f + gap);
    char d[16], u[16];
    data_gb(d, sizeof d, l.t[period][0], l.has_t[period]);
    data_gb(u, sizeof u, l.t[period][1], l.has_t[period]);
    b.text(l.n, F22, C_WHITE, 77, ry + BASE22, 1, (int) namew);
    b.text(d, F22, C_WHITE, 295, ry + BASE22, 2, 100);
    b.text(u, F22, C_LABEL, 403, ry + BASE22, 2, 100);
  }
  if (more) {
    char t[16];
    snprintf(t, sizeof t, "+%d MORE", more);
    float ry = y + 18 + 4 + 6 * (26.8f + gap);
    b.text(t, F15, C_LABEL, 240, ry + (26.8f - 18.3f) / 2 + BASE15, 0, 380, 2);
  }
  if (total_at >= 0) {
    char t[32], nm[14], tot[16];
    snprintf(nm, sizeof nm, "%s", prim->n);
    for (char *p = nm; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    if (prim->has_t[period]) {
      float g = prim->t[period][0] + prim->t[period][1];
      if (g >= 1000) snprintf(tot, sizeof tot, "%.2f TB", g / 1000); else snprintf(tot, sizeof tot, "%.0f GB", g);
    } else snprintf(tot, sizeof tot, "—");
    snprintf(t, sizeof t, "%s TOTAL %s", nm, tot);
    b.text(t, F15, C_LABEL, 240, s.y[total_at] + BASE15, 0, 380, 2);
  }
}

inline void sd_detail(B &b, const Feed &f) {
  const auto &d = f.sd;
  int rows = (d.n + 3) / 4;
  Stack s;
  int hd = s.add(18);
  int grid = s.add(rows * 56 + (rows > 0 ? (rows - 1) * 6 : 0), 10);
  int ft = s.add(18, 10);
  s.go(92, 372);
  header(b, 0, "STORAGE · DISKS", C_BLUE, s.y[hd]);
  float tw = (316 - 18) / 4.0f;
  for (int i = 0; i < d.n; i++) {
    const Disk &k = d.d[i];
    float x = 82 + (i % 4) * (tw + 6), y = s.y[grid] + (i / 4) * 62;
    bool has = k.u >= 0, warn = has && k.u >= 90;
    uint32_t edge = C_TILE;
    Item *t = b.rect(x, y, tw, 56, 8, C_TILE);
    t->bw = 2; t->bg = edge;
    char nm[12], pc[8];
    sc::cut(nm, sizeof nm, k.n, 7);
    if (has) snprintf(pc, sizeof pc, "%d%%", k.u); else snprintf(pc, sizeof pc, "—");
    b.text(nm, F15, warn ? C_WARN : C_LABEL, x + 8, y + 5 + 14.5f, 1, (int) tw - 12);
    b.text(pc, F22, warn ? C_WARN : C_WHITE, x + 8, y + 5 + 18 + 18.7f, 1, (int) tw - 12);
    b.bar(x + 7, y + 56 - 4 - 4, tw - 14, 4, has ? k.u : 0, warn ? C_WARN : C_BLUE);
  }
  char line[40];
  if (d.pe > 0) snprintf(line, sizeof line, "%d PARITY ERROR%s", d.pe, d.pe == 1 ? "" : "S");
  else if (d.par) snprintf(line, sizeof line, "PARITY CHECK %d%%", d.pp);
  else snprintf(line, sizeof line, "%d DISKS", d.n);
  b.text(line, F15, C_LABEL, 240, s.y[ft] + BASE15, 0, 380, 2);
}

inline void ev_detail(B &b, const Feed &f) {
  const auto &e = f.ev;
  Stack s;
  int hd = s.add(18);
  int hv = s.add(96, 8);
  int sub = s.add(26.8f, 4);
  int gr = s.add(18 + 4 + 40, 22);
  s.go(TOP, BOT);
  header(b, 0, "EV · RANGE", C_BLUE, s.y[hd]);
  char v[12], line[40];
  bool have = e.rg >= 0;
  if (have) snprintf(v, sizeof v, "%d", e.rg); else snprintf(v, sizeof v, "—");
  int g = b.group(240, 6);
  b.text(v, F96, have ? C_WHITE : C_IDLE, 0, s.y[hv] + BASE96, 1, 0, -2, g);
  if (have) b.text("mi", F22, C_LABEL, 0, s.y[hv] + BASE96, 1, 0, 0, g);
  if (e.soc >= 0 && e.lim > 0) snprintf(line, sizeof line, "at %d%% · limit %d%%", e.soc, e.lim);
  else if (e.soc >= 0) snprintf(line, sizeof line, "at %d%% · limit —", e.soc);
  else snprintf(line, sizeof line, "at — · limit —");
  b.text(line, F22, C_LABEL, 240, s.y[sub] + BASE22, 0, 380);
  if (e.up) {   // the car's software update replaces the RATE and TO columns
    char u1[40];
    if (e.upp >= 0) snprintf(u1, sizeof u1, "%d%% · %s → %s", e.upp, e.upf[0] ? e.upf : "—", e.upt[0] ? e.upt : "—");
    else snprintf(u1, sizeof u1, "%s → %s", e.upf[0] ? e.upf : "—", e.upt[0] ? e.upt : "—");
    b.text("SOFTWARE UPDATE", F15, C_BLUE, 240, s.y[gr] + BASE15, 0, 380, 2);
    b.text(u1, F22, C_WHITE, 240, s.y[gr] + 18 + 4 + BASE22 + 4, 0, 380);
    return;
  }
  bool chg = e.ch == 1;
  char rate[12] = "—", eta[12] = "—", etl[12];
  const char *ru = "", *eu = "";
  if (chg && e.kw >= 0) { snprintf(rate, sizeof rate, "%.1f", e.kw); ru = "kW"; }
  if (chg && e.mf > 0) { snprintf(eta, sizeof eta, "%d:%02d", e.mf / 60, e.mf % 60); eu = "h"; }
  else if (!chg && e.plug == 1 && e.soc >= 0 && e.lim > 0 && e.soc >= e.lim) snprintf(eta, sizeof eta, "done");
  if (e.lim > 0) snprintf(etl, sizeof etl, "TO %d%%", e.lim); else snprintf(etl, sizeof etl, "TO LIMIT");
  float cw = 140;
  for (int i = 0; i < 2; i++) {
    float cx = 240 + (i ? 1 : -1) * (cw / 2 + 10);
    b.text(i ? etl : "RATE", F15, C_LABEL, cx, s.y[gr] + BASE15, 0, (int) cw, 2);
    int gg = b.group((int) cx, 4);
    const char *val = i ? eta : rate, *un = i ? eu : ru;
    b.text(val, F40, strcmp(val, "—") == 0 ? C_IDLE : C_WHITE, 0, s.y[gr] + 18 + 4 + 34.3f, 1, 0, 0, gg);
    if (*un) b.text(un, F22, C_LABEL, 0, s.y[gr] + 18 + 4 + 34.3f, 1, 0, 0, gg);
  }
}

inline void us_detail(B &b, const Feed &f) {
  const auto &u = f.us;
  struct Row { char n[24]; int p; int sev; };
  Row rows[3];
  int nr = 0;
  auto addr = [&](const char *n, bool has, int p, int sev) { snprintf(rows[nr].n, sizeof rows[nr].n, "%s", n); rows[nr].p = has ? p : -1; rows[nr].sev = sev; nr++; };
  addr("5 hour", u.h5.has, u.h5.p, key_sev(f, "usage:5h"));
  addr("Week · all", u.wk.has, u.wk.p, key_sev(f, "usage:wk"));
  int best = -1;
  for (int i = 0; i < u.nm; i++) if (best < 0 || u.m[i].p > u.m[best].p) best = i;
  if (best >= 0) { char nm[24]; snprintf(nm, sizeof nm, "Week · %s", u.m[best].n); if (nm[7] >= 'a' && nm[7] <= 'z') nm[7] -= 32; addr(nm, true, u.m[best].p, 0); }
  Stack s;
  int hd = s.add(18);
  int bars = s.add(nr * 41.0f + (nr - 1) * 12, 14);
  int gr = s.add(47, 18);
  s.go(TOP, BOT);
  header(b, 0, "CLAUDE · LIMITS", C_BLUE, s.y[hd]);
  for (int i = 0; i < nr; i++) {
    float y = s.y[bars] + i * (41 + 12);
    b.text(rows[i].n, F22, C_WHITE, 90, y + BASE22, 1, 200);
    char v[8];
    if (rows[i].p >= 0) snprintf(v, sizeof v, "%d%%", rows[i].p); else snprintf(v, sizeof v, "—");
    b.text(v, F22, rows[i].p >= 0 ? C_WHITE : C_IDLE, 390, y + BASE22, 2, 80);
    b.bar(90, y + 26.8f + 6, 300, 8, rows[i].p < 0 ? 0 : rows[i].p, pct_col(rows[i].p, rows[i].sev));
  }
  char t[24];
  float y = s.y[gr];
  b.text("BURN", F15, C_LABEL, 90, y + BASE15, 1, 140, 2);
  if (u.bp >= 0) snprintf(t, sizeof t, "%.0f%% / h", u.bp); else snprintf(t, sizeof t, "—");
  b.text(t, F22, u.bp >= 0 ? C_WHITE : C_IDLE, 90, y + 18 + 2 + BASE22, 1, 140);
  b.text("AT THIS PACE", F15, C_LABEL, 390, y + BASE15, 2, 160, 2);
  uint32_t pc = C_IDLE;
  if (u.bp < 0) snprintf(t, sizeof t, "—");
  else if (u.lm < 0) { snprintf(t, sizeof t, "lasts to reset"); pc = C_OK; }
  else { char d[16]; dur_hm(d, sizeof d, u.lm); snprintf(t, sizeof t, "limit in %s", d); pc = C_WARN; }
  b.text(t, F22, pc, 390, y + 18 + 2 + BASE22, 2, 190);
}

inline void lb_detail(B &b, const Feed &f) {
  const auto &l = f.lb;
  int n = l.n > 6 ? 6 : l.n;
  Stack s;
  int hd = s.add(18);
  int items = s.add(n > 0 ? n * 26.8f + (n - 1) * 6 : 26.8f, 12);
  int dv = s.add(2, 10);
  int q = s.add(26.8f, 0);
  s.go(TOP, BOT);
  header(b, 0, "LAB · WAITING ON YOU", C_BLUE, s.y[hd]);
  if (n == 0) b.text("Nothing waiting", F22, C_LABEL, 240, s.y[items] + BASE22, 0, 310);
  for (int i = 0; i < n; i++) {
    float y = s.y[items] + i * (26.8f + 6);
    bool u = l.i[i].w;
    if (u) b.icon(IC_WARN, 16, C_WARN, 100, y + 5.4f);
    else b.rect(104, y + 9.4f, 8, 8, 4, C_IDLE);
    char tt[44];
    wrap_words(l.i[i].t, F22, 260, 1, (char (*)[44]) tt);
    b.text(tt, F22, u ? C_WARN : C_WHITE, 126, y + BASE22, 1, 270);
  }
  b.rect(100, s.y[dv], 280, 2, 0, C_TRACK);
  b.text("AI QUEUE", F15, C_LABEL, 100, s.y[q] + BASE22, 1, 120, 2);
  char t[28];
  if (l.q >= 0 && l.r >= 0) snprintf(t, sizeof t, "%d jobs · %d running", l.q, l.r);
  else if (l.q >= 0) snprintf(t, sizeof t, "%d jobs", l.q);
  else snprintf(t, sizeof t, "—");
  b.text(t, F22, l.q >= 0 ? C_WHITE : C_IDLE, 380, s.y[q] + BASE22, 2, 230);
}

// ----- alert view
inline int first_number(const char *m) {   // the first number followed by '%', else the first number
  for (int pass = 0; pass < 2; pass++)
    for (const char *p = m; *p; p++)
      if (*p >= '0' && *p <= '9') {
        const char *q = p;
        int v = 0;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        bool letters = q > m && ((q[-1] >= 'a' && q[-1] <= 'z') || (q[-1] >= 'A' && q[-1] <= 'Z'));   // the 1 in "disk1"
        if (pass == 0 ? *p == '%' : !letters) return v;
        p--;
      }
  return -1;
}
inline void alert_title(const Alert &a, char *out, size_t n) {
  int num = first_number(a.m);
  if (strncmp(a.k, "silent:", 7) == 0) snprintf(out, n, "%.12s silent", a.k + 7);
  else if (!strcmp(a.k, "bloom")) snprintf(out, n, "Darkbloom alerts");
  else if (!strcmp(a.k, "array")) snprintf(out, n, "Array problem");
  else if (!strcmp(a.k, "parity")) snprintf(out, n, "Parity errors");
  else if (!strcmp(a.k, "cache")) snprintf(out, n, "Cache %d%% full", num);
  else if (!strcmp(a.k, "unraid")) snprintf(out, n, "Unraid alerts");
  else if (!strcmp(a.k, "arrayfull")) snprintf(out, n, "Array %d%% full", num);
  else if (!strcmp(a.k, "full")) {
    char d[12] = "Disk";
    const char *p = strstr(a.m, "Disk ");
    if (p) { p += 5; int i = 0; while (*p && *p != ' ' && i < 8) d[i++] = *p++; d[i] = 0; }
    snprintf(out, n, "%s %d%% full", d, num);
  } else if (!strcmp(a.k, "wan")) snprintf(out, n, "No internet");
  else if (!strcmp(a.k, "failover")) snprintf(out, n, "Failover active");
  else if (!strcmp(a.k, "cap")) snprintf(out, n, "Data cap %d%%", num);
  else if (!strcmp(a.k, "usage:5h")) snprintf(out, n, "Claude 5 h %d%%", num);
  else if (strncmp(a.k, "usage", 5) == 0) snprintf(out, n, "Claude %d%%", num);
  else sc::cut(out, n, a.m, 18);
}

inline void alert_view(B &b, const Feed &f, const Nav &nv, uint32_t now) {
  int vis[MAX_AL], nvis = 0, pos = 0;
  for (int i = 0; i < f.nal; i++) if (!snoozed(nv, f, i, now)) { if (i == nv.av_i) pos = nvis; vis[nvis++] = i; }
  if (nv.av_done || nvis == 0) {
    b.s.ring = C_OK; b.s.ringw = 4;
    Stack s;
    int ic = s.add(56);
    int t = s.add(44, 12);
    int u = s.add(26.8f, 10);
    s.go(TOP, BOT);
    b.icon(IC_CHECK, 56, C_OK, 240 - 28, s.y[ic]);
    b.text("Done", F40, C_WHITE, 240, s.y[t] + BASE40_11, 0, 380);
    b.text(nvis ? "That was every alert" : "No alerts left", F22, C_LABEL, 240, s.y[u] + BASE22, 0, 380);
    hint(b, "press to go back", 372);
    alert_dots(b, nvis, nvis, C_OK);
    return;
  }
  const Alert &a = f.al[nv.av_i < f.nal ? nv.av_i : vis[0]];
  uint32_t lc = a.sv == 'r' ? C_BAD : C_WARN;
  b.s.ring = lc; b.s.ringw = 10;
  char t[24];
  snprintf(t, sizeof t, "%d OF %d", pos + 1, nvis);
  b.text(t, F15, C_LABEL, 240, 56 + BASE15, 0, 200, 2);
  // Line 1 (what) wraps to 2 lines and ends on a whole word; line 2 (where) is the location the feed sent, never a template.
  // An older feed without t / w falls back to the title derived from the key and the plain message.
  char title[48], where[72];
  if (a.t[0]) cp(title, sizeof title, a.t); else alert_title(a, title, sizeof title);
  if (a.t[0]) cp(where, sizeof where, a.w); else cp(where, sizeof where, a.m);
  char tl[2][44], wl[2][44];
  int ntl = wrap_words(title, F40, 350, 2, tl);
  int nwl = where[0] ? wrap_words(where, F22, 350, 2, wl) : 0;
  Stack s;
  int hd = s.add(18);
  int ti = s.add(ntl * 46.0f - 2, 10);
  int ds = nwl ? s.add(nwl * 28.6f, 10) : -1;
  int sr = s.add(18, 14);
  s.go(90, 340);
  char hdr[32];
  if (a.c > 1) {
    snprintf(hdr, sizeof hdr, "%s · %d %s", a.sv == 'r' ? "ALERT" : "WARNING", a.c, a.u[0] ? a.u : "ITEMS");
    for (char *p = hdr; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
  } else snprintf(hdr, sizeof hdr, "%s", a.sv == 'r' ? "ALERT" : "WARNING");
  header(b, IC_WARN, hdr, lc, s.y[hd]);
  for (int i = 0; i < ntl; i++) b.text(tl[i], F40, C_WHITE, 240, s.y[ti] + i * 46 + BASE40_11, 0, 360, 0);
  for (int i = 0; i < nwl; i++) b.text(wl[i], F22, C_LABEL, 240, s.y[ds] + i * 28.6f + BASE22_13, 0, 340);
  int tp = topic_of(a.tp);
  uint32_t secs = a.a + (uint32_t) (now - f.at_ms) / 1000;
  char dur[24];
  age_text(dur, sizeof dur, (int) secs);
  char src[48];
  snprintf(src, sizeof src, "%s · ACTIVE %s", topic_name(tp), dur);
  for (char *p = src; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
  int g = b.group(240, 8);
  if (topic_icon(tp)) b.icon(topic_icon(tp), 18, C_WHITE, 0, s.y[sr], g);
  b.text(src, F15, C_WHITE, 0, s.y[sr] + BASE15, 1, 0, 2, g);
  // hint lines: white action words
  char l1[40];
  snprintf(l1, sizeof l1, " open %s · ", topic_name(tp));
  for (char *p = l1 + 6; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (p == l1 + 6) ? *p : *p + 32;
  int h1 = b.group(240, 0);
  b.text("press", F15, C_WHITE, 0, 350 + BASE15, 1, 0, 0, h1);
  b.text(l1, F15, C_LABEL, 0, 350 + BASE15, 1, 0, 0, h1);
  b.text("turn", F15, C_WHITE, 0, 350 + BASE15, 1, 0, 0, h1);
  b.text(" next", F15, C_LABEL, 0, 350 + BASE15, 1, 0, 0, h1);
  int h2 = b.group(240, 0);
  b.text("hold", F15, C_WHITE, 0, 350 + 18 + 4 + BASE15, 1, 0, 0, h2);
  b.text(" snooze 1 h", F15, C_LABEL, 0, 350 + 18 + 4 + BASE15, 1, 0, 0, h2);
  alert_dots(b, nvis, pos, lc);
}

// ----- the night layout: hero + one line, all white
inline void night_screen(B &b, const Feed &f, const Clock &c, int topic, uint32_t now, int st) {
  char hv[16] = "—", unit[2] = "", line[40] = "";
  switch (topic) {
    case T_BL: {
      int td = -1;
      for (int i = 0; i < f.bl.n; i++) if (f.bl.m[i].td >= 0) td = (td < 0 ? 0 : td) + f.bl.m[i].td;
      if (td >= 0) snprintf(hv, sizeof hv, "$%d.%02d", td / 100, td % 100);
      snprintf(line, sizeof line, "Darkbloom today");
      break;
    }
    case T_NW: {
      const Link *l = f.nw.n ? &f.nw.l[0] : nullptr;
      if (l) mbps(hv, sizeof hv, l->d);
      snprintf(line, sizeof line, "%s Mbps down", l ? l->n : "Network");
      break;
    }
    case T_SD:
      if (f.sd.ts > 0) { snprintf(hv, sizeof hv, "%d", (int) lroundf(100.0f * f.sd.tu / f.sd.ts)); unit[0] = '%'; }
      snprintf(line, sizeof line, "Storage used");
      break;
    case T_EV:
      if (f.ev.soc >= 0) { snprintf(hv, sizeof hv, "%d", f.ev.soc); unit[0] = '%'; }
      snprintf(line, sizeof line, "EV battery");
      break;
    case T_US:
      if (f.us.h5.has) { snprintf(hv, sizeof hv, "%d", f.us.h5.p); unit[0] = '%'; }
      snprintf(line, sizeof line, "Claude 5 hour");
      break;
    case T_LAB:
      snprintf(hv, sizeof hv, "%d", f.lb.n);
      snprintf(line, sizeof line, "Lab waiting on you");
      break;
    default:
      if (c.valid) {
        struct tm tmv; time_t t = c.t; localtime_r(&t, &tmv);
        snprintf(hv, sizeof hv, "%d:%02d", tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12, tmv.tm_min);
        snprintf(line, sizeof line, "%s", tmv.tm_hour < 12 ? "AM" : "PM");
      } else snprintf(line, sizeof line, "Waiting for the time");
  }
  if (st == S_OFFLINE && topic != T_CLOCK) { snprintf(hv, sizeof hv, "—"); unit[0] = 0; snprintf(line, sizeof line, "No connection"); }
  (void) now;
  Stack s;
  int h = s.add(96);
  int l = s.add(26.8f, 10);
  s.go(TOP, BOT);
  hero(b, s.y[h], hv, unit, C_WHITE, -2);
  b.text(line, F22, C_WHITE, 240, s.y[l] + BASE22, 0, 380);
}

}  // namespace sc

// ---------------------------------------------------------------------------------------------------------------- entry
inline int dot_count(const Feed &f) {
  int c = 0;
  for (int t = 0; t < T_COUNT; t++) if (topic_on(f, t)) c++;
  return c;
}
inline int dot_index(const Feed &f, int topic) {
  int c = 0;
  for (int t = 0; t < topic; t++) if (topic_on(f, t)) c++;
  return c;
}

// brightness: effective backlight percent. <= 5 uses the night layout.
inline void make_scene(const Feed &f, const Nav &nv, const Clock &ck, uint32_t now, int brightness, Scene &out) {
  using namespace sc;
  out = Scene();
  B b(out);
  bool up = link_up(f, now);
  char worst = 0;
  int count = visible_alerts(nv, f, now, &worst);

  // The ring is the worst of the feed's level and every alert still showing, on every screen (dim, old and empty ones too).
  char lvl = f.lvl;
  if (worst == 'r') lvl = 'r'; else if (worst == 'a' && lvl != 'r') lvl = 'a';
  out.ring = C_IDLE; out.ringw = 4;
  if (up) { out.ring = lvl == 'r' ? C_BAD : lvl == 'a' ? C_WARN : C_OK; out.ringw = lvl == 'r' ? 10 : lvl == 'a' ? 6 : 4; }

  if (nv.alert && up) {
    alert_view(b, f, nv, now);
    out.key = 0xA1000000u + (nv.av_done ? 99 : (uint32_t) nv.av_i);
    return;
  }

  int topic = nv.topic;
  int st = topic_state(f, topic, now);
  bool night = brightness >= 1 && brightness <= 5;
  out.night = night;
  out.key = (uint32_t) (topic * 64 + nv.sub * 4 + (nv.detail ? 2 : 0)) + (uint32_t) st * 4096u + (night ? 65536u : 0);

  if (night) {
    marker(b, count, worst, 52);
    night_screen(b, f, ck, topic, now, st);
    return;
  }
  bool old = st == S_OLD;
  if (old) {   // "UPDATED 18 MIN AGO": the data is at least 10 minutes old and the feed says by how much
    int age = topic_age(f, topic, now);
    if (age >= 0) { char t[16]; age_text(t, sizeof t, age); snprintf(b.oldtxt, sizeof b.oldtxt, "UPDATED %s AGO", t); }
  }
  if (st == S_OFFLINE) offline_screen(b, f, topic, now);
  else if (st == S_NODATA) nodata_screen(b, topic);
  else if (nv.detail) {
    switch (topic) {
      case T_BL: bl_detail(b, f); break;
      case T_NW: nw_detail(b, f, nv.sub); break;
      case T_SD: sd_detail(b, f); break;
      case T_EV: ev_detail(b, f); break;
      case T_US: us_detail(b, f); break;
      case T_LAB: lb_detail(b, f); break;
    }
  } else {
    switch (topic) {
      case T_BL: bl_overview(b, f, old); break;
      case T_NW: nw_overview(b, f, old); break;
      case T_SD: sd_overview(b, f, old); break;
      case T_EV: ev_overview(b, f, old); break;
      case T_US: us_overview(b, f, ck, old); break;
      case T_LAB: lb_overview(b, f, old); break;
      default: clock_overview(b, f, ck, now);
    }
  }
  bool has_arc = false;   // a hero arc moves the marker down 4 px
  for (int i = 0; i < out.n; i++) if (out.it[i].k == K_ARC && out.it[i].rnd != 2) has_arc = true;
  marker(b, count, worst, has_arc ? 56 : 52);
  // hint and dots
  if (nv.detail) {
    bool sd = topic == T_SD;
    hint(b, "press to go back", sd ? 380 : 372);
    int pages = detail_count(f, topic);
    dots(b, pages, nv.sub, C_BLUE, sd ? 408 : 404);
  } else {
    if (st == S_OK && detail_count(f, topic) > 0) hint(b, "press for detail");
    else if (st == S_OLD && detail_count(f, topic) > 0) hint(b, "press for detail");
    dots(b, dot_count(f), dot_index(f, topic), C_BLUE);
  }
}

// Boot, Wi-Fi setup and firmware update screens (no feed needed).
inline void make_system(const Sys &sy, Scene &out) {
  using namespace sc;
  out = Scene();
  B b(out);
  out.ring = C_IDLE; out.ringw = 0;
  out.key = 0xB0000000u + (uint32_t) sy.mode;
  if (sy.mode == SYS_BOOT) {
    out.ringw = 0;
    float top = (480 - (120 + 24 + 18.3f + 8 + 26.8f)) / 2;
    Item *sp = b.arc(288, 288 + 319, 112, 8, C_BLUE, true);
    sp->x = 240; sp->y = (int16_t) (top + 60);   // arc centre (hero arcs leave x and y 0 = screen centre)
    b.rect(240 - 7, top + 22 - 7, 14, 14, 7, C_WHITE);
    b.text("DESK PANEL", F15, C_WHITE, 240, top + 120 + 24 + BASE15, 0, 380, 4);
    b.text("Starting", F22, C_LABEL, 240, top + 120 + 24 + 18.3f + 8 + BASE22, 0, 380);
    char v[24];
    snprintf(v, sizeof v, "v%s", sy.version);
    b.text(v, F15, C_IDLE, 240, 396 + BASE15, 0, 380);
  } else if (sy.mode == SYS_WIFI) {
    out.ringw = 4;
    Stack s;
    int ic = s.add(56);
    int t = s.add(44, 12);
    int u = s.add(26.8f, 10);
    int ap = s.add(39, 4);
    int th = s.add(18, 16);
    s.go(80, 380);
    b.icon(IC_WIFI, 56, C_BLUE, 240 - 28, s.y[ic]);
    b.text("Wi-Fi setup", F40, C_WHITE, 240, s.y[t] + BASE40_11, 0, 380);
    b.text("On your phone, join", F22, C_LABEL, 240, s.y[u] + BASE22, 0, 380);
    int g = b.group(240, 0);
    Group &gr = out.g[g - 1];
    gr.pill = true; gr.py = (int16_t) s.y[ap]; gr.ph = 39; gr.padl = 16; gr.padr = 16; gr.pr = 10; gr.pbg = C_TRACK;
    b.text(sy.ap, F22, C_WHITE, 0, s.y[ap] + 6 + BASE22, 1, 0, 0, g);
    b.text("THEN FOLLOW THE PAGE THAT OPENS", F15, C_LABEL, 240, s.y[th] + BASE15, 0, 380, 2);
  } else if (sy.mode == SYS_OTA) {
    out.ringw = 0;
    hero_arc(b, sy.pct, C_BLUE);
    Stack s;
    int hd = s.add(18);
    int hv = s.add(96, 8);
    int sub = s.add(26.8f, 4);
    int dn = s.add(18, 18);
    s.go(TOP, BOT);
    header(b, IC_DOWNLOAD, "PANEL · UPDATING", C_BLUE, s.y[hd]);
    char v[8];
    snprintf(v, sizeof v, "%d", sy.pct);
    hero(b, s.y[hv], v, "%", C_WHITE, -2);
    char t[32];
    snprintf(t, sizeof t, "from %s", sy.version);
    b.text(t, F22, C_LABEL, 240, s.y[sub] + BASE22, 0, 380);
    b.text("DON'T UNPLUG", F15, C_WHITE, 240, s.y[dn] + BASE15, 0, 380, 2);
  }
}

}  // namespace panel
