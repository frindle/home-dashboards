#!/usr/bin/env python3
"""Sample feeds (all made up; the numbers are the ones on the designer's artboards) and the list of screens to render.
Writes feeds/*.json, manifest.tsv and shots.json. Usage: shots.py OUT_DIR. Standard library only.
Actions per shot are the knob calls: R+ R- (turn), P (press), L (hold 600 ms). Column 5 holds preview options."""
import copy, json, os, sys

def mac(n, td, tm, w, jobs, age, s="serving", on=1, sl=0):
    return dict(n=n, s=s, on=on, sl=sl, w=w, td=td, tm=tm, jobs=jobs, age=age)

def disk(n, r, u, sp, t):
    return dict(n=n, r=r, u=u, sp=sp, t=t)

def design():
    """Every number from the artboards. Today 13 Nov 2026, 9:58 (the clock is fixed in the harness)."""
    return dict(
        v=1, lvl="g", go="", rr=[], why=[],
        bl=dict(ok=1, st=0, al=0, m=[mac("studio-a", 612, 9000, 22, 3, 12), mac("mini-c", 480, 5400, 9, 1, 5),
                                     mac("laptop-b", 257, 2395, 8, 0, 40, s="idle"), mac("desk-d", 0, 200, 0, 0, 10800, s="silent", on=0, sl=0)]),
        nw=dict(ok=1, st=0, src="wan", tp="r", l=[
            dict(n="Cox", d=4.2, u=0.7, t1=[143, 31], t7=[917, 466], t30=[4880, 1790]),
            dict(n="Starlink", d=0.0, u=0.0, t1=[0, 0], t7=[0.1, 0.1], t30=[0.5, 0.6]),
            dict(n="Unraid", d=27, u=7.9, t1=[373, 113], t7=[2550, 946], t30=[12300, 3980])]),
        sd=dict(ok=1, st=0, tot=dict(s=62000, u=34700), nd=6, spin=2, arr="STARTED", par=1, pp=52, pe=0, na=0, nwn=0, d=[
            disk("parity", "p", 0, 1, 33), disk("disk1", "d", 96, 1, 34), disk("disk2", "d", 64, 0, 31), disk("disk3", "d", 58, 0, 30),
            disk("disk4", "d", 12, 0, 29), disk("cache", "c", 55, 1, 40)]),
        ev=dict(ok=1, st=0, plug=1, soc=45, lim=70, ch=1, rg=152, kw=7.4, mf=85),
        us=dict(ok=1, st=0, h5=dict(p=62, r=6480), wk=dict(p=41, r=342120),
                m=[dict(n="Opus", p=28, r=342120), dict(n="Sonnet", p=19, r=342120)], bp=9),
        lb=dict(ok=1, st=0, age=30, q=7, r=2, i=[dict(t="Review eval results", a=7200, w=1), dict(t="Approve training run", a=18000, w=0),
                                                  dict(t="Pick prompt variant", a=86400, w=0)]),
        al=[], ac=0)

def alert(k, tp, sv, m, a):
    return dict(k=k, tp=tp, sv=sv, m=m, a=a)

F = {}
F["design"] = design()

d = design(); del d["lb"]
F["nolab"] = d                                    # no Lab feature yet: Lab hidden, six dots

a3 = design(); a3["lvl"] = "a"
a3["al"] = [alert("full", "sd", "a", "Disk disk1 is 96% full.", 8040),
            alert("usage:5h", "us", "a", "Claude 5-hour usage is 82%. Resets in 90 min.", 300),
            alert("failover", "nw", "a", "Cox looks down. Traffic is on Starlink.", 900)]
a3["ac"] = 3
F["alerts3"] = a3

a1 = design(); a1["lvl"] = "a"
a1["al"] = [alert("wan", "nw", "a", "No traffic on any internet link. The internet may be down.", 700)]; a1["ac"] = 1
F["alert1"] = a1

r1 = design(); r1["lvl"] = "r"
r1["bl"]["m"][3] = mac("desk-d", 0, 200, 0, 0, 10800, s="silent", on=0, sl=1)
r1["al"] = [alert("silent:desk-d", "bl", "r", "desk-d has stopped serving Darkbloom (no activity for over 30 min).", 1900)]; r1["ac"] = 1
F["red1"] = r1

r3 = design(); r3["lvl"] = "r"
r3["sd"]["d"][5] = disk("cache", "c", 93, 1, 47)
r3["us"]["h5"] = dict(p=96, r=4800); r3["us"]["lm"] = 900; r3["us"]["bp"] = 30
r3["al"] = [alert("cache", "sd", "r", "Cache pool is 93% full. Run the mover or free space.", 5400),
            alert("usage:5h", "us", "r", "Claude 5-hour usage is 96%. Resets in 80 min.", 120),
            alert("wan", "nw", "a", "No traffic on any internet link. The internet may be down.", 700)]; r3["ac"] = 3
F["red3"] = r3

o = design()
for k in ("bl", "nw", "sd", "ev", "us", "lb"): o[k]["st"] = 1
F["old"] = o

nd = design()
for k in ("bl", "nw", "sd", "ev", "us", "lb"): nd[k] = dict(ok=0, st=0)
F["nodata"] = nd

def macs(n):
    base = [mac("studio-a", 612, 9000, 22, 3, 12), mac("mini-c", 480, 5400, 9, 1, 5),
            mac("laptop-b", 257, 2395, 8, 0, 40, s="idle"), mac("desk-d", 0, 200, 0, 0, 10800, s="silent", on=0)]
    f = design(); f["bl"]["m"] = base[:n]
    return f
for n in (1, 2, 3):
    F["macs%d" % n] = macs(n)

e = design(); e["ev"].update(ch=0, plug=0, rg=88, kw=-1, mf=-1); del e["ev"]["kw"]; del e["ev"]["mf"]
F["ev_unplugged"] = e
e = design(); e["ev"].update(soc=70, ch=0, plug=1, rg=213); del e["ev"]["kw"]; del e["ev"]["mf"]
F["ev_limit"] = e
e = design(); e["ev"].update(ch=0, plug=1); del e["ev"]["rg"]; del e["ev"]["kw"]; del e["ev"]["mf"]
F["ev_norange"] = e
e = design(); e["ev"].update(ch=1); del e["ev"]["kw"]; del e["ev"]["mf"]
F["ev_nokw"] = e

d16 = design()
d16["sd"].update(nd=16, spin=5, tot=dict(s=192000, u=128000), pp=0, par=0)
d16["sd"]["d"] = [disk("parity", "p", 0, 1, 33)] + [disk("disk%d" % i, "d", 20 + i * 5, int(i % 3 != 0), 28 + i % 9) for i in range(1, 15)] + [disk("cache", "c", 55, 1, 41)]
F["disks16"] = d16

w = design()
w["bl"]["m"] = [mac("studio-m4-ultra", 123456, 4567800, 1234, 12, 3), mac("mini-c", 612, 21800, 9, 0, 5)]
w["nw"]["l"] = [dict(n="Fiber-10G-wan", d=1234.5, u=987.6, t1=[1234.5, 456.7], t7=[8765.4, 1234.5], t30=[23456.7, 5432.1]),
                dict(n="LTE backup", d=9.4, u=2.1), dict(n="Starlink", d=88.8, u=9.9)]
w["sd"].update(tot=dict(s=192000, u=190500), arr="STOPPING", par=1, pp=87)
w["us"].update(h5=dict(p=100, r=100000), wk=dict(p=100, r=500000), lm=200000)
w["lb"]["i"][0]["t"] = "Review the very long eval results report"
F["worst"] = w

def nlinks(n):
    f = design()
    names = ["Cox", "Starlink", "Unraid", "Verizon", "Lumen", "Comcast", "Frontier", "Spectrum", "T-Mobile"]
    speeds = [(4.2, 0.7), (0.0, 0.0), (27, 7.9), (12.4, 3.1), (88.0, 12.5), (1.2, 0.3), (64.5, 9.9), (2.2, 0.4), (150.3, 20.8)]
    t = [([143, 31], [917, 466], [4880, 1790]), ([0, 0], [0.1, 0.1], [0.5, 0.6]), ([373, 113], [2550, 946], [12300, 3980])] * 3
    f["nw"]["l"] = [dict(n=names[i], d=speeds[i][0], u=speeds[i][1], t1=t[i][0], t7=t[i][1], t30=t[i][2]) for i in range(n)]
    return f
for n in (4, 5, 6, 9):
    F["links%d" % n] = nlinks(n)

def nlinks_period(n):
    """n links whose order of data used differs between 24 h, 7 d and 30 d."""
    f = design()
    names = ["Cox", "Starlink", "Unraid", "Verizon", "Lumen", "Comcast", "Frontier", "Spectrum", "T-Mobile", "Mint", "Visible", "Ting"]
    ls = []
    for i in range(n):
        t1 = [round(20 + ((i * 5) % n) * 40.5, 1), round(5 + i * 3.1, 1)]
        t7 = [round(100 + (n - i) * 210.3, 1), round(40 + (n - i) * 55.5, 1)]
        t30 = [round(300 + ((i * 7) % n) * 640.7, 1), round(90 + ((i * 7) % n) * 130.2, 1)]
        ls.append(dict(n=names[i], d=round(1.5 + i * 3.7, 1), u=round(0.4 + i * 0.6, 1), t1=t1, t7=t7, t30=t30))
    f["nw"]["l"] = ls
    return f
for n in (8, 12):
    F["linksp%d" % n] = nlinks_period(n)

def nalerts(n):
    f = design(); f["lvl"] = "a"
    f["al"] = [alert("k%d" % i, "nw", "a", "No traffic on link %d. Check it." % (i + 1), 600 + i * 60) for i in range(n)]
    f["ac"] = n
    return f
for n in (1, 7, 8, 12):
    F["alerts%dx" % n] = nalerts(n)

# ---- shots: (slug, feed, actions, description, age seconds since the feed arrived, options)
S = []
def shot(slug, feed, actions="", desc="", age=5, opts=""):
    S.append((slug, feed, actions, desc, age, opts))

def top(topic, lab=True):
    order = ["bl", "nw", "sd", "ev", "us"] + (["lab"] if lab else []) + ["clock"]
    return " ".join(["R+"] * order.index(topic))
def at(topic, detail=False, extra="", lab=True):
    return " ".join(x for x in (top(topic, lab), "P" if detail else "", extra) if x)

# the 24 artboards
shot("alert-view", "alerts3", "L", "Alert: 1 of 3, disk1 96% full, amber ring 10 px, hint lines")
shot("boot", "-", "", "Boot: spinner, DESK PANEL, version", opts="sys=boot")
shot("claude-detail", "design", at("us", True), "Claude detail: three bars, burn and pace")
shot("darkbloom-detail", "design", at("bl", True), "Darkbloom detail: one status line per Mac (no model row)")
shot("dim", "design", "", "Dim night layout at 1% brightness", opts="b=1")
shot("ev-detail", "design", at("ev", True), "EV detail: range, rate, time to limit")
shot("firmware-updating", "-", "", "Firmware: updating 64%", opts="sys=ota pct=64")
shot("lab-detail", "design", at("lab", True), "Lab detail: three items and the model queue")
shot("main-darkbloom-overview", "design", "", "Main: Darkbloom overview, 2 of 4 serving")
shot("network-today", "design", at("nw", True), "Network detail, last 24 h")
shot("network-week", "design", at("nw", True, "R+"), "Network detail, last 7 days")
shot("network-month", "design", at("nw", True, "R+ R+"), "Network detail, last 30 days")
shot("overview-claude", "design", at("us"), "Claude overview: 62%, week 41%")
shot("overview-clock", "design", at("clock"), "Clock: 9:58 AM, status grid")
shot("overview-ev", "design", at("ev"), "EV overview: charging, limit tick at 70%")
shot("overview-lab", "design", at("lab"), "Lab overview: 3 waiting, 7 in queue")
shot("overview-network", "design", at("nw"), "Network overview: three links")
shot("overview-storage", "design", at("sd"), "Storage overview: 56%, disk1 96% full")
shot("state-nodata", "nodata", at("sd"), "State: No data yet (Storage)")
shot("state-offline", "design", at("sd"), "State: No connection, last data 12 min ago", age=720)
shot("state-stale", "old", at("sd"), "State: old data (grey values, grey arc)")
shot("storage-detail", "design", at("sd", True), "Storage detail: disk tiles and parity footer")
shot("wifi-setup", "-", "", "Wi-Fi setup: join the hotspot", opts="sys=wifi")

# status ring and alert marker
shot("ring-ok", "design", at("bl"), "Ring green 4 px, no marker")
shot("ring-warning-marker", "alerts3", at("bl"), "Ring amber 6 px, amber marker with 3")
shot("ring-warning-single", "alert1", at("nw"), "One amber alert: marker with 1")
shot("ring-alert-view", "red1", "", "Ring red 10 px: a new red alert opens the view by itself")
shot("ring-alert-marker", "red3", "L R+ R+ R+ R+ R+ R+ R+", "Red marker on the clock after leaving the view")
shot("alert-view-red", "red3", "", "Red alert, 1 of 3 (cache 93% full)")
shot("alert-view-2", "red3", "R+", "Alert 2 of 3 (red, Claude 96%)")
shot("alert-view-3", "red3", "R+ R+", "Alert 3 of 3 (amber)")
shot("alert-done", "red3", "R+ R+ R+", "Done card after the last alert")
shot("alert-snoozed", "alerts3", "L L", "After a snooze: 1 of 2 left, marker would show 2")
shot("marker-on-arc", "alerts3", at("sd"), "Marker sits at y 56 on arc screens")

# Macs 1 to 4
for n in (1, 2, 3):
    shot("macs-%d" % n, "macs%d" % n, "", "Darkbloom overview with %d Mac%s" % (n, "" if n == 1 else "s"))
shot("macs-4", "design", "", "Darkbloom overview with 4 Macs (one silent)")
shot("macs-4-detail", "design", at("bl", True), "Darkbloom detail with 4 Macs")
shot("darkbloom-red-silent", "red1", "L", "Silent Mac: red dot, offline line", opts="")

# EV states
shot("ev-charging", "design", at("ev"), "EV charging (blue pill, bolt)")
shot("ev-unplugged", "ev_unplugged", at("ev"), "EV unplugged (neutral pill)")
shot("ev-at-limit", "ev_limit", at("ev"), "EV at limit (green pill)")
shot("ev-detail-unplugged", "ev_unplugged", at("ev", True), "EV detail unplugged: dashes keep the rows")
shot("ev-detail-nokw", "ev_nokw", at("ev", True), "EV detail charging, rate and time unknown: dashes")
shot("ev-detail-norange", "ev_norange", at("ev", True), "EV detail with no range: grey dash hero")

# lab hidden, topics, old data
shot("lab-hidden-clock", "nolab", at("clock", lab=False), "Lab not built yet: six dots, six status cells")
shot("lab-hidden-ev", "nolab", at("ev", lab=False), "Lab hidden: dots after EV")
shot("old-darkbloom", "old", "", "Old data: Darkbloom")
shot("old-network", "old", at("nw"), "Old data: Network")
shot("old-ev", "old", at("ev"), "Old data: EV")
shot("old-claude", "old", at("us"), "Old data: Claude")
shot("old-lab", "old", at("lab"), "Old data: Lab")
shot("nodata-darkbloom", "nodata", "", "No data: Darkbloom")
shot("nodata-ev", "nodata", at("ev"), "No data: EV")
shot("nodata-lab", "nodata", at("lab"), "No data: Lab")
shot("offline-clock", "design", at("clock"), "No connection does not hide the clock", age=300)
shot("offline-never", "-", "", "No feed ever arrived", age=60, opts="noclock")

# night layouts
shot("night-darkbloom", "design", "", "Night: Darkbloom", opts="b=3")
shot("night-network", "design", at("nw"), "Night: Network", opts="b=3")
shot("night-storage", "design", at("sd"), "Night: Storage", opts="b=3")
shot("night-ev", "design", at("ev"), "Night: EV", opts="b=3")
shot("night-claude", "design", at("us"), "Night: Claude", opts="b=3")
shot("night-lab", "design", at("lab"), "Night: Lab", opts="b=3")
shot("night-clock", "design", at("clock"), "Night: Clock", opts="b=3")
shot("night-marker", "red3", "L R+ R+ R+ R+ R+ R+ R+", "Night with an alert: ring and marker stay", opts="b=3")

# crowded and long
shot("disks-16", "disks16", at("sd", True), "16 disks (4 by 4 tiles)")
shot("disks-16-overview", "disks16", at("sd"), "16 disks overview")
shot("worst-darkbloom", "worst", "", "Long Mac name, big amounts")
shot("worst-network", "worst", at("nw"), "Three links with 4-digit speeds")
shot("worst-network-detail", "worst", at("nw", True), "Long names and TB rows")
shot("worst-storage", "worst", at("sd"), "99% used, parity check")
shot("worst-claude", "worst", at("us"), "100% and a long limit time")
shot("worst-lab-detail", "worst", at("lab", True), "Long Lab item")

# more links and more alerts
shot("links-3", "design", at("nw"), "Network overview, 3 links (columns)")
shot("links-4", "links4", at("nw"), "Network overview, 4 links (rows)")
shot("links-5", "links5", at("nw"), "Network overview, 5 links (rows, fits the circle)")
shot("links-6", "links6", at("nw"), "Network overview, 6 links: 4 busiest and +2 MORE")
shot("links-9", "links9", at("nw"), "Network overview, 9 links: 4 busiest and +5 MORE")
shot("links-5-detail", "links5", at("nw", True), "Network detail, 5 links (small LAST 24 H label)")
shot("links-5-detail-week", "links5", at("nw", True, "R+"), "Network detail, 5 links, 7 days")
shot("links-9-detail", "links9", at("nw", True), "Network detail, 9 links: 6 biggest and +3 MORE")
shot("links-8-detail", "linksp8", at("nw", True), "Network detail, 8 links, 24 h: 6 biggest and +2 MORE")
shot("links-8-detail-week", "linksp8", at("nw", True, "R+"), "Network detail, 8 links, 7 days (different order and links)")
shot("links-8-detail-month", "linksp8", at("nw", True, "R+ R+"), "Network detail, 8 links, 30 days")
shot("links-12-detail", "linksp12", at("nw", True), "Network detail, 12 links, 24 h: 6 biggest and +6 MORE")
shot("alerts-1", "alerts1x", "L", "1 alert: one dot plus the hollow Done dot")
shot("alerts-7", "alerts7x", "L R+ R+", "7 alerts: seven dots plus hollow, third current")
shot("alerts-7-done", "alerts7x", "L R+ R+ R+ R+ R+ R+ R+", "7 alerts: the Done card, hollow dot current")
shot("alerts-8", "alerts8x", "L R+ R+", "8 alerts: no dots, 3 OF 8 counter")
shot("alerts-12", "alerts12x", "L", "12 alerts: no dots, 1 OF 12 counter")
shot("alerts-12-done", "alerts12x", "L " + " ".join(["R+"] * 12), "12 alerts: the Done card without dots", age=30)

def main(out):
    fd = os.path.join(out, "feeds")
    os.makedirs(fd, exist_ok=True)
    for k, v in F.items():
        json.dump(v, open(os.path.join(fd, k + ".json"), "w"), separators=(",", ":"))
    with open(os.path.join(out, "manifest.tsv"), "w") as m, open(os.path.join(out, "shots.json"), "w") as j:
        rows = []
        for i, (slug, feed, act, desc, age, opts) in enumerate(S, 1):
            name = "%02d-%s" % (i, slug)
            m.write("\t".join([name, feed, str(age), act, opts]) + "\n")
            rows.append(dict(name=name, feed=feed, actions=act, desc=desc, age=age, note=""))
        json.dump(rows, j)

if __name__ == "__main__":
    main(sys.argv[1])
