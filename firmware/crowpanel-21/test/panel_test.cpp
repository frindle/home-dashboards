// Host tests for panel_feed.h / panel_nav.h / panel_view.h. No LVGL, no ESPHome.
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "panel_view.h"

using namespace panel;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static bool load(Feed &f, const std::string &json, uint32_t now) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  return parse_feed(doc.as<JsonObjectConst>(), f, now);
}

static const char *MAC = R"({"n":"studio-m4","s":"serving","on":1,"w":22,"td":1234,"tm":45600,"jobs":3,"age":12})";

static std::string body(const char *lvl, const char *go, const char *rr, const char *why, const char *macs = nullptr) {
  std::string m = macs ? macs : (std::string(MAC) + "," + R"({"n":"mini","s":"serving","on":1,"w":8,"td":300,"tm":9000,"jobs":0,"age":5})");
  std::string s = R"({"v":1,"lvl":")" + std::string(lvl) + "\"";
  if (go && *go) s += std::string(",\"go\":\"") + go + "\"";
  if (rr && *rr) s += std::string(",\"rr\":[") + rr + "]";
  s += std::string(",\"why\":[") + (why ? why : "") + "]";
  s += R"(,"bl":{"ok":1,"al":0,"m":[)" + m + "]}";
  s += R"(,"nw":{"ok":1,"src":"wan","tp":"r","l":[{"n":"Cox","d":312.4,"u":23.1,"t1":[10.5,2.2],"t7":[80,12],"t30":[300,50]},{"n":"Starlink","d":41.2,"u":8,"t1":[1,1]}]})";
  s += R"(,"sd":{"ok":1,"tot":{"s":96000,"u":71000},"nd":6,"spin":2,"arr":"STARTED","par":0,"pp":0,"pe":0,"na":0,"nwn":1,"d":[{"n":"disk1","r":"d","u":71,"sp":1,"t":34},{"n":"disk2","r":"d","u":80,"sp":0,"t":31},{"n":"cache","r":"c","u":55,"sp":1,"t":40},{"n":"disk3","u":10},{"n":"disk4","u":10},{"n":"disk5","u":10}]})";
  s += R"(,"ev":{"ok":1,"plug":1,"soc":54,"lim":80,"amps":16,"tgt":80,"kwh":12.5,"h":6.5,"sf":0,"wk":1}})";
  return s;
}

static void test_parse() {
  Feed f;
  CHECK(!f.valid);
  CHECK(load(f, body("g", "", "", ""), 1000));
  CHECK(f.valid && f.at_ms == 1000 && f.lvl == 'g');
  CHECK(f.bl.ok && f.bl.n == 2 && f.bl.m[0].td == 1234 && strcmp(f.bl.m[1].n, "mini") == 0);
  CHECK(f.nw.ok && f.nw.wan && f.nw.rolling && f.nw.n == 2);
  CHECK(f.nw.l[0].has_t[0] && f.nw.l[0].has_t[1] && f.nw.l[0].has_t[2] && f.nw.l[0].t[1][0] == 80.0f);
  CHECK(f.nw.l[1].has_t[0] && !f.nw.l[1].has_t[1]);
  CHECK(f.sd.ok && f.sd.n == 6 && f.sd.d[2].r == 'c' && f.sd.d[3].t == -1 && f.sd.d[3].sp == -1);
  CHECK(f.ev.ok && f.ev.soc == 54 && f.ev.h > 6.4f);
  CHECK(f.br == -1);

  // wrong version or junk: refused, previous data untouched
  Feed g = f;
  CHECK(!load(g, R"({"v":2,"lvl":"r"})", 5));
  CHECK(g.lvl == 'g' && g.at_ms == 1000);
  CHECK(!load(g, "not json", 5));
  CHECK(!load(g, "{}", 5));
  CHECK(g.at_ms == 1000);

  // N Macs: 6 sent, 4 kept
  std::string macs;
  for (int i = 0; i < 6; i++) macs += std::string(i ? "," : "") + R"({"n":"m)" + std::to_string(i) + R"(","s":"serving","on":1,"td":1})";
  Feed h;
  CHECK(load(h, body("g", "", "", "", macs.c_str()), 1));
  CHECK(h.bl.n == MAX_MACS);

  // optional/legacy: v1 reader without any new keys; sections off
  Feed k;
  CHECK(load(k, R"({"v":1,"lvl":"a","why":["cache:92%"],"bl":{"ok":0},"nw":{"ok":0},"sd":{"ok":0},"ev":{"ok":0},"br":0})", 7));
  CHECK(k.lvl == 'a' && !k.bl.ok && !k.nw.ok && !k.sd.ok && !k.ev.ok && k.nwhy == 1 && k.br == 1);
  CHECK(strcmp(k.why[0], "cache:92%") == 0);

  // long strings are cut, not overflowed
  Feed l;
  CHECK(load(l, body("g", "", "", "", R"({"n":"a-very-very-long-mac-name-indeed","s":"serving-and-then-some-more","on":1})"), 1));
  CHECK(strlen(l.bl.m[0].n) < sizeof l.bl.m[0].n);
  // br 150 clamps
  CHECK(load(l, R"({"v":1,"br":150})", 1) && l.br == 100);
}

static Feed green() { Feed f; load(f, body("g", "", "", ""), 0); return f; }
static Feed amber() { Feed f; load(f, body("a", "", "", "\"cache:85%\""), 0); return f; }

// A feed with an alert list ("al"), optional "ac" and usage section ("us").
static Feed with(const char *al, const char *us = nullptr, int ac = 0, uint32_t at = 0) {
  std::string b = body("g", "", "", "");
  std::string add = std::string("\"al\":[") + al + "]";
  if (ac) add += ",\"ac\":" + std::to_string(ac);
  if (us) add += std::string(",\"us\":") + us;
  b.insert(b.find("\"bl\""), add + ",");
  Feed f; load(f, b, at); return f;
}
#define A_CACHE R"({"k":"cache","tp":"sd","sv":"r","m":"The cache pool is 93% full. Free space or move data off it.","a":30})"
#define A_SILENT R"({"k":"silent:mini","tp":"bl","sv":"r","m":"mini has stopped serving Darkbloom (no activity for over 30 min).","a":5})"
#define A_WAN R"({"k":"wan","tp":"nw","sv":"a","m":"Every internet link looks idle. The connection may be down.","a":700})"
#define A_USAGE R"({"k":"usage:5h","tp":"us","sv":"r","m":"Claude 5-hour usage is 96%. Resets in 80 min.","a":0})"
#define US_OK R"({"ok":1,"st":0,"age":5,"h5":{"p":96,"r":4800},"wk":{"p":40,"r":200000},"m":[{"n":"Opus","p":70,"r":200000},{"n":"Sonnet","p":20,"r":200000}],"bp":30,"lm":900})"

static void test_nav() {
  Feed f = green(); Nav n; uint32_t t = 1000;
  CHECK(n.topic == T_BL && !n.detail);
  on_rotate(n, f, 1, t); CHECK(n.topic == T_NW);
  on_rotate(n, f, 1, t); on_rotate(n, f, 1, t); on_rotate(n, f, 1, t); CHECK(n.topic == T_CLOCK);
  on_rotate(n, f, 1, t); CHECK(n.topic == T_BL);          // wraps
  on_rotate(n, f, -1, t); CHECK(n.topic == T_CLOCK);      // and back
  on_press(n, f, t); CHECK(!n.detail);                    // clock has no detail
  on_rotate(n, f, 1, t);                                  // -> BL
  on_press(n, f, t); CHECK(n.detail && n.sub == 0);       // overview -> detail
  on_rotate(n, f, 1, t); CHECK(n.sub == 1);
  on_rotate(n, f, 1, t); CHECK(n.sub == 0);               // 2 macs, wraps inside detail
  on_rotate(n, f, -1, t); CHECK(n.sub == 1 && n.topic == T_BL);
  on_press(n, f, t); CHECK(!n.detail && n.topic == T_BL); // press = back

  // storage detail: array page + ceil(6/4)=2 disk pages = 3
  CHECK(detail_count(f, T_SD) == 3);
  n = Nav(); on_rotate(n, f, 1, t); on_rotate(n, f, 1, t); CHECK(n.topic == T_SD);
  on_press(n, f, t);
  for (int i = 0; i < 3; i++) on_rotate(n, f, 1, t);
  CHECK(n.sub == 0);

  // idle: home after 2 minutes, not before; the home screen stays
  n = Nav(); on_rotate(n, f, 1, 0); on_press(n, f, 0); on_tick(n, f, 1000);
  CHECK(n.topic == T_NW && n.detail);
  on_tick(n, f, 119999); CHECK(n.topic == T_NW);
  on_tick(n, f, 120000); CHECK(n.topic == T_BL && !n.detail);
  // a turn restarts the idle clock
  n = Nav(); on_rotate(n, f, 1, 0); on_rotate(n, f, 1, 100000); on_tick(n, f, 200000); CHECK(n.topic == T_SD);
  on_tick(n, f, 220001); CHECK(n.topic == T_BL);
  // uint32 wrap safe
  n = Nav(); uint32_t big = 0xFFFFFFFFu - 1000; on_rotate(n, f, 1, big); on_tick(n, f, big + 130000u); CHECK(n.topic == T_BL);

  // feed shrinks under the user: sub clamps, detail drops
  n = Nav(); on_press(n, f, 0); on_rotate(n, f, 1, 0); CHECK(n.sub == 1);
  Feed one; load(one, body("g", "", "", "", MAC), 0);
  on_tick(n, one, 1); CHECK(n.sub == 0);
  Feed none = f; none.bl.ok = false; on_tick(n, none, 2); CHECK(!n.detail);
}

static int chord(int y, int h);
static void test_alert_parse() {
  Feed f = with(A_CACHE "," A_WAN, US_OK, 6, 100);
  CHECK(f.nal == 2 && f.ac == 6 && strcmp(f.al[0].k, "cache") == 0 && f.al[0].sv == 'r' && f.al[1].sv == 'a' && f.al[1].a == 700);
  CHECK(strcmp(f.al[0].tp, "sd") == 0 && strstr(f.al[0].m, "93%") != nullptr);
  CHECK(f.us.present && f.us.ok && f.us.h5.has && f.us.h5.p == 96 && f.us.h5.r == 4800 && f.us.wk.p == 40 && f.us.nm == 2);
  CHECK(strcmp(f.us.m[0].n, "Opus") == 0 && f.us.bp == 30.0f && f.us.lm == 900);
  Feed g = green();                                            // older/healthy server: no keys at all
  CHECK(g.nal == 0 && g.ac == 0 && !g.us.present);
  Feed h = with("", R"({"ok":0,"st":0})");
  CHECK(h.us.present && !h.us.ok);
  // more than the cap and junk entries are tolerated
  std::string many;
  for (int i = 0; i < 9; i++) many += std::string(i ? "," : "") + R"({"k":"k)" + std::to_string(i) + R"(","tp":"sd","sv":"r","m":"x"})";
  many += R"(,{"tp":"sd"})";
  Feed m = with(many.c_str());
  CHECK(m.nal == MAX_AL);
}

static void test_gesture() {
  // the long press opens the alert view from EVERY page (overview and detail, every topic) and closes back to the same page
  Feed f = with(A_CACHE "," A_WAN, US_OK, 0, 1000);
  uint32_t t = 2000;
  for (int topic = 0; topic < T_COUNT; topic++) {
    for (int det = 0; det < 2; det++) {
      int cnt = det ? detail_count(f, topic) : 1;
      if (det && cnt == 0) continue;
      for (int sub = 0; sub < cnt; sub++) {
        Nav n; n.topic = topic; n.detail = det; n.sub = sub; n.started = true; n.last_knob = t;
        on_long_press(n, f, t);
        CHECK(n.alert && !n.auto_open && n.av_i == 0);
        on_long_press(n, f, t + 10);
        CHECK(!n.alert && n.topic == topic && n.detail == (bool) det && n.sub == sub);   // back where he was
      }
    }
  }
  // no alerts: nothing happens; a stale list: nothing happens
  Feed g = green(); g.at_ms = 1000; Nav n; on_long_press(n, g, t); CHECK(!n.alert);
  Nav n2; on_long_press(n2, f, 1000 + STALE_MS + 1); CHECK(!n2.alert);
  // short press is untouched outside the view: detail in, detail out
  Nav n3; on_press(n3, f, t); CHECK(n3.detail && !n3.alert); on_press(n3, f, t); CHECK(!n3.detail);
}

static void test_view_stepping() {
  Feed f = with(A_CACHE "," A_SILENT "," A_WAN "," A_USAGE, US_OK, 0, 1000);
  uint32_t t = 2000;
  Nav n; on_rotate(n, f, 1, t); CHECK(n.topic == T_NW);
  on_long_press(n, f, t);
  CHECK(n.alert && n.av_i == 0);
  on_rotate(n, f, 1, t); CHECK(n.av_i == 1 && n.topic == T_NW);     // turn steps alerts, the page underneath is not moved
  on_rotate(n, f, 1, t); on_rotate(n, f, 1, t); CHECK(n.av_i == 3);
  on_rotate(n, f, 1, t); CHECK(n.av_i == 0);                        // wraps
  on_rotate(n, f, -1, t); CHECK(n.av_i == 3);
  // short press: that alert's topic page; Claude usage page for the usage alert
  on_press(n, f, t); CHECK(!n.alert && n.topic == T_US && !n.detail);
  // silent Mac: its own detail page
  Nav m; on_long_press(m, f, t); on_rotate(m, f, 1, t); on_press(m, f, t);
  CHECK(!m.alert && m.topic == T_BL && m.detail && m.sub == 1);
  // view text: topic, count, whole message, active time (alert's own clock plus time since the feed arrived)
  Clock c; View v; Nav k; k.alert = true; k.av_i = 1;
  make_view(f, k, c, 1000 + 70000, v);
  CHECK(strcmp(v.title, "ALERT 2 OF 4") == 0 && strcmp(v.big, "DARKBLOOM") == 0 && v.c_title == C_RED);
  CHECK(strstr(v.l[0], "mini has stopped") != nullptr && strstr(v.l[1], "Darkbloom") != nullptr);
  CHECK(strstr(v.l[3], "active for <2 min") != nullptr);           // 5 s + 70 s
  CHECK(v.badge[0] == 0);                                          // no badge on the alert view itself
  k.av_i = 2; make_view(f, k, c, 1000 + 70000, v); CHECK(v.c_title == C_AMBER && strstr(v.l[3], "13 min") != nullptr);
  // ac larger than the list: the title says 2 OF 1000? it uses the true count
  Feed big = with(A_CACHE "," A_SILENT, nullptr, 9, 1000); k.av_i = 0; make_view(big, k, c, 2000, v);
  CHECK(strcmp(v.title, "ALERT 1 OF 9") == 0);
}

static void test_badge() {
  Clock c; View v; Nav n;
  Feed g = green(); g.at_ms = 1000; make_view(g, n, c, 2000, v); CHECK(v.badge[0] == 0);
  Feed one = with(A_WAN, nullptr, 0, 1000); make_view(one, n, c, 2000, v);
  CHECK(strcmp(v.badge, "! ALERT") == 0 && v.c_badge == C_AMBER);
  Feed two = with(A_CACHE "," A_WAN, nullptr, 0, 1000); make_view(two, n, c, 2000, v);
  CHECK(strcmp(v.badge, "! 2 ALERTS") == 0 && v.c_badge == C_RED);
  Feed many = with(A_CACHE "," A_WAN, nullptr, 12, 1000); make_view(many, n, c, 2000, v); CHECK(strcmp(v.badge, "! 9+ ALERTS") == 0);
  // on every page, including clock and the no-data pages
  for (int topic = 0; topic < T_COUNT; topic++) {
    Nav q; q.topic = topic; Feed f = with(A_CACHE, US_OK, 0, 1000); make_view(f, q, c, 2000, v);
    CHECK(strcmp(v.badge, "! ALERT") == 0);
    f.sd.ok = false; q.detail = false; make_view(f, q, c, 2000, v); CHECK(v.badge[0] != 0);
  }
  // no link: the stale list is not shown
  make_view(two, n, c, 1000 + STALE_MS + 1, v); CHECK(v.badge[0] == 0);
  // the badge pill fits the round screen at the top edge (centre y -204, 116 wide, 26 high)
  CHECK(chord(-204, 26) >= 116);
}

static void test_alert_model() {
  Feed none = green();
  Feed r1 = with(A_SILENT);
  Feed r2 = with(A_SILENT "," A_CACHE);
  Feed am = with(A_WAN);
  uint32_t t = 10000;

  // amber never jumps
  { Nav n; on_feed(n, am, t); CHECK(!n.alert); }

  // a new red jumps from wherever Penn is, remembers the place, shows that alert
  Nav n; on_rotate(n, none, 1, t); CHECK(n.topic == T_NW);
  on_feed(n, r1, t); CHECK(n.alert && n.auto_open && n.av_i == 0 && strcmp(n.av_k, "silent:mini") == 0);
  // seen = snoozed. Leaving brings him back to Network and nothing re-jumps
  on_long_press(n, r1, t + 1000); CHECK(!n.alert && n.topic == T_NW);
  on_feed(n, r1, t + 20000); CHECK(!n.alert);
  on_feed(n, r1, t + SNOOZE_MS - 1000); CHECK(!n.alert);
  // 15 minutes after he saw it: it jumps again
  on_feed(n, r1, t + SNOOZE_MS); CHECK(n.alert);
  on_long_press(n, r1, t + SNOOZE_MS + 5);

  // a NEW red during a snooze jumps at once; the first one is not shown again
  on_feed(n, r2, t + SNOOZE_MS + 100); CHECK(n.alert && strcmp(n.av_k, "cache") == 0);

  // two reds: jumps once to the first unseen one; the other is reachable by turning and counted, no second jump
  Feed two = with(A_CACHE "," A_USAGE, US_OK);
  Nav m; on_feed(m, two, t); CHECK(m.alert && m.av_i == 0);
  on_rotate(m, two, 1, t + 1); CHECK(m.av_i == 1);
  on_long_press(m, two, t + 2); on_feed(m, two, t + 3); CHECK(!m.alert);

  // leaving without ever stepping: the unseen second red stays quiet (no repeated nagging), badge keeps it
  Nav q; on_feed(q, two, t); on_long_press(q, two, t + 1); on_feed(q, two, t + 2); CHECK(!q.alert);

  // amber turning red jumps
  Nav a; on_feed(a, with(R"({"k":"usage:5h","tp":"us","sv":"a","m":"x"})", US_OK), t); CHECK(!a.alert);
  on_feed(a, with(A_USAGE, US_OK), t + 1000); CHECK(a.alert);

  // an alert that clears and comes back is new again, even inside the old snooze window
  Nav b; on_feed(b, r1, t); on_long_press(b, r1, t + 1);
  on_feed(b, none, t + 5000); CHECK(!b.alert);
  on_feed(b, r1, t + 6000); CHECK(b.alert);

  // cleared while the view is open: it closes by itself; home if it opened by itself, back if Penn opened it
  Nav c1; on_rotate(c1, none, 1, t); on_feed(c1, r1, t); CHECK(c1.alert);
  on_feed(c1, none, t + 1000); CHECK(!c1.alert && c1.topic == T_BL && !c1.detail);
  Nav c2; on_rotate(c2, none, 1, t); on_long_press(c2, with(A_WAN, nullptr, 0, t), t); // opened by hand on Network
  CHECK(c2.alert && !c2.auto_open);
  on_feed(c2, none, t + 1000); CHECK(!c2.alert && c2.topic == T_NW);
  // the list shrinks under the view: follows the alert by key, else clamps
  Nav d; on_feed(d, r2, t); on_rotate(d, r2, 1, t + 1); CHECK(d.av_i == 1);
  Feed rest = with(A_CACHE); on_feed(d, rest, t + 2); CHECK(d.alert && d.av_i == 0 && strcmp(d.av_k, "cache") == 0);

  // interplay with the idle timer: an automatic jump waits for Penn, a hand-opened view times out home
  Nav e; on_feed(e, r1, t); on_tick(e, r1, t + 10 * 60000); CHECK(e.alert);
  Nav h; h.started = true; h.last_knob = t; on_long_press(h, am, t + 1); // am is amber-only: live list opens by hand
  h.last_knob = t; on_tick(h, am, t + HOME_AFTER_MS - 1); CHECK(h.alert);
  on_tick(h, am, t + HOME_AFTER_MS); CHECK(!h.alert && h.topic == T_BL);

  // the cause clears and the go-to press has already moved him: nothing happens to his position
  Nav g; on_feed(g, r1, t); on_press(g, r1, t + 1); CHECK(!g.alert && g.topic == T_BL && g.detail && g.sub == 1);
  on_feed(g, none, t + 2); CHECK(g.topic == T_BL && g.detail);

  // usage switched off: the topic leaves the rotation and a parked position falls back home
  Feed on = with(A_WAN, US_OK); Nav u; u.topic = T_EV;
  on_rotate(u, on, 1, t); CHECK(u.topic == T_US);
  on_rotate(u, on, 1, t); CHECK(u.topic == T_CLOCK);
  Feed off = green(); Nav w; w.topic = T_EV; on_rotate(w, off, 1, t); CHECK(w.topic == T_CLOCK);
  u.topic = T_US; on_tick(u, off, t); CHECK(u.topic == T_BL);
}

static void test_usage_views() {
  Clock c; View v;
  Feed f = with(A_USAGE, US_OK, 0, 1000);
  Nav n; n.topic = T_US;
  make_view(f, n, c, 2000, v);
  CHECK(strcmp(v.title, "CLAUDE USAGE") == 0 && strcmp(v.big, "96%") == 0 && v.bar == 96 && v.c_big == C_RED);
  CHECK(strstr(v.l[0], "resets in 80 min") != nullptr && strstr(v.l[1], "week 40%") != nullptr && strstr(v.l[1], "2 d 7 h") != nullptr);
  CHECK(strstr(v.l[2], "full in 15 min") != nullptr);
  n.detail = true; make_view(f, n, c, 2000, v);
  CHECK(strcmp(v.big, "30%/h") == 0 && strstr(v.l[1], "Opus 70%") != nullptr && strstr(v.l[2], "Sonnet 20%") != nullptr);
  // old data and not-yet-reported
  Feed st = with("", R"({"ok":1,"st":1,"h5":{"p":10,"r":600}})", 0, 1000); n.detail = false; make_view(st, n, c, 2000, v);
  CHECK(strstr(v.foot, "old data") != nullptr && v.c_big == C_AMBER);
  Feed nd = with("", R"({"ok":0,"st":0})", 0, 1000); make_view(nd, n, c, 2000, v); CHECK(strcmp(v.big, "NO DATA") == 0);
  // only a weekly number: no 5-hour window right now
  Feed wk = with("", R"({"ok":1,"st":0,"wk":{"p":12,"r":90000}})", 0, 1000); make_view(wk, n, c, 2000, v);
  CHECK(strcmp(v.big, "--") == 0 && strstr(v.l[1], "week 12%") != nullptr);
  // dur() wording matches the server
  char b[24];
  dur(b, sizeof b, 30); CHECK(strcmp(b, "<2 min") == 0); dur(b, sizeof b, 4800); CHECK(strcmp(b, "80 min") == 0);
  dur(b, sizeof b, 3 * 3600 + 1200); CHECK(strcmp(b, "3 h 20 min") == 0); dur(b, sizeof b, 3 * 86400); CHECK(strcmp(b, "3 d") == 0);
  dur(b, sizeof b, 200000); CHECK(strcmp(b, "2 d 7 h") == 0);
}

static void test_wrap() {
  char out[3][CAP_LINE + 1];
  CHECK(wrap_text("Every internet link looks idle. The connection may be down.", CAP_LINE, out, 3) == 3);
  CHECK(strcmp(out[0], "Every internet link looks") == 0 && strcmp(out[2], "down.") == 0);
  CHECK(wrap_text("short", CAP_LINE, out, 3) == 1 && strcmp(out[0], "short") == 0);
  // too long: cut with "..."; one huge word is split
  int nl = wrap_text("aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo pppp qqqq rrrr ssss tttt uuuu vvvv", CAP_LINE, out, 3);
  CHECK(nl == 3 && strlen(out[2]) <= CAP_LINE && strstr(out[2], "...") != nullptr);
  nl = wrap_text("0123456789012345678901234567890123456789", CAP_LINE, out, 3); CHECK(nl == 2 && strlen(out[0]) == CAP_LINE);
}

static size_t width_est(const char *s, int px_per_char) { return strlen(s) * px_per_char; }

// Chord width of the circle (minus the ring) at the label's centre line plus half its height.
static int chord(int y, int h) {
  int d = (y < 0 ? -y : y) + h / 2;
  int r = 240 - 14;
  if (d >= r) return 0;
  double hc = std::sqrt((double) r * r - (double) d * d);
  return (int) (2 * hc);
}

static void check_fit(const View &v, const char *what) {
  // average glyph width of Montserrat at these sizes is about 0.58 of the point size; digits and capitals are wider
  // so use 0.62.
  struct { const char *s; int y, h, font; } rows[] = {
    {v.title, -168, 23, 20}, {v.big, -105, 55, 48}, {v.l[0], -25, 23, 20}, {v.l[1], 17, 23, 20},
    {v.l[2], 59, 23, 20}, {v.l[3], 101, 23, 20}, {v.foot, 160, 18, 16}};
  for (auto &r : rows) {
    int w = (int) width_est(r.s, (int) (r.font * 0.62));
    if (w > chord(r.y, r.h)) { fails++; printf("FAIL too wide (%s): \"%s\" %d > %d\n", what, r.s, w, chord(r.y, r.h)); }
    checks++;
  }
  CHECK(strlen(v.title) <= CAP_TITLE && strlen(v.big) <= CAP_BIG && strlen(v.foot) <= CAP_FOOT);
}

static void test_views() {
  // worst case: 4 macs with long names, 4 links, 16 disks, long reasons
  std::string macs;
  for (int i = 0; i < 4; i++)
    macs += std::string(i ? "," : "") + R"({"n":"studio-mac-m)" + std::to_string(i) + R"(-long","s":"serving","on":1,"w":123,"td":123456,"tm":9999999,"jobs":99,"age":86400,"sl":1})";
  Feed f;
  CHECK(load(f, body("r", "bl", "\"silent:m\"", "\"silent:studio-mac-m0-l\",\"array:STOPPED\",\"parity:err\"", macs.c_str()), 1000));
  f.nw.n = 4;
  for (int i = 0; i < 4; i++) { snprintf(f.nw.l[i].n, sizeof f.nw.l[i].n, "Starlink2"); f.nw.l[i].d = 944.4f; f.nw.l[i].u = 940.1f; for (int k = 0; k < 3; k++) { f.nw.l[i].has_t[k] = true; f.nw.l[i].t[k][0] = 12345.6f; f.nw.l[i].t[k][1] = 9999.9f; } }
  f.sd.par = 1; f.sd.pp = 99; f.sd.n = 16; f.sd.na = 99; f.sd.nwn = 99;
  for (int i = 0; i < 16; i++) { snprintf(f.sd.d[i].n, sizeof f.sd.d[i].n, "disk%d", i + 1); f.sd.d[i].u = 100; f.sd.d[i].t = 55; f.sd.d[i].sp = 0; }
  f.ev.sf = 1;
  f.bl.al = 12;
  Clock c; c.valid = true; strcpy(c.time, "23:59"); strcpy(c.date, "Wed 30 Sep");
  uint32_t now = 2000;

  for (int topic = 0; topic < T_COUNT; topic++) {
    for (int det = 0; det < 2; det++) {
      int cnt = det ? detail_count(f, topic) : 1;
      if (det && cnt == 0) continue;
      for (int sub = 0; sub < cnt; sub++) {
        Nav n; n.topic = topic; n.detail = det; n.sub = sub;
        View v; make_view(f, n, c, now, v);
        char what[48]; snprintf(what, sizeof what, "topic %d detail %d sub %d", topic, det, sub);
        check_fit(v, what);
        CHECK(v.title[0] != 0);
      }
    }
  }
  // the alert view with the longest messages the server can send (90 characters), every position in a full list
  {
    const char *msg[6] = {
      "Cache pool is 99% full and the array is 99% full. Free some space on both right now!",
      "studio-mac-m0-long has stopped serving Darkbloom (no activity for over 30 min) okay.",
      "Every internet link looks idle. The connection may be down. Check the router power.",
      "Claude weekly usage is 100%. Resets in 6 d 23 h. Slow down or the limit will block you.",
      "Tonight's charge will fall short of the target. Raise the limit or plug in earlier.",
      "Unraid has 12 unread alerts and 13 unread warnings that need a look soon, please do."};
    Feed al = f; al.nal = 6; al.ac = 12;
    for (int i = 0; i < 6; i++) { al.al[i] = Alert(); snprintf(al.al[i].k, sizeof al.al[i].k, "k%d", i); strcpy(al.al[i].tp, i == 3 ? "us" : i == 4 ? "ev" : "sd"); al.al[i].sv = i % 2 ? 'a' : 'r'; snprintf(al.al[i].m, sizeof al.al[i].m, "%.90s", msg[i]); al.al[i].a = 9 * 86400; }
    for (int i = 0; i < 6; i++) { Nav n; n.alert = true; n.av_i = i; View v; make_view(al, n, c, now, v); check_fit(v, "alert view"); CHECK(strncmp(v.title, "ALERT", 5) == 0); CHECK(v.l[0][0] != 0); }
    // normal pages with the badge present still fit (the badge is a separate widget at the top edge)
    for (int topic = 0; topic < T_COUNT; topic++) { Nav n; n.topic = topic; View v; make_view(al, n, c, now, v); check_fit(v, "badge page"); CHECK(v.badge[0] != 0); }
  }
  // the usage pages, full of data
  {
    Feed u = with(A_USAGE, R"({"ok":1,"st":1,"h5":{"p":100,"r":17999},"wk":{"p":100,"r":604799},"m":[{"n":"Sonnet4.5xx","p":100,"r":604799},{"n":"Opus","p":99,"r":1},{"n":"Haiku","p":1},{"n":"Fable","p":2,"r":99999}],"bp":100,"lm":0})", 0, 1000);
    for (int det = 0; det < 2; det++) for (int pg = 0; pg < (det ? detail_count(u, T_US) : 1); pg++) {
      Nav n; n.topic = T_US; n.detail = det; n.sub = pg; View v; make_view(u, n, c, 2000, v); check_fit(v, "usage"); CHECK(v.title[0]);
    }
    CHECK(detail_count(u, T_US) == 2);
  }

  // ring colours
  Nav n; View v;
  Feed g = green(); g.at_ms = 1000; make_view(g, n, c, 2000, v); CHECK(v.ring == C_RING_OK);
  Feed a = amber(); a.at_ms = 1000; make_view(a, n, c, 2000, v); CHECK(v.ring == C_AMBER);
  // never-connected: waiting, then no link
  Feed none; make_view(none, n, c, 5000, v); CHECK(strcmp(v.big, "WAIT") == 0);
  make_view(none, n, c, 60000, v); CHECK(strcmp(v.big, "NO LINK") == 0 && v.ring == C_DIM);
  // stale after the window: no link, shows how old, numbers hidden
  make_view(g, n, c, 1000 + STALE_MS + 1, v);
  CHECK(strcmp(v.big, "NO LINK") == 0 && strstr(v.l[1], "ago") != nullptr && v.ring == C_DIM);
  make_view(g, n, c, 1000 + STALE_MS, v); CHECK(strcmp(v.big, "NO LINK") != 0);
  // clock still works with no link (shows time + the note)
  Nav nc; nc.topic = T_CLOCK; make_view(g, nc, c, 1000 + STALE_MS + 1, v);
  CHECK(strcmp(v.big, "23:59") == 0 && strstr(v.l[2], "no link") != nullptr);

  // per-section: no link of one section while others fine
  Feed s = green(); s.at_ms = 1000; s.sd.ok = false;
  Nav ns; ns.topic = T_SD; make_view(s, ns, c, 2000, v);
  CHECK(strcmp(v.big, "NO DATA") == 0);
  s = green(); s.at_ms = 1000; s.nw.wan = false;
  Nav nn; nn.topic = T_NW; make_view(s, nn, c, 2000, v); CHECK(strstr(v.foot, "offline") != nullptr);
  // stale section (last-good copy): amber note
  s = green(); s.at_ms = 1000; s.ev.st = true; Nav ne; ne.topic = T_EV; make_view(s, ne, c, 2000, v);
  CHECK(strstr(v.foot, "old data") != nullptr && v.c_foot == C_AMBER);
  // network labels: rolling vs calendar
  s = green(); s.at_ms = 1000; Nav nd; nd.topic = T_NW; nd.detail = true; make_view(s, nd, c, 2000, v);
  CHECK(strstr(v.l[1], "24 h") != nullptr);
  s.nw.rolling = false; make_view(s, nd, c, 2000, v); CHECK(strstr(v.l[1], "today") != nullptr);
  s.nw.rolling = true; nd.sub = 1; make_view(s, nd, c, 2000, v); CHECK(strstr(v.l[2], "n/a") != nullptr);
  // darkbloom sums over however many Macs
  s = green(); s.at_ms = 1000; Nav nb; make_view(s, nb, c, 2000, v);
  CHECK(strcmp(v.big, "$15.34") == 0);
}

static void test_brightness() {
  Feed f = green(); Nav n;
  CHECK(brightness(f, n, 1000) == DEFAULT_BRIGHTNESS);
  f.br = 5; CHECK(brightness(f, n, 1000) == 5);
  on_rotate(n, f, 1, 2000); CHECK(brightness(f, n, 3000) == 40);    // a touch lifts a dim screen
  CHECK(brightness(f, n, 2000 + WAKE_MS) == 5);                     // ...and it dims again
  f.br = 80; CHECK(brightness(f, n, 3000) == 80);
}

int main() {
  test_parse(); test_alert_parse(); test_nav(); test_gesture(); test_view_stepping(); test_badge(); test_alert_model(); test_usage_views(); test_wrap(); test_views(); test_brightness();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
