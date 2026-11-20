#pragma once
// Turns (feed, navigation state, clock) into the text and colours of ONE screen. Plain C++, no LVGL: test/panel_test.cpp
// checks every screen here for text that is too long for the round display. panel_ui.h copies a View to the widgets.
//
// Screen layout (all centred, 480x480 round, 14 px ring): title, big number, optional bar, four lines, footer.
// Line limits are in characters, sized from the circle's width at each line's height (see CAP_* below).
#include <initializer_list>
#include "panel_nav.h"

namespace panel {

constexpr uint32_t C_WHITE = 0xFFFFFF, C_GREY = 0xA0A6B0, C_BLUE = 0x4DA3FF, C_AMBER = 0xFFB000, C_RED = 0xFF3B30,
                   C_DIM = 0x606060, C_RING_OK = 0x1F5FAA;

constexpr int CAP_TITLE = 20, CAP_BIG = 10, CAP_LINE = 30, CAP_FOOT = 30;

struct View {
  char title[CAP_TITLE + 1] = "", big[CAP_BIG + 1] = "", l[4][CAP_LINE + 1] = {}, foot[CAP_FOOT + 1] = "";
  uint32_t c_title = C_BLUE, c_big = C_WHITE, c_l[4] = {C_WHITE, C_GREY, C_GREY, C_GREY}, c_foot = C_GREY;
  int bar = -1;                // 0..100, or -1 for no bar
  uint32_t c_bar = C_BLUE;
  uint32_t ring = C_RING_OK;
  char badge[12] = "";         // alert badge at the top of the screen ("! 3 ALERTS"), empty = hidden
  uint32_t c_badge = C_RED, c_badge_text = C_WHITE;
};

struct Clock {
  bool valid = false;
  char time[8] = "--:--";      // "14:05"
  char date[20] = "";          // "Fri 20 Nov"
};

// ---- small formatters (all return into the caller's buffer) ----
inline void money(char *b, size_t n, int cents) {
  if (cents < 0) { snprintf(b, n, "--"); return; }
  snprintf(b, n, "$%d.%02d", cents / 100, cents % 100);
}
inline void mbps(char *b, size_t n, float v) {
  if (v < 0) snprintf(b, n, "--");
  else if (v >= 100) snprintf(b, n, "%.0f", v);
  else snprintf(b, n, "%.1f", v);
}
inline void gbs(char *b, size_t n, float gb) {
  if (gb >= 1000) snprintf(b, n, "%.1f TB", gb / 1000.0f);
  else if (gb >= 100) snprintf(b, n, "%.0f GB", gb);
  else if (gb >= 10) snprintf(b, n, "%.1f GB", gb);
  else snprintf(b, n, "%.2f GB", gb);
}
inline void tbs(char *b, size_t n, int gb) {
  if (gb >= 1000) snprintf(b, n, "%.1f TB", gb / 1000.0f);
  else snprintf(b, n, "%d GB", gb);
}
inline void since(char *b, size_t n, uint32_t secs) {
  if (secs < 120) snprintf(b, n, "%u s", (unsigned) secs);
  else if (secs < 7200) snprintf(b, n, "%u min", (unsigned) (secs / 60));
  else snprintf(b, n, "%u h", (unsigned) (secs / 3600));
}

// Same wording as durText() in lib/panelAlerts.ts: "under 2 min", "45 min", "2 h 10 min", "3 d 4 h".
inline void dur(char *b, size_t n, uint32_t secs) {
  if (secs < 90) { snprintf(b, n, "<2 min"); return; }
  uint32_t min = (secs + 30) / 60;
  if (min < 120) { snprintf(b, n, "%u min", (unsigned) min); return; }
  uint32_t h = min / 60;
  if (h < 48) { if (min % 60) snprintf(b, n, "%u h %u min", (unsigned) h, (unsigned) (min % 60)); else snprintf(b, n, "%u h", (unsigned) h); return; }
  if (h % 24) snprintf(b, n, "%u d %u h", (unsigned) (h / 24), (unsigned) (h % 24)); else snprintf(b, n, "%u d", (unsigned) (h / 24));
}

// 2 red, 1 amber, 0 none for one alert key in the feed's list.
inline int alert_sev(const Feed &f, const char *k) {
  for (int i = 0; i < f.nal; i++) if (strcmp(f.al[i].k, k) == 0) return f.al[i].sv == 'r' ? 2 : 1;
  return 0;
}

inline bool starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

// 2 red, 1 amber, 0 none: does a red key (rr) or a displayed reason (why) start with one of these prefixes?
inline int tint(const Feed &f, std::initializer_list<const char *> prefixes) {
  int t = 0;
  for (const char *p : prefixes) {
    for (int i = 0; i < f.nrr; i++) if (starts(f.rr[i], p)) return 2;
    for (int i = 0; i < f.nwhy; i++) if (starts(f.why[i], p)) t = 1;
  }
  return t;
}
inline uint32_t tint_color(int t, uint32_t normal) { return t == 2 ? C_RED : t == 1 ? C_AMBER : normal; }

inline bool link_up(const Feed &f, uint32_t now) { return f.valid && (uint32_t) (now - f.at_ms) <= STALE_MS; }

inline const char *topic_title(int t) {
  switch (t) {
    case T_BL: return "DARKBLOOM";
    case T_NW: return "NETWORK";
    case T_SD: return "STORAGE";
    case T_EV: return "EV TONIGHT";
    case T_US: return "CLAUDE USAGE";
    default: return "CLOCK";
  }
}

// ---- pieces ----
inline void ring_for(const Feed &f, uint32_t now, View &v) {
  if (!link_up(f, now)) v.ring = C_DIM;
  else v.ring = f.lvl == 'r' ? C_RED : f.lvl == 'a' ? C_AMBER : C_RING_OK;
}

inline void no_link(const Feed &f, uint32_t now, const char *title, View &v) {
  snprintf(v.title, sizeof v.title, "%s", title);
  v.c_title = C_GREY;
  if (!f.valid && now < 45000) {
    snprintf(v.big, sizeof v.big, "WAIT");
    snprintf(v.l[0], sizeof v.l[0], "connecting to dashboard");
    v.c_big = C_GREY; v.c_l[0] = C_GREY;
    return;
  }
  snprintf(v.big, sizeof v.big, "NO LINK");
  v.c_big = C_AMBER;
  snprintf(v.l[0], sizeof v.l[0], "dashboard not reachable");
  v.c_l[0] = C_AMBER;
  if (f.valid) {
    char a[16]; since(a, sizeof a, (now - f.at_ms) / 1000);
    snprintf(v.l[1], sizeof v.l[1], "last data %s ago", a);
  } else {
    snprintf(v.l[1], sizeof v.l[1], "no data yet");
  }
}

inline void no_data(const char *title, const char *what, View &v) {
  snprintf(v.title, sizeof v.title, "%s", title);
  v.c_title = C_GREY;
  snprintf(v.big, sizeof v.big, "NO DATA");
  v.c_big = C_AMBER;
  snprintf(v.l[0], sizeof v.l[0], "%s", what);
  v.c_l[0] = C_AMBER;
}

inline void old_note(View &v, const char *foot) {
  v.c_title = C_AMBER;
  v.c_big = C_AMBER;
  snprintf(v.foot, sizeof v.foot, "%s", foot);
  v.c_foot = C_AMBER;
}

// ---- Darkbloom ----
inline void bl_overview(const Feed &f, View &v) {
  const auto &b = f.bl;
  snprintf(v.title, sizeof v.title, "DARKBLOOM");
  if (b.n == 0) { snprintf(v.big, sizeof v.big, "--"); snprintf(v.l[0], sizeof v.l[0], "no Macs reported"); return; }
  int td = -1, tm = -1, w = -1, on = 0;
  const Mac *silent = nullptr;
  for (int i = 0; i < b.n; i++) {
    const Mac &m = b.m[i];
    if (m.td >= 0) td = (td < 0 ? 0 : td) + m.td;
    if (m.tm >= 0) tm = (tm < 0 ? 0 : tm) + m.tm;
    if (m.w >= 0) w = (w < 0 ? 0 : w) + m.w;
    if (m.on) on++;
    if (m.sl && !silent) silent = &m;
  }
  money(v.big, sizeof v.big, td);
  snprintf(v.l[0], sizeof v.l[0], "earned today, %d of %d serving", on, b.n);
  if (tm >= 0) { char a[16]; money(a, sizeof a, tm); snprintf(v.l[1], sizeof v.l[1], "month %s", a); }
  if (w >= 0) snprintf(v.l[2], sizeof v.l[2], "%d W now", w);
  if (silent) { snprintf(v.l[3], sizeof v.l[3], "SILENT: %s", silent->n); v.c_l[3] = C_RED; v.c_big = C_RED; }
  else if (b.al > 0) { snprintf(v.l[3], sizeof v.l[3], "%d open alert%s", b.al, b.al == 1 ? "" : "s"); v.c_l[3] = C_AMBER; }
  snprintf(v.foot, sizeof v.foot, "press: each Mac");
  if (b.st) old_note(v, "old data");
}

inline void bl_detail(const Feed &f, int i, View &v) {
  const auto &b = f.bl;
  if (i < 0 || i >= b.n) return;
  const Mac &m = b.m[i];
  snprintf(v.title, sizeof v.title, "%.14s", m.n);
  money(v.big, sizeof v.big, m.td);
  if (m.sl) { snprintf(v.l[0], sizeof v.l[0], "SILENT"); v.c_l[0] = C_RED; v.c_big = C_RED; }
  else if (m.w >= 0) snprintf(v.l[0], sizeof v.l[0], "%s  %d W", m.s[0] ? m.s : "?", m.w);
  else snprintf(v.l[0], sizeof v.l[0], "%s", m.s[0] ? m.s : "?");
  if (m.tm >= 0) { char a[16]; money(a, sizeof a, m.tm); snprintf(v.l[1], sizeof v.l[1], "month %s", a); }
  if (m.jobs >= 0) snprintf(v.l[2], sizeof v.l[2], "sandbox jobs %d", m.jobs);
  if (m.age >= 0) { char a[16]; since(a, sizeof a, (uint32_t) m.age); snprintf(v.l[3], sizeof v.l[3], "last power %s ago", a); }
  snprintf(v.foot, sizeof v.foot, "Mac %d of %d   press: back", i + 1, b.n);
  if (b.st && !m.sl) old_note(v, "old data   press: back");
}

// ---- Network ----
inline void nw_overview(const Feed &f, View &v) {
  const auto &w = f.nw;
  snprintf(v.title, sizeof v.title, "NETWORK");
  float sum = -1;
  for (int i = 0; i < w.n; i++) if (w.l[i].d >= 0) sum = (sum < 0 ? 0 : sum) + w.l[i].d;
  mbps(v.big, sizeof v.big, sum);
  snprintf(v.l[0], sizeof v.l[0], "Mbps down, all links");
  for (int i = 0; i < w.n && i < 3; i++) {
    char d[12], u[12];
    mbps(d, sizeof d, w.l[i].d); mbps(u, sizeof u, w.l[i].u);
    snprintf(v.l[i + 1], sizeof v.l[i + 1], "%.10s  %s / %s", w.l[i].n, d, u);
  }
  if (!w.wan) { snprintf(v.foot, sizeof v.foot, "monitor offline: Unraid only"); v.c_foot = C_AMBER; }
  else snprintf(v.foot, sizeof v.foot, "Mbps down / up   press: more");
  if (w.st) old_note(v, "old data");
}

inline void nw_detail(const Feed &f, int i, View &v) {
  const auto &w = f.nw;
  if (i < 0 || i >= w.n) return;
  const Link &l = w.l[i];
  snprintf(v.title, sizeof v.title, "%.12s", l.n);
  mbps(v.big, sizeof v.big, l.d);
  char u[12]; mbps(u, sizeof u, l.u);
  snprintf(v.l[0], sizeof v.l[0], "Mbps down   up %s", u);
  const char *names_roll[3] = {"24 h", "7 days", "30 days"};
  const char *names_cal[3] = {"today", "week", "month"};
  for (int k = 0; k < 3; k++) {
    const char *nm = w.rolling ? names_roll[k] : names_cal[k];
    if (l.has_t[k]) {
      char a[16], b[16];
      gbs(a, sizeof a, l.t[k][0]); gbs(b, sizeof b, l.t[k][1]);
      snprintf(v.l[k + 1], sizeof v.l[k + 1], "%s  %s / %s", nm, a, b);
    } else {
      snprintf(v.l[k + 1], sizeof v.l[k + 1], "%s  n/a", nm);
    }
  }
  snprintf(v.foot, sizeof v.foot, "link %d of %d   down / up", i + 1, w.n);
  if (w.st) old_note(v, "old data   press: back");
}

// ---- Storage ----
inline uint32_t disk_color(const Feed &f, const Disk &d) {
  if (d.r == 'c') { int t = tint(f, {"cache"}); if (t) return tint_color(t, C_WHITE); }
  for (int i = 0; i < f.nwhy; i++) {
    if (starts(f.why[i], "full:") && strcmp(f.why[i] + 5, d.n) == 0) return C_AMBER;
  }
  return C_WHITE;
}

inline void sd_overview(const Feed &f, View &v) {
  const auto &s = f.sd;
  snprintf(v.title, sizeof v.title, "STORAGE");
  if (s.ts <= 0) { snprintf(v.big, sizeof v.big, "--"); }
  else {
    int pct = (int) ((int64_t) s.tu * 100 / s.ts);
    snprintf(v.big, sizeof v.big, "%d%%", pct);
    v.bar = pct > 100 ? 100 : pct;
  }
  int t = tint(f, {"array", "parity", "cache", "unraid", "full"});
  v.c_big = tint_color(t, C_WHITE);
  v.c_bar = t == 2 ? C_RED : t == 1 ? C_AMBER : C_BLUE;
  char a[16], b[16]; tbs(a, sizeof a, s.tu); tbs(b, sizeof b, s.ts);
  snprintf(v.l[0], sizeof v.l[0], "%s of %s used", a, b);
  snprintf(v.l[1], sizeof v.l[1], "%d of %d disks spinning", s.spin, s.nd);
  if (s.par) snprintf(v.l[2], sizeof v.l[2], "%.12s, parity check %d%%", s.arr[0] ? s.arr : "?", s.pp);
  else snprintf(v.l[2], sizeof v.l[2], "array %.12s", s.arr[0] ? s.arr : "?");
  if (tint(f, {"array"}) == 2) v.c_l[2] = C_RED;
  int hot = -1;
  for (int i = 0; i < s.n; i++) if (s.d[i].t > hot) hot = s.d[i].t;
  if (hot >= 0) snprintf(v.l[3], sizeof v.l[3], "hottest disk %d C   alerts %d", hot, s.na);
  else snprintf(v.l[3], sizeof v.l[3], "alerts %d", s.na);
  snprintf(v.foot, sizeof v.foot, "press: array and disks");
  if (s.st) old_note(v, "old data");
}

inline void sd_detail(const Feed &f, int i, View &v) {
  const auto &s = f.sd;
  int pages = detail_count(f, T_SD);
  if (i < 0 || i >= pages) return;
  if (i == 0) {
    snprintf(v.title, sizeof v.title, "ARRAY");
    snprintf(v.big, sizeof v.big, "%.10s", s.arr[0] ? s.arr : "?");
    v.c_big = tint(f, {"array"}) == 2 ? C_RED : C_WHITE;
    if (s.pe > 0) { snprintf(v.l[0], sizeof v.l[0], "parity errors %d", s.pe); v.c_l[0] = C_RED; }
    else if (s.par) snprintf(v.l[0], sizeof v.l[0], "parity check %d%%", s.pp);
    else snprintf(v.l[0], sizeof v.l[0], "parity idle");
    snprintf(v.l[1], sizeof v.l[1], "unread: %d alerts, %d warnings", s.na, s.nwn);
    if (s.na > 0) v.c_l[1] = C_RED; else if (s.nwn > 0) v.c_l[1] = C_AMBER;
    snprintf(v.l[2], sizeof v.l[2], "%d of %d disks spinning", s.spin, s.nd);
    int hot = -1;
    for (int k = 0; k < s.n; k++) if (s.d[k].t > hot) hot = s.d[k].t;
    if (hot >= 0) snprintf(v.l[3], sizeof v.l[3], "hottest disk %d C", hot);
    snprintf(v.foot, sizeof v.foot, "1 of %d   press: back", pages);
  } else {
    snprintf(v.title, sizeof v.title, "DISKS %d/%d", i, pages - 1);
    for (int r = 0; r < DISKS_PER_PAGE; r++) {
      int k = (i - 1) * DISKS_PER_PAGE + r;
      if (k >= s.n) break;
      const Disk &d = s.d[k];
      char u[8] = "--", t[10] = "", sp[8] = "";
      if (d.u >= 0) snprintf(u, sizeof u, "%d%%", d.u);
      if (d.t >= 0) snprintf(t, sizeof t, "  %dC", d.t);
      if (d.sp == 1) snprintf(sp, sizeof sp, "  on"); else if (d.sp == 0) snprintf(sp, sizeof sp, "  zzz");
      snprintf(v.l[r], sizeof v.l[r], "%.8s  %s%s%s", d.n, u, t, sp);
      v.c_l[r] = disk_color(f, d);
    }
    snprintf(v.foot, sizeof v.foot, "%d of %d   press: back", i + 1, pages);
  }
  if (s.st) old_note(v, "old data   press: back");
}

// ---- EV ----
inline void ev_overview(const Feed &f, View &v) {
  const auto &e = f.ev;
  snprintf(v.title, sizeof v.title, "EV TONIGHT");
  if (e.soc >= 0) { snprintf(v.big, sizeof v.big, "%d%%", e.soc); v.bar = e.soc > 100 ? 100 : e.soc; }
  else snprintf(v.big, sizeof v.big, "--");
  v.c_bar = e.sf ? C_AMBER : C_BLUE;
  if (e.plug >= 0) snprintf(v.l[0], sizeof v.l[0], e.plug ? "plugged in" : "not plugged in");
  if (e.tgt >= 0 && e.h >= 0) snprintf(v.l[1], sizeof v.l[1], "to %d%% in %.1f h", e.tgt, e.h);
  if (e.amps >= 0 && e.kwh >= 0) snprintf(v.l[2], sizeof v.l[2], "%d A   %.1f kWh", e.amps, e.kwh);
  if (e.sf) { snprintf(v.l[3], sizeof v.l[3], "short of target tonight"); v.c_l[3] = C_AMBER; v.c_big = C_AMBER; }
  else snprintf(v.l[3], sizeof v.l[3], e.wk == 1 ? "work day plan" : e.wk == 0 ? "weekend plan" : "");
  snprintf(v.foot, sizeof v.foot, "press: plan");
  if (e.st) old_note(v, "old data");
}

inline void ev_detail(const Feed &f, View &v) {
  const auto &e = f.ev;
  snprintf(v.title, sizeof v.title, "TONIGHT'S PLAN");
  if (e.tgt >= 0) snprintf(v.big, sizeof v.big, "%d%%", e.tgt); else snprintf(v.big, sizeof v.big, "--");
  if (e.lim >= 0) snprintf(v.l[0], sizeof v.l[0], "charge limit %d%%", e.lim);
  if (e.h >= 0 && e.amps >= 0) snprintf(v.l[1], sizeof v.l[1], "%.1f h window at %d A", e.h, e.amps);
  if (e.kwh >= 0) snprintf(v.l[2], sizeof v.l[2], "%.1f kWh from the wall", e.kwh);
  if (e.sf) { snprintf(v.l[3], sizeof v.l[3], "short of target tonight"); v.c_l[3] = C_AMBER; }
  snprintf(v.foot, sizeof v.foot, "target SoC   press: back");
  if (e.st) old_note(v, "old data   press: back");
}

// ---- Claude usage ----
inline void us_overview(const Feed &f, View &v) {
  const auto &u = f.us;
  snprintf(v.title, sizeof v.title, "CLAUDE USAGE");
  if (u.h5.has) {
    snprintf(v.big, sizeof v.big, "%d%%", u.h5.p);
    v.bar = u.h5.p > 100 ? 100 : u.h5.p;
    int t = alert_sev(f, "usage:5h");
    v.c_big = tint_color(t, C_WHITE);
    v.c_bar = t == 2 ? C_RED : t == 1 ? C_AMBER : C_BLUE;
    char d[16];
    if (u.h5.r >= 0) { dur(d, sizeof d, (uint32_t) u.h5.r); snprintf(v.l[0], sizeof v.l[0], "5 h window, resets in %s", d); }
    else snprintf(v.l[0], sizeof v.l[0], "5 h window used");
  } else {
    snprintf(v.big, sizeof v.big, "--");
    snprintf(v.l[0], sizeof v.l[0], "5 h window: no session now");
  }
  if (u.wk.has) {
    char d[16] = "";
    if (u.wk.r >= 0) dur(d, sizeof d, (uint32_t) u.wk.r);
    if (d[0]) snprintf(v.l[1], sizeof v.l[1], "week %d%%, resets in %s", u.wk.p, d);
    else snprintf(v.l[1], sizeof v.l[1], "week %d%% used", u.wk.p);
    int t = alert_sev(f, "usage:wk");
    v.c_l[1] = tint_color(t, C_GREY);
  }
  if (u.lm >= 0) {
    char d[16]; dur(d, sizeof d, (uint32_t) u.lm);
    snprintf(v.l[2], sizeof v.l[2], "at this pace: full in %s", d);
    v.c_l[2] = C_AMBER;
  } else if (u.bp >= 0) {
    snprintf(v.l[2], sizeof v.l[2], "pace %.0f%% per hour, fine", u.bp);
  }
  snprintf(v.foot, sizeof v.foot, "press: models and pace");
  if (u.st) old_note(v, "old data");
}

inline void us_detail(const Feed &f, int page, View &v) {
  const auto &u = f.us;
  int pages = detail_count(f, T_US);
  if (page < 0 || page >= pages) return;
  snprintf(v.title, sizeof v.title, "CLAUDE LIMITS");
  if (page == 0) {
    if (u.bp >= 0) snprintf(v.big, sizeof v.big, "%.0f%%/h", u.bp); else snprintf(v.big, sizeof v.big, "--");
    if (u.bp < 0) snprintf(v.l[0], sizeof v.l[0], "burn rate: too early to tell");
    else if (u.lm >= 0) { char d[16]; dur(d, sizeof d, (uint32_t) u.lm); snprintf(v.l[0], sizeof v.l[0], "5 h limit in %s", d); v.c_l[0] = C_AMBER; v.c_big = C_AMBER; }
    else snprintf(v.l[0], sizeof v.l[0], "5 h burn rate, fine");
  } else {
    snprintf(v.big, sizeof v.big, "MODELS");
  }
  if (u.nm == 0 && page == 0) { snprintf(v.l[1], sizeof v.l[1], "no per-model limits sent"); }
  for (int r = 0; r < MODELS_PER_PAGE; r++) {
    int k = page * MODELS_PER_PAGE + r;
    if (k >= u.nm) break;
    const auto &m = u.m[k];
    char d[16] = "";
    if (m.r >= 0) dur(d, sizeof d, (uint32_t) m.r);
    char key[32]; snprintf(key, sizeof key, "usage:%s", m.n);
    for (char *c = key + 6; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    if (d[0]) snprintf(v.l[1 + r], sizeof v.l[1 + r], "%.9s %d%%, %s", m.n, m.p, d);
    else snprintf(v.l[1 + r], sizeof v.l[1 + r], "%.9s %d%% of week", m.n, m.p);
    v.c_l[1 + r] = tint_color(alert_sev(f, key), C_WHITE);
  }
  snprintf(v.foot, sizeof v.foot, "%d of %d   press: back", page + 1, pages);
  if (u.st) old_note(v, "old data   press: back");
}

// ---- Clock ----
inline void clock_view(const Feed &f, const Clock &c, uint32_t now, View &v) {
  snprintf(v.title, sizeof v.title, "CLOCK");
  snprintf(v.big, sizeof v.big, "%s", c.time);
  snprintf(v.l[0], sizeof v.l[0], "%s", c.date);
  if (!link_up(f, now)) { snprintf(v.l[2], sizeof v.l[2], "no link to dashboard"); v.c_l[2] = C_AMBER; return; }
  if (f.lvl == 'g') { snprintf(v.l[2], sizeof v.l[2], "all ok"); v.c_l[2] = C_BLUE; return; }
  for (int i = 0; i < f.nwhy && i < 3; i++) {
    // two reasons per line is too wide; one per line
    snprintf(v.l[1 + i], sizeof v.l[1 + i], "%s", f.why[i]);
    v.c_l[1 + i] = f.lvl == 'r' ? C_RED : C_AMBER;
  }
}

// ---- Alerts ----
inline const char *alert_topic_name(const char *tp) {
  if (strcmp(tp, "sd") == 0) return "STORAGE";
  if (strcmp(tp, "bl") == 0) return "DARKBLOOM";
  if (strcmp(tp, "nw") == 0) return "NETWORK";
  if (strcmp(tp, "us") == 0) return "CLAUDE";
  if (strcmp(tp, "ev") == 0) return "EV CHARGE";
  return "ALERT";
}

// Greedy word wrap into at most `maxl` lines of `width` characters; a word longer than a line is split; text that does not
// fit ends in "...". Returns the number of lines used.
inline int wrap_text(const char *s, int width, char (*out)[CAP_LINE + 1], int maxl) {
  int line = 0, col = 0;
  out[0][0] = 0;
  const char *p = s;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *e = p;
    while (*e && *e != ' ') e++;
    int wl = (int) (e - p);
    if (col > 0 && col + 1 + wl > width) {          // start a new line
      if (line + 1 >= maxl) { goto overflow; }
      line++; col = 0; out[line][0] = 0;
    }
    while (wl > 0) {                                // append (splitting a very long word)
      int room = width - col - (col > 0 ? 1 : 0);
      if (room <= 0) {
        if (line + 1 >= maxl) goto overflow;
        line++; col = 0; out[line][0] = 0; continue;
      }
      int take = wl < room ? wl : room;
      if (col > 0) out[line][col++] = ' ';
      memcpy(out[line] + col, p, (size_t) take);
      col += take; out[line][col] = 0;
      p += take; wl -= take;
    }
    p = e;
  }
  return line + 1;
overflow:
  {
    int L = (int) strlen(out[line]);
    if (L > width - 3) L = width - 3;
    memcpy(out[line] + L, "...", 4);
  }
  return line + 1;
}

// The alert view: topic in big letters, the whole message, how long it has been active. Turn steps through the list.
inline void alert_view(const Feed &f, const Nav &n, uint32_t now, View &v) {
  int i = n.av_i < 0 ? 0 : n.av_i >= f.nal ? f.nal - 1 : n.av_i;
  const Alert &a = f.al[i];
  uint32_t col = a.sv == 'r' ? C_RED : C_AMBER;
  if (f.nal > 1) snprintf(v.title, sizeof v.title, "ALERT %d OF %d", i + 1, f.ac > f.nal ? f.ac : f.nal);
  else snprintf(v.title, sizeof v.title, "ALERT");
  v.c_title = col;
  snprintf(v.big, sizeof v.big, "%s", alert_topic_name(a.tp));
  v.c_big = col;
  char lines[3][CAP_LINE + 1];
  int nl = wrap_text(a.m, CAP_LINE, lines, 3);
  for (int k = 0; k < nl; k++) { snprintf(v.l[k], sizeof v.l[k], "%s", lines[k]); v.c_l[k] = C_WHITE; }
  char d[16]; dur(d, sizeof d, a.a + (now - f.at_ms) / 1000);
  snprintf(v.l[3], sizeof v.l[3], "active for %s", d);
  v.c_l[3] = C_GREY;
  snprintf(v.foot, sizeof v.foot, "%s", f.nal > 1 ? "turn next  press go  hold back" : "press go       hold back");
  v.ring = col;
}

// Badge shown on every other screen while alerts exist.
inline void badge_for(const Feed &f, View &v) {
  bool red = false;
  for (int i = 0; i < f.nal; i++) if (f.al[i].sv == 'r') red = true;
  int cnt = f.ac > f.nal ? f.ac : f.nal;
  if (cnt > 9) snprintf(v.badge, sizeof v.badge, "! 9+ ALERTS");
  else if (cnt > 1) snprintf(v.badge, sizeof v.badge, "! %d ALERTS", cnt);
  else snprintf(v.badge, sizeof v.badge, "! ALERT");
  v.c_badge = red ? C_RED : C_AMBER;
  v.c_badge_text = red ? C_WHITE : 0x000000;
}

// ---- entry point ----
inline void clip(char *s, size_t cap) { if (strlen(s) > cap) s[cap] = 0; }

inline void make_view(const Feed &f, const Nav &n, const Clock &c, uint32_t now, View &v) {
  v = View();
  ring_for(f, now, v);
  bool up = link_up(f, now);
  int topic = n.topic;
  if (n.alert && up && f.nal > 0) {
    alert_view(f, n, now, v);
  } else {
    if (alerts_live(f, now)) badge_for(f, v);
    if (topic == T_CLOCK) {
      clock_view(f, c, now, v);
    } else if (!up) {
      no_link(f, now, topic_title(topic), v);
    } else {
      bool det = n.detail;
      switch (topic) {
        case T_BL:
          if (!f.bl.ok) no_data("DARKBLOOM", "ledger not responding", v);
          else if (det) bl_detail(f, n.sub, v); else bl_overview(f, v);
          break;
        case T_NW:
          if (!f.nw.ok) no_data("NETWORK", "no network numbers", v);
          else if (det) nw_detail(f, n.sub, v); else nw_overview(f, v);
          break;
        case T_SD:
          if (!f.sd.ok) no_data("STORAGE", "no storage numbers", v);
          else if (det) sd_detail(f, n.sub, v); else sd_overview(f, v);
          break;
        case T_EV:
          if (!f.ev.ok) no_data("EV TONIGHT", "no charge plan", v);
          else if (det) ev_detail(f, v); else ev_overview(f, v);
          break;
        case T_US:
          if (!f.us.ok) no_data("CLAUDE USAGE", "waiting for the reporter", v);
          else if (det) us_detail(f, n.sub, v); else us_overview(f, v);
          break;
      }
    }
  }
  clip(v.title, CAP_TITLE); clip(v.big, CAP_BIG); clip(v.foot, CAP_FOOT);
  for (auto &l : v.l) clip(l, CAP_LINE);
}

}  // namespace panel
