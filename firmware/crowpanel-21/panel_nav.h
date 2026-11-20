#pragma once
// Navigation and red auto-switch for crowpanel-21.yaml. Plain C++, no LVGL: test/panel_test.cpp runs it on a normal
// machine. All times are millis(); differences are done in uint32_t, so the 49-day wrap is harmless.
//
// Model: five topics. Each has one OVERVIEW screen; pressing opens its DETAIL screens, turning moves between them,
// pressing again goes back to the overview. Turning on an overview moves to the next topic.
//   Darkbloom (home): detail = one page per Mac in the feed
//   Network:          detail = one page per link
//   Storage:          detail = array page, then 4 disks per page
//   EV tonight:       detail = the plan
//   Clock:            no detail
// The ALERT screen sits on top of all of that while the feed is red (see on_feed).
#include "panel_feed.h"

namespace panel {

enum Topic { T_BL = 0, T_NW, T_SD, T_EV, T_CLOCK, T_COUNT };

constexpr uint32_t HOME_AFTER_MS = 2 * 60 * 1000;      // back to the home screen after this long without the knob
constexpr uint32_t SNOOZE_MS = 15 * 60 * 1000;         // turning or pressing on an alert silences it this long
constexpr uint32_t STALE_MS = 75 * 1000;               // no good body for this long: "no link", numbers hidden
constexpr int DISKS_PER_PAGE = 4;

struct Nav {
  int topic = T_BL;           // home
  bool detail = false;
  int sub = 0;

  bool alert = false;         // the alert screen is showing
  int alert_topic = T_BL;     // where a press on it goes
  int alert_sub = 0;
  int prev_topic = T_BL, prev_sub = 0;
  bool prev_detail = false;
  bool jumped = false;        // current screen is the result of an automatic jump: going home on "clear" is allowed

  bool red_prev = false;
  bool snoozed = false;
  uint32_t snooze_until = 0;
  char seen[MAX_RR][24] = {};  // red reason keys already shown in this red spell
  int nseen = 0;

  uint32_t last_knob = 0;      // millis() of the last turn or press
  bool started = false;
};

inline int detail_count(const Feed &f, int topic) {
  switch (topic) {
    case T_BL: return f.bl.ok ? f.bl.n : 0;
    case T_NW: return f.nw.ok ? f.nw.n : 0;
    case T_SD: return f.sd.ok ? 1 + (f.sd.n + DISKS_PER_PAGE - 1) / DISKS_PER_PAGE : 0;
    case T_EV: return f.ev.ok ? 1 : 0;
    default: return 0;
  }
}

inline void go_home(Nav &n) {
  n.topic = T_BL; n.detail = false; n.sub = 0;
}

inline void touch(Nav &n, uint32_t now) { n.last_knob = now; n.started = true; }

// Keep the position valid when the feed changes under it (a Mac disappears, a section goes ok=0).
inline void clamp(Nav &n, const Feed &f) {
  if (n.topic < 0 || n.topic >= T_COUNT) go_home(n);
  if (n.detail) {
    int c = detail_count(f, n.topic);
    if (c == 0) { n.detail = false; n.sub = 0; }
    else if (n.sub >= c) n.sub = c - 1;
    else if (n.sub < 0) n.sub = 0;
  }
}

inline void snooze(Nav &n, uint32_t now) {
  n.snoozed = true;
  n.snooze_until = now + SNOOZE_MS;
}

// dir: +1 clockwise (next), -1 anticlockwise.
inline void on_rotate(Nav &n, const Feed &f, int dir, uint32_t now) {
  if (dir == 0) return;
  dir = dir > 0 ? 1 : -1;
  touch(n, now);
  if (n.alert) {
    // Turning dismisses the alert and snoozes it; the turn then moves from where the user was before the jump.
    n.alert = false;
    n.jumped = false;
    snooze(n, now);
    n.topic = n.prev_topic; n.detail = n.prev_detail; n.sub = n.prev_sub;
    clamp(n, f);
  }
  if (n.detail) {
    int c = detail_count(f, n.topic);
    if (c > 0) n.sub = (n.sub + dir + c) % c;
  } else {
    n.topic = (n.topic + dir + T_COUNT) % T_COUNT;
    n.jumped = false;
  }
}

inline void on_press(Nav &n, const Feed &f, uint32_t now) {
  touch(n, now);
  if (n.alert) {
    // Pressing on the alert snoozes it and opens the detail it points at (the silent Mac, or Storage).
    n.alert = false;
    snooze(n, now);
    n.topic = n.alert_topic; n.sub = n.alert_sub; n.detail = true;
    clamp(n, f);
    return;
  }
  if (n.detail) { n.detail = false; n.sub = 0; return; }
  if (detail_count(f, n.topic) > 0) { n.detail = true; n.sub = 0; }
}

// Call about once a second. Returns to the home screen after HOME_AFTER_MS without the knob. The alert screen stays
// until the user acts on it or the feed clears.
inline void on_tick(Nav &n, const Feed &f, uint32_t now) {
  if (!n.started) { n.last_knob = now; n.started = true; }
  clamp(n, f);
  if (n.alert) return;
  if ((n.topic != T_BL || n.detail) && (uint32_t) (now - n.last_knob) >= HOME_AFTER_MS) go_home(n);
}

inline bool seen_has(const Nav &n, const char *k) {
  for (int i = 0; i < n.nseen; i++) if (strcmp(n.seen[i], k) == 0) return true;
  return false;
}

inline void jump(Nav &n, const Feed &f) {
  if (!n.alert) {   // remember where the user was, so turning gives that back
    n.prev_topic = n.topic; n.prev_detail = n.detail; n.prev_sub = n.sub;
  }
  n.alert = true;
  n.jumped = true;
  n.alert_topic = strcmp(f.go, "sd") == 0 ? T_SD : T_BL;
  n.alert_sub = 0;
  if (n.alert_topic == T_BL) {
    for (int i = 0; i < f.bl.n; i++) if (f.bl.m[i].sl) { n.alert_sub = i; break; }
  }
}

// Call with every NEW good body (not for a stale or failed poll).
//  - not red: forget the spell. If we had jumped, go home.
//  - red with a reason key not seen in this spell: jump at once, even during a snooze.
//  - red, same reasons: jump again only when the 15-minute snooze is over and the alert screen is not up.
// "Red" needs a `go` target; the feed always sets one with a red level.
inline void on_feed(Nav &n, const Feed &f, uint32_t now) {
  bool red = f.lvl == 'r' && f.nrr > 0 && f.go[0];
  if (!red) {
    if (n.red_prev) {
      n.nseen = 0;
      n.snoozed = false;
      n.alert = false;
      if (n.jumped) go_home(n);
      n.jumped = false;
    }
    n.red_prev = false;
    return;
  }
  n.red_prev = true;
  bool fresh = false;
  for (int i = 0; i < f.nrr; i++) if (!seen_has(n, f.rr[i])) fresh = true;
  n.nseen = 0;                                      // the seen set follows the current reasons: one that clears and
  for (int i = 0; i < f.nrr; i++) {                 // comes back counts as new again
    cp(n.seen[n.nseen++], sizeof n.seen[0], f.rr[i]);
  }
  bool snooze_over = !n.snoozed || (int32_t) (now - n.snooze_until) >= 0;
  if (fresh || (snooze_over && !n.alert)) jump(n, f);
  else if (n.alert) {                               // keep the press target current (the silent Mac may change)
    n.alert_topic = strcmp(f.go, "sd") == 0 ? T_SD : T_BL;
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
