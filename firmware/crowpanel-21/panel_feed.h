#pragma once
// Panel feed model and parser for crowpanel-21.yaml. Plain C++ (no LVGL, no ESPHome types except the JSON header),
// so test/panel_test.cpp can build it on a normal machine. Key legend: lib/panelFeed.ts in the dashboard repo.
//
// Everything is fixed-size: no heap use while parsing, so a long-running panel cannot fragment memory.
// Absent numbers are -1 (or has_* false); the feed leaves missing numbers out instead of sending null.
#include <cstdint>
#include <cstdio>
#include <cstring>
#if __has_include("esphome/components/json/json_util.h")
#include "esphome/components/json/json_util.h"
#else
#include <ArduinoJson.h>
#endif

namespace panel {

// Same limits as MAX_MACS / MAX_LINKS / MAX_DISKS in lib/panelFeed.ts. More Macs than this are ignored.
constexpr int MAX_MACS = 4, MAX_LINKS = 4, MAX_DISKS = 16, MAX_RR = 4, MAX_WHY = 3;
constexpr int FEED_VERSION = 1;

inline void cp(char *dst, size_t n, const char *src) {
  if (!src) src = "";
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

struct Mac {
  char n[16] = "", s[14] = "";
  bool sl = false, on = false;
  int w = -1, td = -1, tm = -1, jobs = -1, age = -1;
};
struct Link {
  char n[12] = "";
  float d = -1, u = -1;
  bool has_t[3] = {false, false, false};   // 1 day, 7 days, 30 days
  float t[3][2] = {{0, 0}, {0, 0}, {0, 0}};  // GB down, GB up
};
struct Disk {
  char n[10] = "";
  char r = 'd';
  int u = -1, sp = -1, t = -1;
};

struct Feed {
  bool valid = false;        // a good body has been parsed at least once
  uint32_t at_ms = 0;        // millis() when the last good body arrived
  char lvl = 'g';
  char go[4] = "";
  char rr[MAX_RR][24] = {};
  int nrr = 0;
  char why[MAX_WHY][24] = {};
  int nwhy = 0;
  int br = -1;               // backlight percent from the server's night schedule, -1 = none

  struct Bl { bool ok = false, st = false; int n = 0, al = 0; Mac m[MAX_MACS]; } bl;
  struct Nw { bool ok = false, st = false, wan = false, rolling = false; int n = 0; Link l[MAX_LINKS]; } nw;
  struct Sd {
    bool ok = false, st = false;
    int ts = 0, tu = 0, nd = 0, spin = 0, par = 0, pp = 0, pe = 0, na = 0, nwn = 0, n = 0;
    char arr[14] = "";
    Disk d[MAX_DISKS];
  } sd;
  struct Ev {
    bool ok = false, st = false;
    int plug = -1, soc = -1, lim = -1, amps = -1, tgt = -1, sf = 0, wk = -1;
    float kwh = -1, h = -1;
  } ev;
};

namespace detail {
inline int geti(JsonVariantConst v, int dflt = -1) { return v.is<int>() ? v.as<int>() : dflt; }
inline float getf(JsonVariantConst v, float dflt = -1) { return v.is<float>() ? v.as<float>() : dflt; }
inline bool ok_of(JsonObjectConst o) { return o["ok"].as<int>() == 1; }
}  // namespace detail

// Parses one body into `out`. Returns false (and leaves `out` untouched) if it is not a v1 panel body. `out` is only
// written when the whole body parsed, so a bad or half-sent body can never leave the panel with mixed old/new numbers.
inline bool parse_feed(JsonObjectConst root, Feed &out, uint32_t now_ms) {
  using namespace detail;
  if (root["v"].as<int>() != FEED_VERSION) return false;
  static Feed f;   // scratch copy, static to keep ~1.5 KB off the stack
  f = Feed();
  f.valid = true;
  f.at_ms = now_ms;
  const char *lvl = root["lvl"] | "g";
  f.lvl = (lvl[0] == 'r' || lvl[0] == 'a') ? lvl[0] : 'g';
  cp(f.go, sizeof f.go, root["go"] | "");
  for (JsonVariantConst v : root["rr"].as<JsonArrayConst>()) {
    if (f.nrr >= MAX_RR) break;
    cp(f.rr[f.nrr++], sizeof f.rr[0], v.as<const char *>());
  }
  for (JsonVariantConst v : root["why"].as<JsonArrayConst>()) {
    if (f.nwhy >= MAX_WHY) break;
    cp(f.why[f.nwhy++], sizeof f.why[0], v.as<const char *>());
  }
  f.br = geti(root["br"]);
  if (f.br > 100) f.br = 100;
  if (f.br == 0) f.br = 1;

  // ---- Darkbloom
  JsonObjectConst bl = root["bl"];
  f.bl.ok = ok_of(bl);
  f.bl.st = bl["st"].as<int>() == 1;
  f.bl.al = geti(bl["al"], 0);
  if (f.bl.ok) {
    for (JsonObjectConst o : bl["m"].as<JsonArrayConst>()) {
      if (f.bl.n >= MAX_MACS) break;
      Mac &m = f.bl.m[f.bl.n++];
      cp(m.n, sizeof m.n, o["n"] | "mac");
      cp(m.s, sizeof m.s, o["s"] | "");
      m.sl = o["sl"].as<int>() == 1;
      m.on = o["on"].as<int>() == 1;
      m.w = geti(o["w"]); m.td = geti(o["td"]); m.tm = geti(o["tm"]); m.jobs = geti(o["jobs"]); m.age = geti(o["age"]);
    }
  }

  // ---- Network
  JsonObjectConst nw = root["nw"];
  f.nw.ok = ok_of(nw);
  f.nw.st = nw["st"].as<int>() == 1;
  f.nw.wan = strcmp(nw["src"] | "", "wan") == 0;
  f.nw.rolling = strcmp(nw["tp"] | "r", "c") != 0;
  if (f.nw.ok) {
    for (JsonObjectConst o : nw["l"].as<JsonArrayConst>()) {
      if (f.nw.n >= MAX_LINKS) break;
      Link &l = f.nw.l[f.nw.n++];
      cp(l.n, sizeof l.n, o["n"] | "link");
      l.d = getf(o["d"]); l.u = getf(o["u"]);
      const char *keys[3] = {"t1", "t7", "t30"};
      for (int i = 0; i < 3; i++) {
        JsonArrayConst a = o[keys[i]].as<JsonArrayConst>();
        if (a.size() == 2 && a[0].is<float>() && a[1].is<float>()) {
          l.has_t[i] = true; l.t[i][0] = a[0].as<float>(); l.t[i][1] = a[1].as<float>();
        }
      }
    }
  }

  // ---- Storage
  JsonObjectConst sd = root["sd"];
  f.sd.ok = ok_of(sd);
  f.sd.st = sd["st"].as<int>() == 1;
  if (f.sd.ok) {
    f.sd.ts = geti(sd["tot"]["s"], 0); f.sd.tu = geti(sd["tot"]["u"], 0);
    f.sd.nd = geti(sd["nd"], 0); f.sd.spin = geti(sd["spin"], 0);
    cp(f.sd.arr, sizeof f.sd.arr, sd["arr"] | "");
    f.sd.par = sd["par"].as<int>() == 1; f.sd.pp = geti(sd["pp"], 0); f.sd.pe = geti(sd["pe"], 0);
    f.sd.na = geti(sd["na"], 0); f.sd.nwn = geti(sd["nwn"], 0);
    for (JsonObjectConst o : sd["d"].as<JsonArrayConst>()) {
      if (f.sd.n >= MAX_DISKS) break;
      Disk &d = f.sd.d[f.sd.n++];
      cp(d.n, sizeof d.n, o["n"] | "?");
      const char *r = o["r"] | "d";
      d.r = (r[0] == 'c' || r[0] == 'p') ? r[0] : 'd';
      d.u = geti(o["u"]); d.sp = geti(o["sp"]); d.t = geti(o["t"]);
    }
  }

  // ---- EV
  JsonObjectConst ev = root["ev"];
  f.ev.ok = ok_of(ev);
  f.ev.st = ev["st"].as<int>() == 1;
  if (f.ev.ok) {
    f.ev.plug = geti(ev["plug"]); f.ev.soc = geti(ev["soc"]); f.ev.lim = geti(ev["lim"]); f.ev.amps = geti(ev["amps"]);
    f.ev.tgt = geti(ev["tgt"]); f.ev.sf = geti(ev["sf"], 0); f.ev.wk = geti(ev["wk"]);
    f.ev.kwh = getf(ev["kwh"]); f.ev.h = getf(ev["h"]);
  }

  out = f;
  return true;
}

}  // namespace panel
