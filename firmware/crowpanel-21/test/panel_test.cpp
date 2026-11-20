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

// Red feed helper: reasons are keys, go says where.
static Feed red(const char *go, const char *rr, const char *why = "\"x\"") {
  Feed f; load(f, body("r", go, rr, why), 0); return f;
}
static Feed green() { Feed f; load(f, body("g", "", "", ""), 0); return f; }
static Feed amber() { Feed f; load(f, body("a", "", "", "\"cache:85%\""), 0); return f; }

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

static void test_autoswitch() {
  Feed g = green(), a = amber();
  Feed r1 = red("bl", "\"silent:mini\"", "\"silent:mini\"");
  r1.bl.m[1].sl = true;
  Nav n; uint32_t t = 10000;

  // amber never switches
  on_feed(n, a, t); CHECK(!n.alert);

  // user browsing Network; red arrives -> jump straight to alert, pointing at the silent Mac
  on_rotate(n, g, 1, t);
  CHECK(n.topic == T_NW);
  on_feed(n, r1, t); CHECK(n.alert && n.alert_topic == T_BL && n.alert_sub == 1);
  // alert view shows
  Clock c; View v; r1.at_ms = t; make_view(r1, n, c, t, v);
  CHECK(strcmp(v.title, "ALERT") == 0 && strcmp(v.big, "MAC SILENT") == 0 && v.ring == C_RED);
  // same red again: stays, no change
  on_feed(n, r1, t + 20000); CHECK(n.alert);

  // press: open the detail of the silent Mac and snooze
  on_press(n, r1, t + 30000);
  CHECK(!n.alert && n.topic == T_BL && n.detail && n.sub == 1 && n.snoozed);
  // the same red during the snooze: no jump
  on_feed(n, r1, t + 60000); CHECK(!n.alert);
  on_feed(n, r1, t + 14 * 60000); CHECK(!n.alert);
  // snooze over (15 min from the press): jumps again
  on_feed(n, r1, t + 30000 + SNOOZE_MS); CHECK(n.alert);

  // turn dismisses and snoozes; the turn then acts from where the user was
  n = Nav(); on_rotate(n, g, 1, t); on_feed(n, r1, t);
  on_rotate(n, r1, 1, t + 1000);
  CHECK(!n.alert && n.topic == T_SD);                      // was NW, one step on
  on_feed(n, r1, t + 20000); CHECK(!n.alert);              // snoozed

  // NEW reason during a snooze: immediate jump
  Feed r2 = red("sd", "\"silent:mini\",\"array\"", "\"silent:mini\",\"array:STOPPED\"");
  r2.bl.m[1].sl = true;
  on_feed(n, r2, t + 40000); CHECK(n.alert && n.alert_topic == T_SD);
  // ...and a reason that goes away and comes back is new again
  n = Nav(); on_feed(n, r1, t); on_press(n, r1, t + 1);
  Feed r3 = red("sd", "\"array\"", "\"array:STOPPED\"");
  on_feed(n, r3, t + 100); CHECK(n.alert);                 // array is new
  on_press(n, r3, t + 200);
  on_feed(n, r1, t + 300);                                  // array gone, silent back
  CHECK(n.alert);                                           // silent was dropped from the seen set, so it is new

  // clear: back home if we were the ones who moved the screen
  n = Nav(); on_rotate(n, g, 1, t); on_feed(n, r1, t); CHECK(n.alert);
  on_feed(n, g, t + 20000); CHECK(!n.alert && n.topic == T_BL && !n.detail && !n.snoozed);
  // clear after the user took over the screen: stay where they are
  n = Nav(); on_feed(n, r1, t); on_rotate(n, r1, 1, t + 10);
  on_rotate(n, r1, 1, t + 20);
  int topic = n.topic;
  on_feed(n, g, t + 30000); CHECK(n.topic == topic);
  // but a press that opened the Mac detail from the alert is also user-driven
  n = Nav(); on_feed(n, r1, t); on_press(n, r1, t + 5); CHECK(n.detail);
  on_feed(n, g, t + 25000); CHECK(n.topic == T_BL);         // jumped flag was set by the jump; home is where it ends anyway
  // after clear and a new red spell the snooze is gone
  n = Nav(); on_feed(n, r1, t); on_rotate(n, r1, 1, t + 1); on_feed(n, g, t + 2); on_feed(n, r1, t + 3);
  CHECK(n.alert);
  // red_without_go does not alert
  Feed nogo = r1; nogo.go[0] = 0; n = Nav(); on_feed(n, nogo, t); CHECK(!n.alert);

  // idle timer never pulls an alert away
  n = Nav(); on_feed(n, r1, t); on_tick(n, r1, t + 5 * 60000); CHECK(n.alert);

  // storage alert points at storage; press opens it
  Feed rs = red("sd", "\"cache\"", "\"cache:97%\"");
  n = Nav(); on_feed(n, rs, t); on_press(n, rs, t + 1); CHECK(n.topic == T_SD && n.detail && n.sub == 0);
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
  { Nav n; n.alert = true; n.alert_topic = T_BL; View v; make_view(f, n, c, now, v); check_fit(v, "alert"); CHECK(strcmp(v.title, "ALERT") == 0); }

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
  test_parse(); test_nav(); test_autoswitch(); test_views(); test_brightness();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
