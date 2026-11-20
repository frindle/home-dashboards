#pragma once
// Navigation, alerts and backlight for crowpanel-21.yaml. Plain C++, no LVGL: test/panel_test.cpp runs it on a normal
// machine. All times are millis(); differences are done in uint32_t, so the 49-day wrap is harmless.
//
// Topics, in dial order: Darkbloom, Network, Storage, EV, Claude, Lab, Clock. Claude is hidden when the feed has no `us`
// section and Lab when it has no `lb` section. Each topic has one OVERVIEW screen; a press opens its DETAIL pages, turning
// moves between them (Network has three: 24 h, 7 days, 30 days; the others have one), a press again goes back.
//
// ALERTS (one model for every screen):
//   * The feed carries a priority list (`al`, red first). The alert marker at the top shows the count of alerts that are not
//     snoozed. A snooze is panel-only and lasts SNOOZE_MS; it never changes the server.
//   * LONG press (600 ms, set in the yaml) from any screen opens the ALERT VIEW. In the view: turn = next alert, press = jump
//     to that alert's topic, long press = snooze it for an hour. Turning past the last alert shows a "Done" card; pressing
//     it returns to the screen the user came from. The view also closes when no unsnoozed alert is left.
//   * A red alert that is new, turned from amber to red, or whose snooze ended opens the view by itself (once per alert).
//     Amber alerts only show in the marker.
//   * The 2 minute return to the home screen (HOME_AFTER_MS) is kept as designed before; a view that opened by itself stays.
#include "panel_feed.h"

namespace panel {

enum Topic { T_BL = 0, T_NW, T_SD, T_EV, T_US, T_LAB, T_CLOCK, T_COUNT };

constexpr uint32_t HOME_AFTER_MS = 2 * 60 * 1000;      // back to the home screen after this long without the knob
constexpr uint32_t SNOOZE_MS = 60 * 60 * 1000;         // a snoozed alert stays quiet this long (panel only)
constexpr uint32_t STALE_MS = 75 * 1000;               // no good body for this long: "no link", numbers hidden
constexpr int NW_PERIODS = 3;     // detail pages of Network: 24 h, 7 days, 30 days

// Per-alert memory, keyed by the alert's stable key. A slot exists only while the alert is in the feed, so an alert that
// clears and comes back is new again.
struct Slot {
  char k[24] = "";
  char sv = 'a';
  bool quiet = false;      // has been jumped to / seen: do not jump for it again
  bool acked = false;      // seen in the view: quiet until `until`
  uint32_t until = 0;
};

struct Nav {
  int topic = T_BL;           // home
  bool detail = false;
  int sub = 0;

  bool alert = false;         // the alert view is showing
  bool auto_open = false;     // it opened by itself (a red jump), not by long press
  char av_k[24] = "";         // the alert it shows, followed by key so the list can change under it
  int av_i = 0;
  bool av_done = false;       // the "Done" card after the last alert
  int prev_topic = T_BL, prev_sub = 0;
  bool prev_detail = false;

  Slot slot[MAX_AL];
  int nslot = 0;

  uint32_t last_knob = 0;      // millis() of the last turn or press
  bool started = false;
};

inline bool topic_on(const Feed &f, int topic) { return (topic != T_US || f.us.present) && (topic != T_LAB || f.lb.present); }

inline int detail_count(const Feed &f, int topic) {
  switch (topic) {
    case T_BL: return f.bl.ok ? 1 : 0;
    case T_NW: return f.nw.ok ? NW_PERIODS : 0;
    case T_SD: return f.sd.ok ? 1 : 0;
    case T_EV: return f.ev.ok ? 1 : 0;
    case T_US: return f.us.ok ? 1 : 0;
    case T_LAB: return f.lb.ok ? 1 : 0;
    default: return 0;
  }
}

inline void go_home(Nav &n) {
  n.topic = T_BL; n.detail = false; n.sub = 0;
}

inline void touch(Nav &n, uint32_t now) { n.last_knob = now; n.started = true; }

// Keep the position valid when the feed changes under it (a Mac disappears, a section goes ok=0, usage is switched off).
inline void clamp(Nav &n, const Feed &f) {
  if (n.topic < 0 || n.topic >= T_COUNT || !topic_on(f, n.topic)) go_home(n);
  if (n.detail) {
    int c = detail_count(f, n.topic);
    if (c == 0) { n.detail = false; n.sub = 0; }
    else if (n.sub >= c) n.sub = c - 1;
    else if (n.sub < 0) n.sub = 0;
  }
}

// Alerts only count while the feed is fresh; a stale list is not shown.
inline bool alerts_fresh(const Feed &f, uint32_t now) { return f.valid && f.nal > 0 && (uint32_t) (now - f.at_ms) <= STALE_MS; }

inline int find_alert(const Feed &f, const char *k) {
  for (int i = 0; i < f.nal; i++) if (strcmp(f.al[i].k, k) == 0) return i;
  return -1;
}

inline Slot *find_slot(Nav &n, const char *k) {
  for (int i = 0; i < n.nslot; i++) if (strcmp(n.slot[i].k, k) == 0) return &n.slot[i];
  return nullptr;
}

inline Slot *add_slot(Nav &n, const Alert &a) {
  if (n.nslot >= MAX_AL) return nullptr;
  Slot &s = n.slot[n.nslot++];
  s = Slot();
  cp(s.k, sizeof s.k, a.k);
  s.sv = a.sv;
  return &s;
}

// True while the alert at index i of the feed list is snoozed.
inline bool snoozed(const Nav &n, const Feed &f, int i, uint32_t now) {
  if (i < 0 || i >= f.nal) return false;
  for (int k = 0; k < n.nslot; k++)
    if (strcmp(n.slot[k].k, f.al[i].k) == 0) return n.slot[k].acked && (int32_t) (now - n.slot[k].until) < 0;
  return false;
}
// Alerts shown in the marker and the view: fresh feed, not snoozed. `worst` gets 'r', 'a' or 0.
inline int visible_alerts(const Nav &n, const Feed &f, uint32_t now, char *worst = nullptr) {
  int c = 0;
  char w = 0;
  if (alerts_fresh(f, now))
    for (int i = 0; i < f.nal; i++)
      if (!snoozed(n, f, i, now)) { c++; if (f.al[i].sv == 'r') w = 'r'; else if (!w) w = 'a'; }
  if (worst) *worst = w;
  return c;
}
// Next unsnoozed alert from index `from` going `dir`, -1 when none (from itself counts last).
inline int next_visible(const Nav &n, const Feed &f, int from, int dir, uint32_t now) {
  for (int k = 1; k <= f.nal; k++) {
    int i = ((from + dir * k) % f.nal + f.nal) % f.nal;
    if (!snoozed(n, f, i, now)) return i;
  }
  return -1;
}

inline int topic_of(const char *tp) {
  if (strcmp(tp, "bl") == 0) return T_BL;
  if (strcmp(tp, "nw") == 0) return T_NW;
  if (strcmp(tp, "sd") == 0) return T_SD;
  if (strcmp(tp, "ev") == 0) return T_EV;
  if (strcmp(tp, "us") == 0) return T_US;
  if (strcmp(tp, "lb") == 0) return T_LAB;
  return T_BL;
}

[[maybe_unused]] inline bool same_name(const char *a, const char *b) {   // ASCII case-insensitive; the server lower-cases keys
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return false;
  }
  return *a == *b;
}

// Showing an alert in the view marks it as seen so it never opens the view again by itself.
inline void see(Nav &n, const Feed &f, int i, uint32_t now) {
  if (i < 0 || i >= f.nal) return;
  n.av_i = i;
  cp(n.av_k, sizeof n.av_k, f.al[i].k);
  Slot *s = find_slot(n, f.al[i].k);
  if (!s) s = add_slot(n, f.al[i]);
  if (s) s->quiet = true;
}

inline void open_view(Nav &n, const Feed &f, int i, bool automatic, uint32_t now) {
  if (!n.alert) { n.prev_topic = n.topic; n.prev_detail = n.detail; n.prev_sub = n.sub; }
  n.alert = true;
  n.av_done = false;
  n.auto_open = automatic;
  see(n, f, i, now);
}

// Leave the view. `home`: go to the home screen instead of back to where Penn was.
inline void close_view(Nav &n, const Feed &f, bool home) {
  n.alert = false;
  n.av_done = false;
  n.auto_open = false;
  if (home) go_home(n);
  else { n.topic = n.prev_topic; n.detail = n.prev_detail; n.sub = n.prev_sub; }
  clamp(n, f);
}

// dir: +1 clockwise (next), -1 anticlockwise.
inline void on_rotate(Nav &n, const Feed &f, int dir, uint32_t now) {
  if (dir == 0) return;
  dir = dir > 0 ? 1 : -1;
  touch(n, now);
  if (n.alert) {
    // visible alerts in list order, then the Done card
    int vis[MAX_AL], nv = 0, cur = -1;
    for (int i = 0; i < f.nal; i++) if (!snoozed(n, f, i, now)) { if (!n.av_done && i == n.av_i) cur = nv; vis[nv++] = i; }
    int pos = n.av_done ? nv : (cur < 0 ? 0 : cur);
    pos = (pos + dir + nv + 1) % (nv + 1);
    if (pos == nv) n.av_done = true;
    else { n.av_done = false; see(n, f, vis[pos], now); }
    return;
  }
  if (n.detail) {
    int c = detail_count(f, n.topic);
    if (c > 0) n.sub = (n.sub + dir + c) % c;
  } else {
    int t = n.topic;
    for (int k = 0; k < T_COUNT; k++) { t = (t + dir + T_COUNT) % T_COUNT; if (topic_on(f, t)) break; }
    n.topic = t;
  }
}

// Short press. In the alert view it opens the topic page of the alert on screen; elsewhere it opens or closes detail.
inline void on_press(Nav &n, const Feed &f, uint32_t now) {
  touch(n, now);
  if (n.alert) {
    if (n.av_done || n.av_i < 0 || n.av_i >= f.nal) { close_view(n, f, false); return; }
    const Alert &a = f.al[n.av_i];
    n.alert = false;
    n.auto_open = false;
    n.topic = topic_of(a.tp); n.detail = false; n.sub = 0;
    clamp(n, f);
    return;
  }
  if (n.detail) { n.detail = false; n.sub = 0; return; }
  if (detail_count(f, n.topic) > 0) { n.detail = true; n.sub = 0; }
}

// Long press (600 ms): the same on every screen. Outside the alert view it opens the view (when an alert is showing). Inside
// it, it snoozes the alert on screen for SNOOZE_MS and moves to the next one, or leaves the view when none is left.
inline void on_long_press(Nav &n, const Feed &f, uint32_t now) {
  touch(n, now);
  if (n.alert) {
    if (n.av_done) { close_view(n, f, false); return; }
    if (n.av_i >= 0 && n.av_i < f.nal) {
      Slot *s = find_slot(n, f.al[n.av_i].k);
      if (!s) s = add_slot(n, f.al[n.av_i]);
      if (s) { s->quiet = true; s->acked = true; s->until = now + SNOOZE_MS; }
    }
    int i = next_visible(n, f, n.av_i, 1, now);
    if (i < 0) close_view(n, f, n.auto_open); else see(n, f, i, now);
    return;
  }
  int i = next_visible(n, f, -1, 1, now);
  if (alerts_fresh(f, now) && i >= 0) open_view(n, f, i, false, now);
}

// Call about once a second. Returns to the home screen after HOME_AFTER_MS without the knob; a view that opened by itself
// stays until Penn acts on it or the alerts clear.
inline void on_tick(Nav &n, const Feed &f, uint32_t now) {
  if (!n.started) { n.last_knob = now; n.started = true; }
  clamp(n, f);
  if (n.alert && n.auto_open) return;
  if ((n.alert || n.topic != T_BL || n.detail) && (uint32_t) (now - n.last_knob) >= HOME_AFTER_MS) {
    if (n.alert) close_view(n, f, true); else go_home(n);
  }
}

// Call with every NEW good body (not for a stale or failed poll).
//  - forget alerts that left the list, so one that comes back counts as new
//  - view open: follow the alert on screen by key; close when the list is empty
//  - view closed: jump to the view when a red alert is new, turned from amber to red, or its snooze ended
inline void on_feed(Nav &n, const Feed &f, uint32_t now) {
  int w = 0;
  for (int i = 0; i < n.nslot; i++) {
    if (find_alert(f, n.slot[i].k) >= 0) { if (w != i) n.slot[w] = n.slot[i]; w++; }
  }
  n.nslot = w;

  if (n.alert) {
    if (n.av_done) { if (visible_alerts(n, f, now) == 0) close_view(n, f, false); return; }
    if (visible_alerts(n, f, now) == 0) { close_view(n, f, n.auto_open); return; }
    int i = find_alert(f, n.av_k);
    if (i < 0 || snoozed(n, f, i, now)) i = next_visible(n, f, i < 0 ? (n.av_i < f.nal ? n.av_i : f.nal - 1) : i, 1, now);
    n.av_i = i;
    cp(n.av_k, sizeof n.av_k, f.al[i].k);
    for (int k = 0; k < f.nal; k++) {                 // new alerts while looking: counted, not yanked around
      Slot *s = find_slot(n, f.al[k].k);
      if (!s) s = add_slot(n, f.al[k]);
      if (s) { s->sv = f.al[k].sv; if (k != i) s->quiet = true; }
    }
    return;
  }

  int first = -1;
  for (int k = 0; k < f.nal; k++) {
    const Alert &a = f.al[k];
    Slot *s = find_slot(n, a.k);
    if (!s) s = add_slot(n, a);
    if (!s) continue;
    if (a.sv == 'r' && s->sv != 'r') { s->quiet = false; s->acked = false; }   // amber became red
    s->sv = a.sv;
    if (a.sv != 'r') { if (!s->acked && !s->quiet) s->quiet = true; continue; }   // amber never jumps
    if (s->acked && (int32_t) (now - s->until) >= 0) { s->quiet = false; s->acked = false; }   // snooze over
    if (!s->quiet && first < 0) first = k;
  }
  if (first >= 0) {
    for (int k = 0; k < f.nal; k++) if (f.al[k].sv == 'r') { Slot *s = find_slot(n, f.al[k].k); if (s && !s->acked) s->quiet = true; }
    open_view(n, f, first, true, now);
  }
}

// Backlight: the server's night schedule (br) if it sent one, else a default. A knob touch in the last minute lifts a
// dim screen to at least 40% so the user can read it; it dims again by itself.
constexpr int DEFAULT_BRIGHTNESS = 60;
constexpr uint32_t WAKE_MS = 60 * 1000;
inline int brightness(const Feed &f, const Nav &n, uint32_t now) {
  int b = (f.valid && f.br > 0) ? f.br : DEFAULT_BRIGHTNESS;
  if (n.started && (uint32_t) (now - n.last_knob) < WAKE_MS && b < 40) b = 40;
  return b;
}

}  // namespace panel
