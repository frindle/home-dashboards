#!/usr/bin/env python3
"""Sample feeds (all made up) and the list of screens to render. Writes feeds/*.json and manifest.tsv.
Usage: shots.py OUT_DIR. Standard library only. Actions per shot are the knob calls: R+ R- (turn), P (press), L (hold)."""
import copy, json, os, sys

def mac(n, td, tm, w, jobs, age, s="serving", on=1, sl=0):
    return dict(n=n, s=s, on=on, sl=sl, w=w, td=td, tm=tm, jobs=jobs, age=age)

def disk(n, r, u, sp, t):
    return dict(n=n, r=r, u=u, sp=sp, t=t)

def green():
    return dict(
        v=1, lvl="g", go="", rr=[], why=[],
        bl=dict(ok=1, st=0, al=0, m=[mac("studio-a", 1234, 45600, 22, 3, 12), mac("mini-c", 612, 21800, 9, 0, 5)]),
        nw=dict(ok=1, st=0, src="wan", tp="r", l=[
            dict(n="Fiber", d=412.6, u=38.2, t1=[18.4, 3.1], t7=[96.0, 14.2], t30=[412.0, 61.5]),
            dict(n="LTE backup", d=9.4, u=2.1, t1=[0.8, 0.2])]),
        sd=dict(ok=1, st=0, tot=dict(s=96000, u=71000), nd=6, spin=2, arr="STARTED", par=0, pp=0, pe=0, na=0, nwn=0, d=[
            disk("disk1", "d", 71, 1, 34), disk("disk2", "d", 64, 0, 31), disk("disk3", "d", 58, 0, 30),
            disk("disk4", "d", 12, 0, 29), disk("disk5", "d", 40, 1, 33), disk("cache", "c", 55, 1, 40)]),
        ev=dict(ok=1, st=0, plug=1, soc=54, lim=80, amps=16, tgt=80, kwh=12.5, h=6.5, sf=0, wk=1),
        us=dict(ok=1, st=0, h5=dict(p=37, r=10800), wk=dict(p=12, r=400000),
                m=[dict(n="Opus", p=20, r=400000), dict(n="Sonnet", p=9, r=400000)], bp=8),
        al=[], ac=0)

def alert(k, tp, sv, m, a):
    return dict(k=k, tp=tp, sv=sv, m=m, a=a)

F = {}
F["green"] = green()

a = green(); a["lvl"] = "a"; a["why"] = ["cache:84%", "full:disk3"]
a["bl"]["al"] = 1
a["nw"]["src"] = "unraid"
a["sd"]["tot"]["u"] = 83000; a["sd"]["nwn"] = 2
a["sd"]["d"][2] = disk("disk3", "d", 96, 0, 30); a["sd"]["d"][5] = disk("cache", "c", 84, 1, 44)
a["ev"].update(soc=31, h=3.0, sf=1)
a["us"].update(h5=dict(p=82, r=5400), lm=2700, bp=41)
a["us"]["m"] = [dict(n="Opus", p=61, r=400000), dict(n="Sonnet", p=44, r=400000), dict(n="Haiku", p=8, r=400000), dict(n="Fable", p=3, r=400000)]
a["al"] = [alert("cache", "sd", "a", "The cache pool is 84% full. Worth freeing space soon.", 2400),
           alert("ev", "ev", "a", "The EV will be short of its target tonight (31% now, 80% wanted, 3 h left).", 900),
           alert("usage:5h", "us", "a", "Claude 5-hour usage is 82%. Resets in 90 min.", 300)]
a["ac"] = 3
F["amber"] = a

a1 = green(); a1["lvl"] = "a"
a1["al"] = [alert("wan", "nw", "a", "Every internet link looks idle. The connection may be down.", 700)]; a1["ac"] = 1
F["amber1"] = a1

r1 = green(); r1["lvl"] = "r"; r1["rr"] = ["silent:mini-c"]
r1["bl"]["m"][1] = mac("mini-c", 0, 21800, 2, 0, 2400, s="silent", on=0, sl=1)
r1["al"] = [alert("silent:mini-c", "bl", "r", "mini-c has stopped serving Darkbloom (no activity for over 30 min).", 1900)]; r1["ac"] = 1
F["red1"] = r1

r3 = green(); r3["lvl"] = "r"; r3["rr"] = ["cache:93%", "usage"]; r3["why"] = ["cache:93%"]
r3["sd"]["d"][5] = disk("cache", "c", 93, 1, 47); r3["sd"]["nwn"] = 1
r3["us"].update(h5=dict(p=96, r=4800), lm=900, bp=30)
r3["al"] = [alert("cache", "sd", "r", "The cache pool is 93% full (1.9 TB of 2 TB). Move data off it before the next backup run.", 5400),
            alert("usage:5h", "us", "r", "Claude 5-hour usage is 96%. Resets in 80 min.", 120),
            alert("wan", "nw", "a", "Every internet link looks idle. The connection may be down.", 700)]; r3["ac"] = 3
F["red3"] = r3

o = green()
for k in ("bl", "nw", "sd", "ev", "us"): o[k]["st"] = 1
F["old"] = o

nd = green()
for k in ("bl", "nw", "sd", "ev", "us"): nd[k] = dict(ok=0, st=0)
F["nodata"] = nd

nb = green(); nb["bl"] = dict(ok=0, st=0)
F["nodata_bl"] = nb

f4 = green()
f4["bl"]["m"] = [mac("studio-a", 1234, 45600, 22, 3, 12), mac("mini-c", 612, 21800, 9, 0, 5),
                 mac("laptop-b", 380, 9100, 14, 1, 40), mac("desk-d", 95, 2300, 6, 0, 8, s="idle", on=0)]
F["fourmacs"] = f4

d16 = green()
d16["sd"].update(nd=16, spin=5, tot=dict(s=192000, u=128000))
d16["sd"]["d"] = [disk("parity", "p", 0, 1, 33)] + [disk("disk%d" % i, "d", 20 + i * 5, int(i % 3 != 0), 28 + i % 9) for i in range(1, 15)] + [disk("cache", "c", 55, 1, 41)]
F["disks16"] = d16

nt = green(); nt["br"] = 5
F["night"] = nt

w = green()
w["bl"]["m"] = [mac("studio-m4-ultra", 123456, 4567800, 1234, 12, 3), mac("mini-c", 612, 21800, 9, 0, 5)]
w["nw"]["l"] = [dict(n="Fiber-10G-wan", d=1234.5, u=987.6, t1=[1234.5, 456.7], t7=[8765.4, 1234.5], t30=[23456.7, 5432.1]),
                dict(n="LTE backup", d=9.4, u=2.1), dict(n="Starlink", d=88.8, u=9.9)]
w["sd"].update(tot=dict(s=192000, u=190500), arr="STOPPING", par=1, pp=87)
w["us"].update(h5=dict(p=100, r=100000), wk=dict(p=100, r=500000), lm=200000)
F["worst"] = w

# ---- shots: (slug, feed, actions, description, age seconds since the feed arrived, note)
S = []
def shot(slug, feed, actions="", desc="", age=5, note=""):
    S.append((slug, feed, actions, desc, age, note))

TOP = {"bl": "", "nw": "R+", "sd": "R+ R+", "ev": "R+ R+ R+", "us": "R+ R+ R+ R+", "clock": "R+ R+ R+ R+ R+"}
def at(topic, detail=False, extra=""):
    return " ".join(x for x in (TOP[topic], "P" if detail else "", extra) if x)

# green
shot("darkbloom-overview-green", "green", at("bl"), "all green, 2 Macs serving")
shot("darkbloom-detail-green", "green", at("bl", True), "Mac 1 of 2")
shot("network-overview-green", "green", at("nw"), "2 links, WAN monitor online")
shot("network-detail-green", "green", at("nw", True), "link 1 with 24 h / 7 d / 30 d")
shot("network-detail-partial-green", "green", at("nw", True, "R+"), "link 2: only the 24 h window known (n/a rows)")
shot("storage-overview-green", "green", at("sd"), "73% used, bar blue")
shot("storage-detail-array-green", "green", at("sd", True), "array page")
shot("storage-detail-disks-green", "green", at("sd", True, "R+"), "disks page 1 of 1 (6 disks)")
shot("ev-overview-green", "green", at("ev"), "plugged in, on plan")
shot("ev-detail-green", "green", at("ev", True), "tonight's plan")
shot("claude-overview-green", "green", at("us"), "37% of 5 h, week 12%")
shot("claude-detail-green", "green", at("us", True), "burn rate and per-model limits")
shot("clock-green", "green", at("clock"), "clock page, all ok")
# amber
shot("darkbloom-overview-amber", "amber", at("bl"), "open alert, amber badge (3 alerts)")
shot("network-overview-amber", "amber", at("nw"), "monitor offline footer, amber badge")
shot("storage-overview-amber", "amber", at("sd"), "cache 84% and full disk: amber, amber badge")
shot("storage-detail-array-amber", "amber", at("sd", True), "array page with warnings")
shot("storage-detail-disks-amber", "amber", at("sd", True, "R+"), "disk3 and cache amber")
shot("ev-overview-amber", "amber", at("ev"), "short of target, amber bar")
shot("ev-detail-amber", "amber", at("ev", True), "plan, short of target")
shot("claude-overview-amber", "amber", at("us"), "82% and 'full in 45 min'")
shot("claude-detail-amber", "amber", at("us", True), "burn rate, 4 models page 1")
shot("claude-detail-models2-amber", "amber", at("us", True, "R+"), "4 models, page 2")
shot("clock-amber", "amber", at("clock"), "clock page with reasons")
shot("badge-amber-single-home", "amber1", at("bl"), "one amber alert: '! ALERT' badge on home")
shot("badge-amber-single-storage", "amber1", at("sd"), "one amber alert badge on storage")
shot("badge-amber-single-detail", "amber1", at("nw", True), "amber badge on a detail page")
shot("alert-view-single-amber", "amber1", "L", "alert view, one amber alert (opened by hold)")
shot("alert-view-three-amber-1", "amber", "L", "alert view 1 of 3, all amber")
shot("alert-view-three-amber-3", "amber", "L R+ R+", "alert view 3 of 3, all amber")
# red
shot("alert-view-single-red", "red1", "", "one red alert (jumped by itself): silent Mac")
shot("alert-view-single-red-opens-mac", "red1", "P", "press in the alert view opens the silent Mac's page")
shot("badge-red-single-home", "red1", "L", "red '! ALERT' badge on home, silent Mac shown")
shot("badge-red-single-network", "red1", "L R+", "red badge on network")
shot("badge-red-single-detail", "red1", "L P", "red badge on a Mac detail page")
shot("alert-view-three-red-1", "red3", "", "3 alerts, view 1 of 3 (red, long 3-line message)")
shot("alert-view-three-red-2", "red3", "R+", "3 alerts, 2 of 3 (red)")
shot("alert-view-three-red-3", "red3", "R+ R+", "3 alerts, 3 of 3 (amber)")
shot("badge-red-three-home", "red3", "L", "'! 3 ALERTS' red badge on home")
shot("badge-red-three-storage", "red3", "L R+ R+", "red badge on storage, cache red")
shot("storage-detail-disks-red", "red3", "L R+ R+ P R+", "cache 93% red on the disks page, red badge")
shot("claude-overview-red", "red3", "L R+ R+ R+ R+", "96% red bar with pace, red badge")
shot("claude-detail-red", "red3", "L R+ R+ R+ R+ P", "burn rate red, red badge")
# old / no link / no data / boot
shot("darkbloom-overview-olddata", "old", at("bl"), "dashboard serves last-good copy: amber 'old data'")
shot("network-overview-olddata", "old", at("nw"), "old data")
shot("storage-overview-olddata", "old", at("sd"), "old data")
shot("storage-detail-disks-olddata", "old", at("sd", True, "R+"), "old data on a detail page")
shot("ev-overview-olddata", "old", at("ev"), "old data")
shot("claude-overview-olddata", "old", at("us"), "old data (reporter silent)")
shot("nolink-home-after-loss", "green", at("bl"), "feed 90 s old: NO LINK, last data 1 min ago, dark ring", age=90)
shot("nolink-storage-detail-after-loss", "green", at("sd", True, "R+"), "feed 100 s old while on a detail page (storage): NO LINK", age=100)
shot("nolink-with-alerts-hidden", "red3", "", "alerts hidden when the feed is stale: alert view not shown", age=100)
shot("nolink-never-connected", "-", "", "booted, no feed yet, 60 s after boot: NO LINK, no data yet", age=60)
shot("nolink-clock", "green", at("clock"), "clock page with stale feed", age=300)
shot("boot-wait", "-", "", "first 45 s after boot, before the first feed: WAIT", age=8)
shot("nodata-darkbloom", "nodata", at("bl"), "ledger section down")
shot("nodata-network", "nodata", at("nw"), "network section down")
shot("nodata-storage", "nodata", at("sd"), "storage section down")
shot("nodata-ev", "nodata", at("ev"), "EV section down")
shot("nodata-claude", "nodata", at("us"), "usage reporter silent")
shot("nodata-darkbloom-only-home", "nodata_bl", at("bl"), "only Darkbloom down, rest ok")
# night
shot("night-dimmed-home", "night", at("bl"), "feed asks for 5% backlight (night). Rendered at normal brightness.", note="night")
shot("night-dimmed-storage", "night", at("sd"), "same, storage. Rendered at normal brightness.", note="night")
# 4 macs, 16 disks
shot("darkbloom-overview-4macs", "fourmacs", at("bl"), "4 Macs, one idle")
shot("darkbloom-detail-4macs-1", "fourmacs", at("bl", True), "Mac 1 of 4")
shot("darkbloom-detail-4macs-4", "fourmacs", at("bl", True, "R- "), "Mac 4 of 4, idle")
shot("storage-overview-16disks", "disks16", at("sd"), "16 disks, 192 TB array")
shot("storage-detail-array-16disks", "disks16", at("sd", True), "array page, 5 pages total")
shot("storage-detail-disks1-16disks", "disks16", at("sd", True, "R+"), "disks 1 of 4")
shot("storage-detail-disks4-16disks", "disks16", at("sd", True, "R- "), "disks 4 of 4 (last page, includes cache)")
# worst-case strings
shot("worst-darkbloom-overview", "worst", at("bl"), "stress: very large earnings and watts")
shot("worst-darkbloom-detail", "worst", at("bl", True), "stress: longest Mac name (15 chars)")
shot("worst-network-overview", "worst", at("nw"), "stress: 3 links, 4-digit Mbps, longest link name")
shot("worst-network-detail", "worst", at("nw", True), "stress: TB rows and 4-digit speeds")
shot("worst-storage-overview", "worst", at("sd"), "stress: 99% used, parity check running")
shot("worst-storage-detail-array", "worst", at("sd", True), "stress: array stopping, parity")
shot("worst-claude-overview", "worst", at("us"), "stress: 100% and a long 'full in' time")

def main(out):
    fd = os.path.join(out, "feeds")
    os.makedirs(fd, exist_ok=True)
    for k, v in F.items():
        json.dump(v, open(os.path.join(fd, k + ".json"), "w"), separators=(",", ":"))
    with open(os.path.join(out, "manifest.tsv"), "w") as m, open(os.path.join(out, "shots.json"), "w") as j:
        rows = []
        for i, (slug, feed, act, desc, age, note) in enumerate(S, 1):
            name = "%02d-%s" % (i, slug)
            m.write("\t".join([name, feed, str(age), act, ""]) + "\n")
            rows.append(dict(name=name, feed=feed, actions=act, desc=desc, age=age, note=note))
        json.dump(rows, j, indent=1)
    print("%d shots, %d feeds" % (len(S), len(F)))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
