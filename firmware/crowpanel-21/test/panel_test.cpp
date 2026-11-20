// Host tests for panel_feed.h / panel_nav.h / panel_scene.h. No LVGL, no ESPHome.
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "panel_scene.h"

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
  on_rotate(n, f, 1, t); CHECK(n.sub == 0);               // one page, stays
  on_rotate(n, f, -1, t); CHECK(n.sub == 0 && n.topic == T_BL);
  on_press(n, f, t); CHECK(!n.detail && n.topic == T_BL); // press = back

  // storage detail is one grid page; network detail has three periods
  CHECK(detail_count(f, T_SD) == 1 && detail_count(f, T_NW) == NW_PERIODS && detail_count(f, T_BL) == 1);
  n = Nav(); on_rotate(n, f, 1, t); on_rotate(n, f, 1, t); CHECK(n.topic == T_SD);
  on_press(n, f, t);
  for (int i = 0; i < 3; i++) on_rotate(n, f, 1, t);
  CHECK(n.sub == 0);
  // Claude and Lab are not in the rotation without their sections
  CHECK(!topic_on(f, T_US) && !topic_on(f, T_LAB) && topic_on(f, T_CLOCK));

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
  n = Nav(); on_rotate(n, f, 1, 0); on_press(n, f, 0); on_rotate(n, f, 1, 0); CHECK(n.topic == T_NW && n.sub == 1);
  Feed none = f; none.nw.ok = false; on_tick(n, none, 2); CHECK(!n.detail);
}

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
  for (int i = 0; i < 15; i++) many += std::string(i ? "," : "") + R"({"k":"k)" + std::to_string(i) + R"(","tp":"sd","sv":"r","m":"x"})";
  many += R"(,{"tp":"sd"})";
  Feed m = with(many.c_str());
  CHECK(m.nal == MAX_AL);
}

static void test_gesture() {
  // the long press opens the alert view from EVERY page (overview and detail, every topic)
  Feed f = with(A_CACHE "," A_WAN, US_OK, 0, 1000);
  uint32_t t = 2000;
  for (int topic = 0; topic < T_COUNT; topic++) {
    if (!topic_on(f, topic)) continue;
    for (int det = 0; det < 2; det++) {
      int cnt = det ? detail_count(f, topic) : 1;
      if (det && cnt == 0) continue;
      for (int sub = 0; sub < cnt; sub++) {
        Nav n; n.topic = topic; n.detail = det; n.sub = sub; n.started = true; n.last_knob = t;
        on_long_press(n, f, t);
        CHECK(n.alert && !n.auto_open && n.av_i == 0);
        on_long_press(n, f, t + 10);                      // snooze the first, show the second
        CHECK(n.alert && n.av_i == 1 && !n.av_done);
        on_long_press(n, f, t + 20);                      // snooze the last: leave, back where he was
        CHECK(!n.alert && n.topic == topic && n.detail == (bool) det && n.sub == sub);
      }
    }
  }
  Feed g = green(); g.at_ms = 1000; Nav n; on_long_press(n, g, t); CHECK(!n.alert);
  Nav n2; on_long_press(n2, f, 1000 + STALE_MS + 1); CHECK(!n2.alert);
  Nav n3; on_press(n3, f, t); CHECK(n3.detail && !n3.alert); on_press(n3, f, t); CHECK(!n3.detail);
}

static void test_view_stepping() {
  Feed f = with(A_CACHE "," A_SILENT "," A_WAN "," A_USAGE, US_OK, 0, 1000);
  uint32_t t = 2000;
  Nav n; on_rotate(n, f, 1, t); CHECK(n.topic == T_NW);
  on_long_press(n, f, t);
  CHECK(n.alert && n.av_i == 0);
  on_rotate(n, f, 1, t); CHECK(n.av_i == 1 && n.topic == T_NW);     // turn steps alerts, the page underneath is not moved
  on_rotate(n, f, 1, t); on_rotate(n, f, 1, t); CHECK(n.av_i == 3 && !n.av_done);
  on_rotate(n, f, 1, t); CHECK(n.av_done);                          // past the last alert: the Done card
  on_rotate(n, f, 1, t); CHECK(!n.av_done && n.av_i == 0);          // and round to the first
  on_rotate(n, f, -1, t); CHECK(n.av_done);                         // backwards from the first reaches Done
  on_rotate(n, f, -1, t); CHECK(!n.av_done && n.av_i == 3);
  on_rotate(n, f, 1, t); CHECK(n.av_done);
  on_press(n, f, t); CHECK(!n.alert && n.topic == T_NW && !n.detail);   // press on Done: back to where he came from
  // detail page underneath is restored too
  Nav d; on_rotate(d, f, 1, t); on_press(d, f, t); on_rotate(d, f, 1, t); CHECK(d.detail && d.sub == 1);
  on_long_press(d, f, t); on_rotate(d, f, -1, t); CHECK(d.av_done); on_long_press(d, f, t);
  CHECK(!d.alert && d.topic == T_NW && d.detail && d.sub == 1);     // long press on Done also leaves
  // press on an alert: that alert's topic overview (also for a silent Mac)
  Nav k; on_long_press(k, f, t); on_press(k, f, t); CHECK(!k.alert && k.topic == T_SD && !k.detail);
  Nav m; on_long_press(m, f, t); on_rotate(m, f, 1, t); on_press(m, f, t); CHECK(!m.alert && m.topic == T_BL && !m.detail);
  // snooze: one alert goes quiet for an hour, the others stay; the marker count follows
  Nav s; on_long_press(s, f, t);
  CHECK(visible_alerts(s, f, t) == 4);
  on_long_press(s, f, t + 1); CHECK(s.alert && s.av_i == 1 && visible_alerts(s, f, t + 2) == 3 && snoozed(s, f, 0, t + 2));
  CHECK(!snoozed(s, f, 0, t + 1 + SNOOZE_MS));                      // an hour later it counts again
  // the idle return still closes a hand-opened view
  Nav z; z.started = true; z.last_knob = t; on_long_press(z, f, t); on_tick(z, f, t + HOME_AFTER_MS); CHECK(!z.alert && z.topic == T_BL);
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
  // long press snoozes it for an hour; an automatic view then goes home and nothing re-jumps
  on_long_press(n, r1, t + 1000); CHECK(!n.alert && n.topic == T_BL);
  on_feed(n, r1, t + 20000); CHECK(!n.alert);
  on_feed(n, r1, t + SNOOZE_MS - 1000); CHECK(!n.alert);
  // an hour after the snooze: it jumps again
  on_feed(n, r1, t + 1000 + SNOOZE_MS); CHECK(n.alert);
  on_long_press(n, r1, t + SNOOZE_MS + 1005);

  // a NEW red during a snooze jumps at once; the first one is not shown again
  on_feed(n, r2, t + SNOOZE_MS + 1100); CHECK(n.alert && strcmp(n.av_k, "cache") == 0);

  // two reds: jumps once to the first unseen one; the other is reachable by turning and counted, no second jump
  Feed two = with(A_CACHE "," A_USAGE, US_OK);
  Nav m; on_feed(m, two, t); CHECK(m.alert && m.av_i == 0);
  on_rotate(m, two, 1, t + 1); CHECK(m.av_i == 1);
  on_long_press(m, two, t + 2); on_feed(m, two, t + 3); CHECK(m.alert && m.av_i == 0);   // snoozed one, the other still counts
  on_long_press(m, two, t + 4); on_feed(m, two, t + 5); CHECK(!m.alert);

  // leaving without ever stepping: the unseen second red stays quiet (no repeated nagging), badge keeps it
  Nav q; on_feed(q, two, t); on_press(q, two, t + 1); on_feed(q, two, t + 2); CHECK(!q.alert);

  // amber turning red jumps
  Nav a; on_feed(a, with(R"({"k":"usage:5h","tp":"us","sv":"a","m":"x"})", US_OK), t); CHECK(!a.alert);
  on_feed(a, with(A_USAGE, US_OK), t + 1000); CHECK(a.alert);

  // an alert that clears and comes back is new again, even inside the old snooze window
  Nav b; on_feed(b, r1, t); on_press(b, r1, t + 1);
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
  Nav g; on_feed(g, r1, t); on_press(g, r1, t + 1); CHECK(!g.alert && g.topic == T_BL && !g.detail);
  on_feed(g, none, t + 2); CHECK(g.topic == T_BL && !g.detail);

  // usage switched off: the topic leaves the rotation and a parked position falls back home
  Feed on = with(A_WAN, US_OK); Nav u; u.topic = T_EV;
  on_rotate(u, on, 1, t); CHECK(u.topic == T_US);
  on_rotate(u, on, 1, t); CHECK(u.topic == T_CLOCK);
  Feed off = green(); Nav w; w.topic = T_EV; on_rotate(w, off, 1, t); CHECK(w.topic == T_CLOCK);
  u.topic = T_US; on_tick(u, off, t); CHECK(u.topic == T_BL);
}

// ---------- scene
static const Item *find_text(const Scene &s, const char *txt) {
  for (int i = 0; i < s.n; i++) if (s.it[i].k == K_TEXT && strcmp(s.it[i].s, txt) == 0) return &s.it[i];
  return nullptr;
}
static bool has_text(const Scene &s, const char *part) {
  for (int i = 0; i < s.n; i++) if (s.it[i].k == K_TEXT && strstr(s.it[i].s, part)) return true;
  return false;
}
static int count_kind(const Scene &s, int k) { int c = 0; for (int i = 0; i < s.n; i++) if (s.it[i].k == k) c++; return c; }
static Scene sc_of(const Feed &f, const Nav &n, uint32_t now, int br = 60, const Clock &c = Clock()) {
  static Scene s; make_scene(f, n, c, now, br, s); return s;
}
// text anywhere below the 15 px floor or outside the round screen would be a bug
static void check_fit(const Scene &s, const char *what) {
  for (int i = 0; i < s.n; i++) {
    const Item &t = s.it[i];
    if (t.k == K_TEXT) {
      if (t.font > F15) { printf("bad font %s\n", what); fails++; }
      float cx = 240, dy = t.y - 240;
      (void) cx;
      if (t.y < 40 || t.y > 450) { printf("FAIL %s: text '%s' at y %d\n", what, t.s, t.y); fails++; }
      (void) dy;
    }
  }
  CHECK(s.n < MAXI && s.ng <= MAXG);
}

static void test_scene() {
  Clock ck; ck.valid = true; ck.t = 1763063880;   // some time in Nov 2025
  uint32_t now = 1000;
  Feed g = green(); g.at_ms = 1000;
  Nav n;
  // every topic, overview and detail, renders and fits
  for (int t = 0; t < T_COUNT; t++) {
    if (!topic_on(g, t)) continue;
    n = Nav(); n.topic = t;
    Scene s = sc_of(g, n, now, 60, ck); check_fit(s, "overview");
    CHECK(s.ring == C_OK && s.ringw == 4);
    CHECK(count_kind(s, K_RECT) >= dot_count(g));   // the page dots
    int dc = detail_count(g, t);
    for (int sub = 0; sub < dc; sub++) { n.detail = true; n.sub = sub; Scene d = sc_of(g, n, now, 60, ck); check_fit(d, "detail"); CHECK(has_text(d, "press to go back")); }
  }
  // Darkbloom overview: hero is the sum of today's earnings, 2 of 2 serving, grid of Macs
  n = Nav();
  Scene s = sc_of(g, n, now, 60, ck);
  CHECK(find_text(s, "$15.34") && has_text(s, "2 OF 2 SERVING") && has_text(s, "this month") && find_text(s, "studio-m4"));
  CHECK(find_text(s, "$15.34")->font == F96);
  // account model: the one line only shows when the feed carries it
  g.bl.md[0] = 0; CHECK(!has_text(sc_of(g, n, now, 60, ck), "SERVING GEMMA"));
  snprintf(g.bl.md, sizeof g.bl.md, "gemma-3-27b"); s = sc_of(g, n, now, 60, ck); CHECK(has_text(s, "SERVING gemma") || has_text(s, "SERVING GEMMA"));
  g.bl.md[0] = 0;
  // Darkbloom detail: no per-Mac model, one status line each
  n.detail = true; s = sc_of(g, n, now, 60, ck);
  CHECK(has_text(s, "serving") && has_text(s, "$12.34") && !has_text(s, "gemma"));

  // Network overview: down/up per link, footer
  n = Nav(); n.topic = T_NW; s = sc_of(g, n, now, 60, ck);
  CHECK(find_text(s, "312") && find_text(s, "23") && find_text(s, "41") && find_text(s, "8.0") && has_text(s, "MBPS"));
  n.detail = true; s = sc_of(g, n, now, 60, ck); CHECK(has_text(s, "Last 24 h") && has_text(s, "TOTAL"));
  n.sub = 2; s = sc_of(g, n, now, 60, ck); CHECK(has_text(s, "Last 30 days") && find_text(s, "300 GB"));

  // Storage: 74% hero, and the amber disk pill when a disk is nearly full
  n = Nav(); n.topic = T_SD; s = sc_of(g, n, now, 60, ck);
  CHECK(find_text(s, "74") && find_text(s, "%") && has_text(s, "71.0 of 96.0 TB") && has_text(s, "6 DISKS") && has_text(s, "All disks OK"));
  g.sd.d[1].u = 96; s = sc_of(g, n, now, 60, ck); CHECK(has_text(s, "disk2 96% full"));
  g.sd.d[1].u = 80;

  // EV: percent hero, limit tick, pill by state
  n = Nav(); n.topic = T_EV; s = sc_of(g, n, now, 60, ck);
  CHECK(find_text(s, "54") && has_text(s, "limit 80%") && has_text(s, "Plugged in"));
  bool tick = false; for (int i = 0; i < s.n; i++) if (s.it[i].k == K_ARC && s.it[i].rnd == 2) tick = true;
  CHECK(tick);
  g.ev.ch = 1; CHECK(has_text(sc_of(g, n, now, 60, ck), "Charging")); g.ev.ch = 0;
  g.ev.plug = 0; CHECK(has_text(sc_of(g, n, now, 60, ck), "Unplugged")); g.ev.plug = 1;
  g.ev.soc = 80; CHECK(has_text(sc_of(g, n, now, 60, ck), "At limit")); g.ev.soc = 54;
  // EV detail: missing values show dashes and keep the rows; range missing -> grey dash hero
  n.detail = true; s = sc_of(g, n, now, 60, ck);
  CHECK(has_text(s, "RATE") && has_text(s, "TO 80%") && find_text(s, "—") && find_text(s, "—")->col == C_IDLE);
  g.ev.rg = 152; g.ev.ch = 1; g.ev.kw = 7.2f; g.ev.mf = 125; s = sc_of(g, n, now, 60, ck);
  CHECK(find_text(s, "152") && find_text(s, "7.2") && find_text(s, "2:05") && find_text(s, "152")->col == C_WHITE);
  g.ev.rg = -1; g.ev.ch = 0; g.ev.kw = -1; g.ev.mf = -1;

  // Claude, Lab (when present), Clock
  Feed u = with(A_USAGE, US_OK, 0, 1000);
  n = Nav(); n.topic = T_US; s = sc_of(u, n, now, 60, ck);
  CHECK(find_text(s, "96") && has_text(s, "resets in 1 h 20 m") && has_text(s, "WEEK") && find_text(s, "40%") && has_text(s, "resets "));
  n.detail = true; s = sc_of(u, n, now, 60, ck);
  CHECK(has_text(s, "Week · all") && has_text(s, "Week · Opus") && has_text(s, "BURN") && has_text(s, "limit in 15 m"));
  n = Nav(); n.topic = T_CLOCK; s = sc_of(g, n, now, 60, ck);
  CHECK(has_text(s, ":") && (find_text(s, "AM") || find_text(s, "PM")) && has_text(s, "2025") == false && has_text(s, "BLOOM") && has_text(s, "DISKS") && !has_text(s, "LAB"));
  Clock none; s = sc_of(g, n, now, 60, none); CHECK(has_text(s, "Waiting for the time"));

  // Lab shows once the feed has the lb section
  Feed lb = green(); lb.at_ms = 1000; lb.lb.present = lb.lb.ok = true; lb.lb.n = 3; lb.lb.q = 7; lb.lb.r = 2;
  snprintf(lb.lb.i[0].t, sizeof lb.lb.i[0].t, "Review run 12"); lb.lb.i[0].a = 7200; lb.lb.i[0].w = true;
  CHECK(topic_on(lb, T_LAB) && dot_count(lb) == dot_count(g) + 1);
  n = Nav(); n.topic = T_LAB; s = sc_of(lb, n, now, 60, ck);
  CHECK(find_text(s, "3") && has_text(s, "waiting on you") && find_text(s, "7") && has_text(s, "in model queue"));
  n.detail = true; s = sc_of(lb, n, now, 60, ck); CHECK(has_text(s, "Review run 12") && find_text(s, "2 h") && has_text(s, "7 · 2 running"));
}

// ---- many links and many alerts (feed with N links / N alerts)
static std::string nw_json(int n, bool totals) {
  std::string l;
  for (int i = 0; i < n; i++) {
    char b[160];
    snprintf(b, sizeof b, "%s{\"n\":\"L%d\",\"d\":%d.5,\"u\":%d.2%s}", i ? "," : "", i, i * 10, i,
             totals ? ",\"t1\":[100,20],\"t7\":[700,140],\"t30\":[3000,600]" : "");
    l += b;
  }
  return R"("nw":{"ok":1,"src":"wan","tp":"r","l":[)" + l + "]}";
}
static Feed feed_with(int links, int alerts, char sv = 'a') {
  std::string s = body("g", "", "", "");
  size_t a = s.find("\"nw\":{"), z = s.find(",\"sd\":{");
  s = s.substr(0, a) + nw_json(links, true) + s.substr(z);
  if (alerts) {
    std::string al = ",\"al\":[";
    for (int i = 0; i < alerts; i++) {
      char b[160];
      snprintf(b, sizeof b, "%s{\"k\":\"k%d\",\"tp\":\"nw\",\"sv\":\"%c\",\"m\":\"Alert number %d.\",\"a\":60}", i ? "," : "", i, sv, i);
      al += b;
    }
    al += "]";
    s.insert(s.find(",\"bl\":"), al);
  }
  Feed f; load(f, s, 1000); f.at_ms = 1000;
  return f;
}
static int count_font(const Scene &s, int font) { int c = 0; for (int i = 0; i < s.n; i++) if (s.it[i].k == K_TEXT && s.it[i].font == font) c++; return c; }

static void test_links() {
  Clock ck; ck.valid = true; ck.t = 1763063880;
  for (int n : {3, 4, 5, 6, 9}) {
    Feed f = feed_with(n, 0);
    CHECK(f.nw.n == n);   // every link the feed has is kept
    Nav nv; nv.topic = T_NW;
    Scene s = sc_of(f, nv, 2000, 60, ck); check_fit(s, "links overview");
    char nm[8];
    if (n == 3) { CHECK(count_font(s, F40) == 3); }
    else { CHECK(count_font(s, F40) == 0); CHECK(n > 5 || find_text(s, "L1") != nullptr); }
    CHECK(has_text(s, "MBPS"));
    if (n <= 5) CHECK(!has_text(s, "MORE"));
    if (n == 6) CHECK(has_text(s, "+2 MORE"));
    if (n == 9) {   // the 4 busiest (L8..L5) in that order, never the quiet ones
      CHECK(has_text(s, "+5 MORE"));
      for (int i = 5; i < 9; i++) { snprintf(nm, sizeof nm, "L%d", i); CHECK(find_text(s, nm)); }
      CHECK(!find_text(s, "L0") && !find_text(s, "L4"));
      CHECK(find_text(s, "L8")->y < find_text(s, "L5")->y);
    }
    // detail pages list the links; 4 or more links turn the big title into a small label
    for (int p = 0; p < 3; p++) {
      nv.detail = true; nv.sub = p;
      Scene d = sc_of(f, nv, 2000, 60, ck); check_fit(d, "links detail");
      static const char *big[] = {"Last 24 h", "Last 7 days", "Last 30 days"}, *small[] = {"LAST 24 H", "LAST 7 DAYS", "LAST 30 DAYS"};
      CHECK(has_text(d, n >= 4 ? small[p] : big[p]));
      CHECK(!has_text(d, n >= 4 ? big[p] : small[p]));
      CHECK(find_text(d, "L1") != nullptr && has_text(d, "press to go back"));
      if (n == 5) { CHECK(find_text(d, "L4") != nullptr); CHECK(count_font(d, F40) == 0); }
      if (n == 9) { CHECK(find_text(d, "L6") && !find_text(d, "L7")); }   // 7 rows fit inside the circle
      for (int i = 0; i < d.n; i++) if (d.it[i].k == K_TEXT) CHECK(d.it[i].y <= 372 + 20);
    }
  }
}

static void test_alert_dots() {
  Clock ck; ck.valid = true; ck.t = 1763063880;
  for (int n : {1, 7, 8, 12}) {
    Feed f = feed_with(3, n);
    CHECK(f.nal == n);
    Nav nv; on_feed(nv, f, 1000);
    on_long_press(nv, f, 2000);
    Scene s = sc_of(f, nv, 2500, 60, ck); check_fit(s, "alert dots");
    char t[16]; snprintf(t, sizeof t, "1 OF %d", n);
    CHECK(has_text(s, t));
    int dots = 0, hollow = 0, wide = 0;
    for (int i = 0; i < s.n; i++) if (s.it[i].k == K_RECT && s.it[i].h == 10) { dots++; if (s.it[i].bw == 2) hollow++; if (s.it[i].w == 22) wide++; }
    if (n <= 7) { CHECK(dots == n + 1 && hollow == 1 && wide == 1); } else CHECK(dots == 0);
    // turn past the last alert: the Done card, with its own hollow dot current
    for (int i = 0; i < n; i++) on_rotate(nv, f, 1, 3000 + i);
    Scene d = sc_of(f, nv, 4000, 60, ck);
    CHECK(has_text(d, "Done"));
    dots = 0; hollow = 0; wide = 0;
    for (int i = 0; i < d.n; i++) if (d.it[i].k == K_RECT && d.it[i].h == 10) { dots++; if (d.it[i].bw == 2) hollow++; if (d.it[i].w == 22) wide++; }
    if (n <= 7) { CHECK(dots == n + 1 && hollow == 1 && wide == 1); } else CHECK(dots == 0);
  }
}

static void test_states() {
  Clock ck; ck.valid = true; ck.t = 1763063880;
  Feed g = green(); g.at_ms = 1000;
  Nav n;
  // ring colours and widths
  Feed a = amber(); a.at_ms = 1000; Scene s = sc_of(a, n, 2000, 60, ck); CHECK(s.ring == C_WARN && s.ringw == 6);
  Feed r = with(A_SILENT, nullptr, 0, 1000); r.lvl = 'r'; s = sc_of(r, n, 2000, 60, ck); CHECK(s.ring == C_BAD && s.ringw == 10);
  // no connection: grey ring, cloud, text; alerts hidden; also on detail pages
  s = sc_of(g, n, 1000 + STALE_MS + 1, 60, ck);
  CHECK(s.ring == C_IDLE && s.ringw == 4 && has_text(s, "No connection") && has_text(s, "Can't reach the dashboard") && has_text(s, "LAST DATA"));
  Feed never; s = sc_of(never, n, 5000, 60, ck); CHECK(has_text(s, "No connection") && has_text(s, "RETRYING"));
  // no data
  Feed nd = g; nd.sd.ok = false; n.topic = T_SD; s = sc_of(nd, n, 2000, 60, ck);
  CHECK(has_text(s, "No data yet") && has_text(s, "UNRAID HASN'T REPORTED") && find_text(s, "—") && find_text(s, "—")->col == C_IDLE);
  CHECK(dot_count(nd) == dot_count(g));   // all dots stay: each dot always means the same topic
  // old data: grey values, pill text
  Feed od = g; od.sd.st = true; s = sc_of(od, n, 2000, 60, ck); CHECK(has_text(s, "OLD DATA") && find_text(s, "74") && find_text(s, "74")->col == C_LABEL);
  // alert marker: count and colour, hidden with none, shifts down on arc screens
  s = sc_of(g, n, 2000, 60, ck); bool pill = false; for (int i = 0; i < s.ng; i++) if (s.g[i].pill && s.g[i].ph == 30) pill = true; CHECK(!pill);
  Feed two = with(A_CACHE "," A_WAN, nullptr, 0, 1000);
  Nav b; s = sc_of(two, b, 2000, 60, ck);
  CHECK(s.ng >= 1 && s.g[0].pill && s.g[0].pbg == C_BAD && s.g[0].py == 52 && find_text(s, "2")->col == C_BG);
  b.topic = T_SD; s = sc_of(two, b, 2000, 60, ck); CHECK(s.g[0].py == 56);
  // snoozing removes it from the count
  Nav sn; on_long_press(sn, two, 2000); on_long_press(sn, two, 2001);
  s = sc_of(two, sn, 2002, 60, ck); CHECK(sn.alert);
  // alert view content
  Nav v; on_long_press(v, two, 2000); s = sc_of(two, v, 2000, 60, ck);
  CHECK(s.ring == C_BAD && s.ringw == 10 && has_text(s, "1 OF 2") && has_text(s, "ALERT") && find_text(s, "Cache 93% full") && has_text(s, "STORAGE · ACTIVE") && has_text(s, "snooze 1 h") && has_text(s, "hold"));
  on_rotate(v, two, 1, 2000); s = sc_of(two, v, 2000, 60, ck); CHECK(s.ring == C_WARN && has_text(s, "2 OF 2") && has_text(s, "WARNING") && find_text(s, "No internet"));
  on_rotate(v, two, 1, 2000); s = sc_of(two, v, 2000, 60, ck); CHECK(find_text(s, "Done") && has_text(s, "press to go back") && s.ring == C_OK);
  // a message is clipped to three lines, never wider than the screen
  for (int i = 0; i < s.n; i++) CHECK(s.it[i].k != K_TEXT || s.it[i].w <= 380);
  // night layout: hero plus one line, all white, no tracks or hints; ring stays
  Nav q; s = sc_of(g, q, 2000, 3, ck);
  CHECK(s.night && find_text(s, "$15.34") && has_text(s, "Darkbloom today") && s.ringw == 4 && !has_text(s, "press for detail") && count_kind(s, K_ARC) == 0);
  for (int i = 0; i < s.n; i++) CHECK(s.it[i].k != K_TEXT || s.it[i].col == C_WHITE);
  q.topic = T_SD; s = sc_of(g, q, 2000, 5, ck); CHECK(count_kind(s, K_ARC) == 0 && find_text(s, "74"));
  s = sc_of(g, q, 2000, 6, ck); CHECK(!s.night && count_kind(s, K_ARC) > 0);
  // the page changes the key (the UI fades), the same page keeps it
  Nav k1, k2; k2.topic = T_NW;
  CHECK(sc_of(g, k1, 2000).key != sc_of(g, k2, 2000).key);
  uint32_t kk = sc_of(g, k1, 2000).key; CHECK(kk == sc_of(g, k1, 3000).key);
}

static void test_system() {
  Scene s; Sys y;
  y.mode = SYS_BOOT; snprintf(y.version, sizeof y.version, "1.5.0"); make_system(y, s);
  CHECK(has_text(s, "DESK PANEL") && has_text(s, "Starting") && has_text(s, "v1.5.0") && count_kind(s, K_ARC) == 1);
  y.mode = SYS_WIFI; snprintf(y.ap, sizeof y.ap, "crowpanel-21 setup"); make_system(y, s);
  CHECK(has_text(s, "Wi-Fi setup") && has_text(s, "On your phone, join") && find_text(s, "crowpanel-21 setup") && has_text(s, "THEN FOLLOW"));
  y.mode = SYS_OTA; y.pct = 64; make_system(y, s);
  CHECK(has_text(s, "UPDATING") && find_text(s, "64") && has_text(s, "DON'T UNPLUG") && count_kind(s, K_ARC) == 2);
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
  test_parse(); test_alert_parse(); test_nav(); test_gesture(); test_view_stepping(); test_alert_model(); test_scene(); test_states(); test_system(); test_brightness();
  test_links();
  test_alert_dots();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
