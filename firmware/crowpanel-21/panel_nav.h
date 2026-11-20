#pragma once
// Navigation, alerts and backlight for crowpanel-21.yaml. Plain C++, no LVGL: test/panel_test.cpp runs it on a normal
// machine. All times are millis(); differences are done in uint32_t, so the 49-day wrap is harmless.
//
// Topics. Each has one OVERVIEW screen; a short press opens its DETAIL screens, turning moves between them, a short press
// again goes back to the overview. Turning on an overview moves to the next topic.
//   Darkbloom (home): detail = one page per Mac in the feed
//   Network:          detail = one page per link
//   Storage:          detail = array page, then 4 disks per page
//   EV tonight:       detail = the plan
//   Claude usage:     detail = per-model limits and burn rate (only when the dashboard sends usage)
//   Clock:            no detail
//
// ALERTS (one model for every screen):
//   * The feed carries a priority list (`al`, red first). While it is not empty every screen shows a badge with the count.
//   * LONG press from any screen opens the ALERT VIEW; long press there returns to the screen Penn came from.
//     In the view: turn = next alert, short press = open that alert's topic page.
//   * Seeing an alert in the view snoozes it for 15 minutes: no automatic jump for it, the badge stays. It comes back
//     by itself after 15 minutes if the cause is still there (and only red alerts ever jump).
//   * A red alert Penn has not seen yet, or whose snooze ended, jumps to the view by itself, from wherever he is.
//     Amber alerts only show in the badge. An alert that clears leaves the list on the next feed; the view closes when
//     the list is empty (home if it opened by itself, back where he was if he opened it).
#include "panel_feed.h"

namespace panel {

enum Topic { T_BL = 0, T_NW, T_SD, T_EV, T_US, T_CLOCK, T_COUNT };

constexpr uint32_t HOME_AFTER_MS = 2 * 60 * 1000;      // back to the home screen after this long without the knob
constexpr uint32_t SNOOZE_MS = 15 * 60 * 1000;         // a seen alert stays quiet this long
constexpr uint32_t STALE_MS = 75 * 1000;               // no good body for this long: "no link", numbers hidden
constexpr int DISKS_PER_PAGE = 4, MODELS_PER_PAGE = 3;

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
  int prev_topic = T_BL, prev_sub = 0;
  bool prev_detail = false;

  Slot slot[MAX_AL];
  int nslot = 0;

  uint32_t last_knob = 0;      // millis() of the last turn or press
  bool started = false;
};

inline bool topic_on(const Feed &f, int topic) { return topic != T_US || f.us.present; }

inline int detail_count(const Feed &f, int topic) {
  switch (topic) {
    case T_BL: return f.bl.ok ? f.bl.n : 0;
    case T_NW: return f.nw.ok ? f.nw.n : 0;
    case T_SD: return f.sd.ok ? 1 + (f.sd.n + DISKS_PER_PAGE - 1) / DISKS_PER_PAGE : 0;
    case T_EV: return f.ev.ok ? 1 : 0;
    case T_US: return f.us.ok ? (f.us.nm > MODELS_PER_PAGE ? 2 : 1) : 0;
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
inline bool alerts_live(const Feed &f, uint32_t now) { return f.valid && f.nal > 0 && (uint32_t) (now - f.at_ms) <= STALE_MS; }

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

inline int topic_of(const char *tp) {
  if (strcmp(tp, "bl") == 0) return T_BL;
  if (strcmp(tp, "nw") == 0) return T_NW;
  if (strcmp(tp, "sd") == 0) return T_SD;
  if (strcmp(tp, "ev") == 0) return T_EV;
  if (strcmp(tp, "us") == 0) return T_US;
  return T_BL;
}

inline bool same_name(const char *a, const char *b) {   // ASCII case-insensitive; the server lower-cases keys
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return false;
  }
  return *a == *b;
}

// Showing an alert in the view snoozes it.
inline void see(Nav &n, const Feed &f, int i, uint32_t now) {
  if (i < 0 || i >= f.nal) return;
  n.av_i = i;
  cp(n.av_k, sizeof n.av_k, f.al[i].k);
  Slot *s = find_slot(n, f.al[i].k);
  if (!s) s = add_slot(n, f.al[i]);
  if (s) { s->quiet = true; s->acked = true; s->until = now + SNOOZE_MS; }
}

inline void open_view(Nav &n, const Feed &f, int i, bool automatic, uint32_t now) {
  if (!n.alert) { n.prev_topic = n.topic; n.prev_detail = n.detail; n.prev_sub = n.sub; }
  n.alert = true;
  n.auto_open = automatic;
  see(n, f, i, now);
}

// Leave the view. `home`: go to the home screen instead of back to where Penn was.
inline void close_view(Nav &n, const Feed &f, bool home) {
  n.alert = false;
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
    if (f.nal > 0) see(n, f, (n.av_i + dir + f.nal) % f.nal, now);
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
    if (n.av_i < 0 || n.av_i >= f.nal) { close_view(n, f, false); return; }
    const Alert &a = f.al[n.av_i];
    n.alert = false;
    n.auto_open = false;
    n.topic = topic_of(a.tp); n.detail = false; n.sub = 0;
    if (n.topic == T_BL && strncmp(a.k, "silent:", 7) == 0) {   // a silent Mac: open that Mac's page
      for (int i = 0; i < f.bl.n; i++) {
        if (same_name(f.bl.m[i].n, a.k + 7) && detail_count(f, T_BL) > 0) { n.detail = true; n.sub = i; break; }
      }
    }
    clamp(n, f);
    return;
  }
  if (n.detail) { n.detail = false; n.sub = 0; return; }
  if (detail_count(f, n.topic) > 0) { n.detail = true; n.sub = 0; }
}

// Long press: the one gesture for alerts, the same on every screen. Opens the alert view (if there is anything to show) or,
// inside it, goes back to where Penn was.
inline void on_long_press(Nav &n, const Feed &f, uint32_t now) {
  touch(n, now);
  if (n.alert) { close_view(n, f, false); return; }
  if (alerts_live(f, now)) open_view(n, f, 0, false, now);
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
    if (f.nal == 0) { close_view(n, f, n.auto_open); return; }
    int i = find_alert(f, n.av_k);
    if (i < 0) i = n.av_i < f.nal ? n.av_i : f.nal - 1;
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
