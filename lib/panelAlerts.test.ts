import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  ALERT_MSG_MAX, MAX_ALERTS, capAlerts, collectAlerts, durText, parseAlertConfig, sortAlerts, trackSince, type Alert,
} from './panelAlerts.ts';
import { PANEL_MAX_BYTES, alertLevel, buildPanel, makePanelCache, type Section, type Sources } from './panelFeed.ts';

const ok = (x: Record<string, unknown> = {}): Section => ({ ok: 1, st: 0, ...x });
const down = (): Section => ({ ok: 0, st: 0 });
const G = { bl: ok({ m: [{ n: 'm4' }], al: 0 }), nw: ok({ src: 'wan', l: [{ n: 'Cox', d: 5, u: 1 }] }), sd: ok({ arr: 'STARTED', d: [] }), ev: ok() };
const keys = (l: Alert[]) => l.map((a) => `${a.sv}:${a.k}`);

test('a healthy system has no alerts', () => {
  assert.deepEqual(collectAlerts(G), []);
});

test('red and amber alerts per topic, with plain messages', () => {
  const l = collectAlerts({
    ...G,
    bl: ok({ m: [{ n: 'm4', sl: 1 }], al: 1 }),
    sd: ok({ arr: 'STOPPED', pe: 2, na: 1, d: [{ n: 'cache', r: 'c', u: 93 }, { n: 'disk1', r: 'd', u: 97 }] }),
    ev: ok({ sf: 1 }),
  });
  assert.deepEqual(keys(l), ['r:array', 'r:cache', 'r:parity', 'r:unraid', 'r:silent:m4', 'a:full:disk1', 'a:ev']);
  assert.match(l.find((a) => a.k === 'array')!.m!, /STOPPED/);
  assert.match(l.find((a) => a.k === 'silent:m4')!.m!, /m4 has stopped serving/);
  assert.ok(l.every((a) => a.m!.length <= ALERT_MSG_MAX && a.m!.length > 10));
});

test('amber variants and the bloom alert only when no Mac is silent', () => {
  assert.deepEqual(keys(collectAlerts({ ...G, bl: ok({ m: [], al: 2 }) })), ['a:bloom']);
  assert.deepEqual(keys(collectAlerts({ ...G, bl: ok({ m: [{ n: 'm4', sl: 1 }], al: 2 }) })), ['r:silent:m4']);
  assert.deepEqual(keys(collectAlerts({ ...G, sd: ok({ arr: 'STARTED', nwn: 3, d: [{ n: 'cache', r: 'c', u: 85 }] }) })), ['a:cache', 'a:unraid']);
});

test('a down or stale source never raises an alert by itself', () => {
  assert.deepEqual(collectAlerts({ bl: down(), nw: down(), sd: down(), ev: down() }), []);
  assert.deepEqual(collectAlerts({ ...G, us: ok({ st: 1, h5: { p: 99, r: 100 } }) }), []);
});

test('thresholds: array full, cache, disk, defaults and env overrides', () => {
  const sd = (usedGb: number, sizeGb = 1000) => ok({ arr: 'STARTED', tot: { s: sizeGb, u: usedGb }, d: [] });
  assert.deepEqual(collectAlerts({ ...G, sd: sd(850) }), []);
  assert.deepEqual(keys(collectAlerts({ ...G, sd: sd(900) })), ['a:arrayfull']);
  assert.deepEqual(keys(collectAlerts({ ...G, sd: sd(970) })), ['r:arrayfull']);
  const cfg = parseAlertConfig({ PANEL_ARRAY_WARN_PCT: '70', PANEL_ARRAY_RED_PCT: '80', PANEL_CACHE_WARN_PCT: '50', PANEL_CACHE_RED_PCT: '60', PANEL_DISK_WARN_PCT: '85' });
  assert.deepEqual(keys(collectAlerts({ ...G, sd: sd(750) }, cfg)), ['a:arrayfull']);
  assert.deepEqual(keys(collectAlerts({ ...G, sd: sd(850) }, cfg)), ['r:arrayfull']);
  const s2 = ok({ arr: 'STARTED', d: [{ n: 'cache', r: 'c', u: 55 }, { n: 'disk2', r: 'd', u: 86 }, { n: 'parity', r: 'p', u: 99 }] });
  assert.deepEqual(keys(collectAlerts({ ...G, sd: s2 }, cfg)), ['a:cache', 'a:full:disk2']);
  assert.match(collectAlerts({ ...G, sd: s2 }, cfg).find((a) => a.k === 'full:disk2')!.m!, /disk2/);   // parity is never "full"
});

test('config: garbage falls back to defaults and warn stays below red', () => {
  const d = parseAlertConfig({});
  assert.equal(d.cacheWarn, 80); assert.equal(d.cacheRed, 90); assert.equal(d.usageRed, 95); assert.equal(d.capGb, undefined);
  const bad = parseAlertConfig({ PANEL_USAGE_WARN_PCT: 'abc', PANEL_USAGE_RED_PCT: '250', PANEL_DATA_CAP_GB: '-4' });
  assert.equal(bad.usageWarn, 80); assert.equal(bad.usageRed, 95); assert.equal(bad.capGb, undefined);
  const swapped = parseAlertConfig({ PANEL_CACHE_WARN_PCT: '95', PANEL_CACHE_RED_PCT: '90' });
  assert.ok(swapped.cacheWarn < swapped.cacheRed);
});

test('network: all idle, failover, data cap', () => {
  const nw = (l: unknown[], extra = {}) => ok({ src: 'wan', l, ...extra });
  assert.deepEqual(keys(collectAlerts({ ...G, nw: nw([{ n: 'Cox', d: 0, u: 0 }]) })), ['r:wan']);
  const two = [{ n: 'Cox', d: 0, u: 0 }, { n: 'Starlink', d: 30, u: 2 }];
  assert.deepEqual(collectAlerts({ ...G, nw: nw(two) }), []);                       // off until a primary is named
  const cfg = parseAlertConfig({ PANEL_WAN_PRIMARY: 'cox' });
  const fo = collectAlerts({ ...G, nw: nw(two) }, cfg);
  assert.deepEqual(keys(fo), ['a:failover']);
  assert.match(fo[0].m!, /Cox looks down.*Starlink/);
  assert.deepEqual(collectAlerts({ ...G, nw: nw([{ n: 'Cox', d: 4, u: 0 }, { n: 'Starlink', d: 0, u: 0 }]) }, cfg), []);
});

test('network cap percentages', () => {
  const nw = (gb: number) => ({ ...G.nw, l: [{ n: 'Cox', d: 5, u: 1, t30: [gb, 0] }, { n: 'Unraid', d: 1, u: 1, t30: [9000, 0] }] });
  const cfg = parseAlertConfig({ PANEL_DATA_CAP_GB: '1000' });                       // no link named: all WAN links, Unraid excluded
  assert.deepEqual(collectAlerts({ ...G, nw: nw(790) }, cfg), []);
  assert.deepEqual(keys(collectAlerts({ ...G, nw: nw(800) }, cfg)), ['a:cap']);
  assert.deepEqual(keys(collectAlerts({ ...G, nw: nw(960) }, cfg)), ['r:cap']);
  assert.match(collectAlerts({ ...G, nw: nw(960) }, cfg)[0].m!, /96%/);
});

test('Claude usage alerts: thresholds, windows, per model, reset text', () => {
  const us = (x: Record<string, unknown>) => ok(x);
  assert.deepEqual(collectAlerts({ ...G, us: us({ h5: { p: 79, r: 600 }, wk: { p: 50 } }) }), []);
  const l = collectAlerts({ ...G, us: us({ h5: { p: 96, r: 4800 }, wk: { p: 82, r: 200000 }, m: [{ n: 'Opus', p: 99 }] }) });
  assert.deepEqual(keys(l), ['r:usage:5h', 'r:usage:opus', 'a:usage:wk']);
  assert.equal(l[0].tp, 'us');
  assert.equal(l[0].m, 'Claude 5-hour usage is 96%. Resets in 80 min.');
  assert.match(l.find((a) => a.k === 'usage:wk')!.m!, /weekly usage is 82%. Resets in 2 d 7 h/);
  const cfg = parseAlertConfig({ PANEL_USAGE_WARN_PCT: '40', PANEL_USAGE_RED_PCT: '50' });
  assert.deepEqual(keys(collectAlerts({ ...G, us: us({ h5: { p: 45 } }) }, cfg)), ['a:usage:5h']);
});

test('durations read plainly', () => {
  assert.equal(durText(30), 'under 2 min'); assert.equal(durText(600), '10 min'); assert.equal(durText(7200), '2 h');
  assert.equal(durText(4800), '80 min'); assert.equal(durText(3 * 3600 + 1200), '3 h 20 min'); assert.equal(durText(3 * 86400), '3 d');
});

test('order: red first, then storage, Darkbloom, network, usage, EV', () => {
  const l = sortAlerts([
    { k: 'ev', tp: 'ev', sv: 'a', t: 'x', w: 'x', m: 'x' }, { k: 'usage:5h', tp: 'us', sv: 'r', t: 'x', w: 'x', m: 'x' }, { k: 'wan', tp: 'nw', sv: 'a', t: 'x', w: 'x', m: 'x' },
    { k: 'silent:m4', tp: 'bl', sv: 'r', t: 'x', w: 'x', m: 'x' }, { k: 'cache', tp: 'sd', sv: 'r', t: 'x', w: 'x', m: 'x' }, { k: 'bloom', tp: 'bl', sv: 'a', t: 'x', w: 'x', m: 'x' },
  ]);
  assert.deepEqual(keys(l), ['r:cache', 'r:silent:m4', 'r:usage:5h', 'a:bloom', 'a:wan', 'a:ev']);
});

test('since-time: starts at first sight, survives rebuilds, resets when the alert clears', () => {
  const since = new Map<string, number>();
  const A = (k: string): Alert => ({ k, tp: 'sd', sv: 'r', t: 'x', w: 'x', m: 'x' });
  assert.equal(trackSince(since, [A('cache')], 1_000_000)[0].a, 0);
  assert.equal(trackSince(since, [A('cache'), A('array')], 1_000_000 + 125_000)[0].a, 125);
  assert.equal(trackSince(since, [A('cache'), A('array')], 1_000_000 + 125_000)[1].a, 0);
  trackSince(since, [A('array')], 1_000_000 + 200_000);                              // cache cleared
  assert.equal(trackSince(since, [A('cache'), A('array')], 1_000_000 + 300_000).find((x) => x.k === 'cache')!.a, 0);
});

test('cap: most important kept, true count reported', () => {
  const many = Array.from({ length: 15 }, (_, i): Alert => ({ k: `k${i}`, tp: 'sd', sv: 'r', t: 'x', w: 'x', m: 'x' }));
  const c = capAlerts(many);
  assert.equal(c.al.length, MAX_ALERTS); assert.equal(c.ac, 15);
  assert.equal(capAlerts(many.slice(0, 3)).ac, undefined);
});

test('ring level and alert list agree: red alert <=> lvl r, amber only <=> lvl a', () => {
  const cases: Record<string, unknown>[] = [
    {}, { bl: ok({ m: [{ n: 'm4', sl: 1 }] }) }, { sd: ok({ arr: 'STOPPED' }) }, { sd: ok({ arr: 'STARTED', tot: { s: 1000, u: 980 } }) },
    { sd: ok({ arr: 'STARTED', tot: { s: 1000, u: 910 } }) }, { us: ok({ h5: { p: 97 } }) }, { us: ok({ wk: { p: 85 } }) },
    { nw: ok({ src: 'wan', l: [{ n: 'Cox', d: 0, u: 0 }, { n: 'Starlink', d: 9, u: 0 }] }) }, { ev: ok({ sf: 1 }) },
  ];
  const cfg = parseAlertConfig({ PANEL_WAN_PRIMARY: 'Cox' });
  for (const c of cases) {
    const sec = { ...G, ...c } as typeof G & { us?: Section };
    const lvl = alertLevel(sec, cfg).lvl;
    const al = collectAlerts(sec, cfg);
    const expect = al.some((a) => a.sv === 'r') ? 'r' : al.length ? 'a' : 'g';
    assert.equal(lvl, expect, JSON.stringify(c));
  }
  const r = alertLevel({ ...G, us: ok({ h5: { p: 97 } }) });
  assert.equal(r.lvl, 'r'); assert.ok(r.rr!.includes('usage:5h')); assert.ok(r.why.includes('usage:5h'));
});

// ---- feed integration ----
const srcOf = (over: Partial<Record<'bl' | 'nw' | 'sd' | 'ev' | 'us', Section>>): Sources => {
  const all = { ...G, ...over };
  const s: Sources = { bl: async () => all.bl, nw: async () => all.nw, sd: async () => all.sd, ev: async () => all.ev };
  if (over.us) s.us = async () => over.us!;
  return s;
};

test('feed: no alerts means no "al" key, and no "us" key when usage is off (bytes as before)', async () => {
  const b = await buildPanel(srcOf({}), { nowMs: 1_000_000 });
  assert.ok(!('al' in b) && !('ac' in b) && !('us' in b));
  assert.deepEqual(Object.keys(b), ['v', 't', 'lvl', 'why', 'bl', 'nw', 'sd', 'ev']);
});

test('feed: alerts carry key, topic, severity, message, age; the age grows across builds', async () => {
  const since = new Map<string, number>();
  const src = srcOf({ sd: ok({ arr: 'STOPPED', d: [] }) });
  const a = await buildPanel(src, { nowMs: 1_000_000, alertSince: since });
  assert.deepEqual(a.al!.map((x) => [x.k, x.tp, x.sv, x.a]), [['array', 'sd', 'r', 0]]);
  const b = await buildPanel(src, { nowMs: 1_000_000 + 90_000, alertSince: since });
  assert.equal(b.al![0].a, 90);
  assert.equal(b.lvl, 'r');
});

test('feed: many alerts everywhere stays inside the size limit and keeps the red ones', async () => {
  const disks = Array.from({ length: 16 }, (_, i) => ({ n: `disk${i + 1}`, r: i === 0 ? 'c' : 'd', u: 97, sp: 1, t: 40 }));
  const links = Array.from({ length: 4 }, (_, i) => ({ n: `Link${i}`, d: 0, u: 0, t1: [100.5, 20.5], t7: [700.5, 120.5], t30: [3000.5, 500.5] }));
  const macs = Array.from({ length: 4 }, (_, i) => ({ n: `studio-m${i}-long`, on: 0, s: 'offline-ish', w: 0, jobs: 0, td: 12345, tm: 123456, age: 9999, sl: 1 }));
  const worst = {
    bl: ok({ m: macs, al: 9 }), nw: ok({ src: 'wan', l: links }),
    sd: ok({ arr: 'STOPPED-LONG', pe: 5, na: 3, nwn: 2, tot: { s: 100000, u: 99000 }, nd: 16, spin: 16, d: disks }),
    ev: ok({ sf: 1, soc: 20, tgt: 80, plug: 1, amps: 32, kwh: 55.5, h: 7.5, lim: 90, wk: 1 }),
    us: ok({ age: 5, h5: { p: 100, r: 17000 }, wk: { p: 99, r: 500000 }, m: [{ n: 'Opus', p: 99, r: 500000 }, { n: 'Sonnet', p: 98, r: 500000 }], bp: 40, lm: 100 }),
  };
  const cfg = parseAlertConfig({ PANEL_WAN_PRIMARY: 'Link0', PANEL_DATA_CAP_GB: '100' });
  const b = await buildPanel(srcOf(worst), { nowMs: 1_000_000, alertCfg: cfg });
  const size = JSON.stringify(b).length;
  assert.ok(size <= PANEL_MAX_BYTES, `size ${size}`);
  assert.ok(b.al!.length >= 3 && b.al!.length <= MAX_ALERTS);
  assert.ok(b.al!.every((x) => x.sv === 'r'));
  assert.ok(b.ac! >= b.al!.length);
  assert.equal(b.lvl, 'r');
});

test('feed cache: alert age follows the clock across cache rebuilds, thresholds come from the config', async () => {
  let t = 5_000_000;
  const get = makePanelCache(srcOf({ ev: ok({ sf: 1 }) }), 1000, () => t, null, undefined, parseAlertConfig({}));
  assert.equal(JSON.parse(await get()).al[0].a, 0);
  t += 60_000;
  assert.equal(JSON.parse(await get()).al[0].a, 60);
});

test('same-kind alerts become one grouped card; every link idle is red with the places on line 2', () => {
  const l = collectAlerts({ ...G, nw: ok({ src: 'wan', l: [{ n: 'Cox', d: 0, u: 0 }, { n: 'Starlink', d: 0, u: 0 }] }) });
  const w = l.find((a) => a.k === 'wan')!;
  assert.equal(w.sv, 'r'); assert.match(w.t, /^No traffic on 2 links/); assert.equal(w.w, 'Cox · Starlink');
  const d = collectAlerts({ ...G, sd: ok({ arr: 'STARTED', d: [{ n: 'disk1', r: 'd', u: 96 }, { n: 'disk2', r: 'd', u: 97 }] }) });
  const full = d.filter((a) => a.k.startsWith('full'));
  assert.equal(full.length, 1); assert.equal(full[0].c, 2); assert.ok(full[0].t.length <= 40 && full[0].w.length <= 64);
  assert.ok(l.every((a) => a.t && a.w));
});
