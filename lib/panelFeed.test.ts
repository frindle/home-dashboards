import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  PANEL_MAX_BYTES, LAST_GOOD_MAX_MS, MAX_DISKS, MAX_LINKS, MAX_MACS, alertLevel, brightnessAt, buildPanel, earnedCents, fitBody,
  makePanelCache, parseBloom, parseDimConfig, parseEv, parseFluxCsv, parseNetTotals, parseStorage, parseTotalsMap,
  parseUnraidNet, parseWan, withExtraLink, withNetTotals, type PanelBody, type Section, type Sources,
} from './panelFeed.ts';

const NOW = Date.parse('2026-11-12T20:00:00Z');
const iso = (agoS: number) => new Date(NOW - agoS * 1000).toISOString();

// made-up Influx annotated CSV (two tables, annotation lines, blank line between tables)
const csv = (rows: [string, string, string, string, string, string][]) =>
  '#datatype,string,long,dateTime:RFC3339,string,string,string,string\n#group,false,false,false,true,true,true,true\n' +
  ',result,table,_time,_measurement,_field,_value,name,role\n' +
  rows.map((r, i) => `,_result,${i},${r[0]},${r[1]},${r[2]},${r[3]},${r[4]},${r[5]}`).join('\n') + '\n\n';

const DISK_ROWS: [string, string, string, string, string, string][] = [
  [iso(30), 'unraid_disk', 'fs_size_kb', '8000000000', 'disk1', 'data'],
  [iso(30), 'unraid_disk', 'fs_used_kb', '6000000000', 'disk1', 'data'],
  [iso(30), 'unraid_disk', 'spinning', '0', 'disk1', 'data'],
  [iso(30), 'unraid_disk', 'temp_c', '31', 'disk1', 'data'],
  [iso(30), 'unraid_disk', 'fs_size_kb', '8000000000', 'parity', 'parity'],
  [iso(30), 'unraid_disk', 'fs_used_kb', '8000000000', 'parity', 'parity'],
  [iso(30), 'unraid_disk', 'spinning', '1', 'parity', 'parity'],
  [iso(30), 'unraid_disk', 'fs_size_kb', '1000000000', 'cache', 'cache'],
  [iso(30), 'unraid_disk', 'fs_used_kb', '250000000', 'cache', 'cache'],
  [iso(30), 'unraid_array', 'state', 'STARTED', '', ''],
  [iso(30), 'unraid_array', 'parity_running', '0', '', ''],
  [iso(30), 'unraid_array', 'parity_errors', '0', '', ''],
  [iso(30), 'unraid_array', 'notif_unread_alert', '0', '', ''],
];

test('parseFluxCsv skips annotations and handles multiple tables and quotes', () => {
  const rows = parseFluxCsv(csv(DISK_ROWS.slice(0, 2)) + ',result,table,_time,_value\n,_r,0,t,"a,b"\n');
  assert.equal(rows.length, 3);
  assert.equal(rows[0]._field, 'fs_size_kb');
  assert.equal(rows[2]._value, 'a,b');
});

test('storage: percent used, parity left out of the total, spinning count, array state', () => {
  const s = parseStorage(parseFluxCsv(csv(DISK_ROWS)), Math.floor(NOW / 1000)) as Record<string, any>;
  assert.equal(s.ok, 1);
  assert.equal(s.st, 0);
  assert.deepEqual(s.tot, { s: 9000, u: 6250 });
  assert.equal(s.nd, 3);
  assert.equal(s.spin, 1); // only parity is spinning
  assert.equal(s.arr, 'STARTED');
  const d1 = s.d.find((d: any) => d.n === 'disk1');
  assert.deepEqual([d1.r, d1.u, d1.sp, d1.t], ['d', 75, 0, 31]);
  assert.equal(s.d.find((d: any) => d.n === 'cache').r, 'c');
  assert.ok(!('t' in s.d.find((d: any) => d.n === 'cache'))); // missing numbers are left out
});

test('storage: a two-SSD cache pool is one "cache" row with the pool use, hottest temp, spinning if either is', () => {
  const rows = [
    ...DISK_ROWS.filter((r) => r[4] !== 'cache'),
    [iso(30), 'unraid_disk', 'fs_size_kb', '2000000000', 'cache', 'cache'],
    [iso(30), 'unraid_disk', 'fs_used_kb', '1420000000', 'cache', 'cache'],
    [iso(30), 'unraid_disk', 'temp_c', '33', 'cache', 'cache'],
    [iso(30), 'unraid_disk', 'spinning', '0', 'cache', 'cache'],
    [iso(30), 'unraid_disk', 'temp_c', '36', 'cache2', 'cache'],
    [iso(30), 'unraid_disk', 'spinning', '1', 'cache2', 'cache'],
  ] as typeof DISK_ROWS;
  const s = parseStorage(parseFluxCsv(csv(rows)), Math.floor(NOW / 1000)) as Record<string, any>;
  const caches = s.d.filter((d: any) => d.r === 'c');
  assert.deepEqual(caches, [{ n: 'cache', r: 'c', u: 71, sp: 1, t: 36 }]);
  assert.equal(s.nd, 3);
  assert.deepEqual(s.tot, { s: 10000, u: 7420 }); // pool size counted once
});

test('storage: old Influx points mark the section stale; no rows means ok=0', () => {
  const old = DISK_ROWS.map((r) => [iso(3600), ...r.slice(1)] as typeof r);
  assert.equal(parseStorage(parseFluxCsv(csv(old)), Math.floor(NOW / 1000)).st, 1);
  assert.equal(parseStorage([], 0).ok, 0);
});

test('network: WAN rows, " WAN" suffix trimmed, rounded; junk gives ok=0; Unraid fallback uses br0 in Mbps', () => {
  const w = parseWan([{ iface: 'eth0', name: 'Cox WAN', rx_mbps: 312.4567, tx_mbps: 11.04 }, { iface: 'x', name: 'Starlink WAN', rx_mbps: 90, tx_mbps: 8 }]) as any;
  assert.deepEqual(w.l, [{ n: 'Cox', d: 312.5, u: 11 }, { n: 'Starlink', d: 90, u: 8 }]);
  assert.equal(w.src, 'wan');
  assert.equal(parseWan({ error: 1 }).ok, 0);
  assert.equal(parseWan([]).ok, 0);
  const rows = [
    { _time: iso(10), _measurement: 'unraid_net', _field: 'rx_bps', _value: '50000000', iface: 'br0' },
    { _time: iso(10), _measurement: 'unraid_net', _field: 'tx_bps', _value: '2000000', iface: 'br0' },
    { _time: iso(10), _measurement: 'unraid_net', _field: 'rx_bps', _value: '1', iface: 'docker0' },
  ];
  const u = parseUnraidNet(rows, Math.floor(NOW / 1000)) as any;
  assert.deepEqual([u.src, u.l[0].d, u.l[0].u, u.st], ['unraid', 50, 2, 0]);
});

const LIVE = { at: 1, mac: [
  { machine_id: 'studio-m4', ts: 1, age_s: 60, stale: false, watts: 143.26, darkbloom_state: 'serving', sandbox_jobs: 0 },
  { machine_id: 'studio-m5', ts: 1, age_s: 5000, stale: true, watts: null, darkbloom_state: 'offline', sandbox_jobs: null },
] };

test('bloom: serving state, watts, earnings, silent machines from alerts; earned_micro to cents', () => {
  const s = parseBloom({
    live: LIVE,
    alerts: [{ alert: 'machine-silent', machine_id: 'studio-m5', age_s: 5000 }],
    earned: { 'studio-m4': { day: 523, month: 11890 } },
  }) as any;
  assert.equal(s.ok, 1);
  assert.equal(s.st, 1); // one machine's power minute is stale
  assert.deepEqual(s.m[0], { n: 'studio-m4', on: 1, s: 'serving', w: 143, jobs: 0, td: 523, tm: 11890, age: 60 });
  assert.deepEqual([s.m[1].on, s.m[1].sl, 'w' in s.m[1]], [0, 1, false]);
  assert.equal(s.al, 1);
  assert.equal(earnedCents({ totals: { earned_micro: 5_230_000 } }), 523);
  assert.equal(earnedCents({}), undefined);
  assert.equal(parseBloom({ live: { error: 'x' }, alerts: [] }).ok, 0);
});

test('ev: plan fields, missing ones left out; null gives ok=0', () => {
  const e = parseEv({ plugged: true, soc: 61.4, limit: 80, amps: 24, kwh: 18.26, windowHours: 7.5, targetPct: 78, shortfall: false, isWork: true }) as any;
  assert.deepEqual([e.plug, e.soc, e.lim, e.amps, e.kwh, e.h, e.tgt, e.wk, 'sf' in e], [1, 61, 80, 24, 18.3, 7.5, 78, 1, false]);
  assert.equal(parseEv(null).ok, 0);
});

test('ev: optional range, power, charging and minutes to limit; minutes only while charging', () => {
  const e = parseEv({ plugged: true, soc: 45, rangeMi: 152.4, powerKw: 7.64, charging: true, minutesToLimit: 110 }) as any;
  assert.deepEqual([e.rg, e.kw, e.ch, e.mf], [152, 7.6, 1, 110]);
  const n = parseEv({ plugged: true, soc: 45, charging: false, minutesToLimit: 110 }) as any;
  assert.deepEqual(['rg' in n, 'kw' in n, n.ch, 'mf' in n], [false, false, 0, false]);
  assert.equal('ch' in (parseEv({ plugged: true, soc: 45 }) as any), false);
});

const ok = (extra: Record<string, unknown> = {}): Section => ({ ok: 1, st: 0, ...extra });
const down = (): Section => ({ ok: 0, st: 0 });

function fakeSources(over: Partial<Record<'bl' | 'nw' | 'sd' | 'ev', () => Promise<Section>>> = {}): Sources {
  return {
    bl: async () => ok({ m: [{ n: 'a', on: 1 }], al: 0 }),
    nw: async () => ok({ src: 'wan', l: [{ n: 'Cox', d: 10, u: 1 }] }),
    sd: async () => ok({ arr: 'STARTED', d: [{ n: 'disk1', u: 50 }] }),
    ev: async () => ok({ plug: 1 }),
    ...over,
  };
}

test('aggregator: all ok is green with every section present', async () => {
  const p = await buildPanel(fakeSources(), { nowMs: NOW });
  assert.deepEqual([p.v, p.t, p.lvl, p.why], [1, Math.floor(NOW / 1000), 'g', []]);
  assert.ok([p.bl, p.nw, p.sd, p.ev].every((s) => s.ok === 1 && s.st === 0));
});

test('aggregator: one source down shows ok=0 for it only and the level goes amber', async () => {
  const p = await buildPanel(fakeSources({ nw: async () => { throw new Error('boom'); } }), { nowMs: NOW });
  assert.equal(p.nw.ok, 0);
  assert.ok(p.bl.ok && p.sd.ok && p.ev.ok);
  assert.deepEqual([p.lvl, p.why], ['a', ['src:nw']]);
});

test('aggregator: a slow source times out without holding the others back', async () => {
  const t0 = Date.now();
  const p = await buildPanel(fakeSources({ sd: () => new Promise<Section>(() => {}) }), { nowMs: NOW, timeoutMs: 50 });
  assert.ok(Date.now() - t0 < 1000);
  assert.equal(p.sd.ok, 0);
  assert.equal(p.bl.ok, 1);
});

test('aggregator: a failing source shows its last good copy as stale for a while, then ok=0', async () => {
  const lastGood = new Map();
  await buildPanel(fakeSources(), { nowMs: NOW, lastGood });
  const failing = fakeSources({ ev: async () => { throw new Error('x'); } });
  const p1 = await buildPanel(failing, { nowMs: NOW + 60_000, lastGood });
  assert.deepEqual([p1.ev.ok, p1.ev.st, (p1.ev as any).plug], [1, 1, 1]);
  assert.deepEqual([p1.lvl, p1.why], ['a', ['old:ev']]);
  const p2 = await buildPanel(failing, { nowMs: NOW + LAST_GOOD_MAX_MS + 1000, lastGood });
  assert.equal(p2.ev.ok, 0);
});

test('alert level: red for a silent Mac, a stopped array, parity errors or an unread Unraid alert; amber for the rest', () => {
  const g = { bl: ok({ m: [], al: 0 }), nw: ok({ src: 'wan', l: [{ d: 5, u: 1 }] }), sd: ok({ arr: 'STARTED', d: [] }), ev: ok() };
  assert.equal(alertLevel(g).lvl, 'g');
  assert.deepEqual(alertLevel({ ...g, bl: ok({ m: [{ n: 'm4', sl: 1 }], al: 1 }) }), { lvl: 'r', why: ['silent:m4'], go: 'bl', rr: ['silent:m4'] });
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STOPPED' }) }).lvl, 'r');
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STARTED', pe: 2 }) }).lvl, 'r');
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STARTED', na: 1 }) }).lvl, 'r');
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STARTED', nwn: 1 }) }).lvl, 'a');
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STARTED', d: [{ n: 'disk3', u: 96 }] }) }).lvl, 'a');
  assert.equal(alertLevel({ ...g, bl: ok({ m: [], al: 1 }) }).lvl, 'a'); // ring snapshot stale
  assert.equal(alertLevel({ ...g, nw: ok({ src: 'wan', l: [{ d: 0, u: 0 }] }) }).lvl, 'a');
  assert.equal(alertLevel({ ...g, ev: ok({ sf: 1 }) }).lvl, 'a');
  // cache pool: amber from 80%, red (and jump to storage) from 90%; data disks at 95%+ stay amber
  assert.deepEqual(alertLevel({ ...g, sd: ok({ arr: 'STARTED', d: [{ n: 'cache', r: 'c', u: 85 }] }) }), { lvl: 'a', why: ['cache:85%'] });
  assert.deepEqual(alertLevel({ ...g, sd: ok({ arr: 'STARTED', d: [{ n: 'cache', r: 'c', u: 92 }] }) }), { lvl: 'r', why: ['cache:92%'], go: 'sd', rr: ['cache'] });
  assert.deepEqual(alertLevel({ ...g, sd: ok({ arr: 'STARTED', d: [{ n: 'cache', r: 'c', u: 71 }, { n: 'disk1', r: 'd', u: 96 }] }) }), { lvl: 'a', why: ['full:disk1'] });
  // go points at the first red reason's section; amber never sets it
  assert.equal(alertLevel({ ...g, bl: ok({ m: [{ n: 'm4', sl: 1 }], al: 1 }), sd: ok({ arr: 'STOPPED' }) }).go, 'bl');
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STARTED', pe: 1 }) }).go, 'sd');
  assert.ok(!('go' in alertLevel({ ...g, ev: ok({ sf: 1 }) })));
  assert.ok(alertLevel({ ...g, bl: ok({ m: [{ n: 'a', sl: 1 }, { n: 'b', sl: 1 }], al: 2 }), sd: ok({ arr: 'STOPPED' }), ev: down() }).why.length <= 3);
});

test('body stays under 4 KB with a worst-case realistic load', async () => {
  const disks = Array.from({ length: 14 }, (_, i) => ({ n: `disk${i + 1}`, r: 'd', u: 88, sp: 1, t: 34 }));
  const p = await buildPanel(fakeSources({
    sd: async () => ok({ tot: { s: 110000, u: 90000 }, d: disks, spin: 14, nd: 14, arr: 'STARTED', par: 1, pp: 55, pe: 0, na: 0, nwn: 0 }),
    bl: async () => ok({ m: [0, 1, 2, 3].map((i) => ({ n: `studio-machine-${i}`, on: 1, s: 'serving', w: 180, jobs: 2, td: 12345, tm: 456789, age: 120 })), al: 0 }),
    nw: async () => ok({ src: 'wan', l: [{ n: 'Cox', d: 912.3, u: 41.2 }, { n: 'Starlink', d: 210.5, u: 18.9 }] }),
    ev: async () => ok({ plug: 1, soc: 55, lim: 80, amps: 32, kwh: 21.4, h: 7.5, tgt: 80, sf: 0, wk: 1 }),
  }), { nowMs: NOW });
  assert.ok(JSON.stringify(p).length < PANEL_MAX_BYTES, String(JSON.stringify(p).length));
});

test('cache: serves one body for 15 s, shares an in-flight build, rebuilds after the ttl', async () => {
  let calls = 0;
  let clock = NOW;
  const get = makePanelCache(fakeSources({ ev: async () => { calls++; await new Promise((r) => setTimeout(r, 20)); return ok({ plug: 1 }); } }), 15_000, () => clock);
  const [a, b] = await Promise.all([get(), get()]);
  assert.equal(a, b);
  assert.equal(calls, 1);
  clock += 14_000;
  await get();
  assert.equal(calls, 1);
  clock += 2_000;
  await get();
  assert.equal(calls, 2);
});

// ---------- red reasons (rr) and go ----------

test('rr: stable keys, so a number moving inside one reason is not a new reason', () => {
  const g = { bl: ok({ m: [], al: 0 }), nw: ok({ src: 'wan', l: [{ d: 5, u: 1 }] }), ev: ok() };
  const at = (u: number, na = 0) => alertLevel({ ...g, sd: ok({ arr: 'STARTED', na, d: [{ n: 'cache', r: 'c', u }] }) });
  assert.deepEqual(at(91).rr, ['cache']);
  assert.deepEqual(at(97).rr, ['cache']);          // 91% -> 97% is the same reason
  assert.deepEqual(at(91, 2).rr, ['cache', 'unraid']);
  assert.ok(!('rr' in at(50)));                    // only on red
  const two = alertLevel({ ...g, bl: ok({ m: [{ n: 'm4', sl: 1 }, { n: 'm5', sl: 1 }], al: 2 }), sd: ok({ arr: 'STOPPED', pe: 1 }) });
  assert.deepEqual(two.rr, ['silent:m4', 'silent:m5', 'array', 'parity']);
  assert.equal(two.go, 'bl');
});

test('go: amber never sets it, a silent Mac wins over storage, storage alone goes to sd', () => {
  const g = { bl: ok({ m: [], al: 0 }), nw: ok({ src: 'wan', l: [{ d: 5, u: 1 }] }), sd: ok({ arr: 'STARTED', d: [] }), ev: ok() };
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STARTED', nwn: 3, d: [{ n: 'cache', r: 'c', u: 85 }] }) }).go, undefined);
  assert.equal(alertLevel({ ...g, bl: ok({ m: [{ n: 'm4', sl: 1 }], al: 1 }), sd: ok({ arr: 'STOPPED' }) }).go, 'bl');
  assert.equal(alertLevel({ ...g, sd: ok({ arr: 'STOPPED' }) }).go, 'sd');
  assert.equal(alertLevel({ ...g, bl: down() }).go, undefined);   // a down source is amber, never a jump
});

// ---------- network totals ----------

const TOTALS = {
  '1d': [{ iface: 'fw_wan_eth0', rx_bytes: 226765088637, tx_bytes: 41584803235, total_bytes: 1 }, { iface: 'fw_wan_eth3', rx_bytes: 15298644, tx_bytes: 17149933 }, { iface: 'br0', rx_bytes: 384011062005, tx_bytes: 124511990270 }],
  '7d': [{ iface: 'fw_wan_eth0', rx_bytes: 1.2e12, tx_bytes: 3e11 }, { iface: 'br0', rx_bytes: 2e12, tx_bytes: 5e11 }],
  '14d': [{ iface: 'fw_wan_eth0', rx_bytes: 9e12, tx_bytes: 9e12 }],
  '30d': [{ iface: 'fw_wan_eth0', rx_bytes: 5e12, tx_bytes: 1.5e12 }, { iface: 'fw_wan_eth3', rx_bytes: 'x', tx_bytes: 1 }],
};

test('totals map: default, override, junk entries skipped', () => {
  assert.deepEqual(parseTotalsMap(undefined), { Cox: 'fw_wan_eth0', Starlink: 'fw_wan_eth3', Unraid: 'br0' });
  assert.deepEqual(parseTotalsMap('Cox=a, Unraid = b ,bad,=x'), { Cox: 'a', Unraid: 'b' });
  assert.deepEqual(parseTotalsMap('nonsense'), { Cox: 'fw_wan_eth0', Starlink: 'fw_wan_eth3', Unraid: 'br0' });
});

test('totals: bytes to GB [down, up] per link and period; missing or bad rows are left out; 14d ignored', () => {
  const t = parseNetTotals(TOTALS);
  assert.deepEqual(t.Cox, { t1: [227, 41.6], t7: [1200, 300], t30: [5000, 1500] });
  assert.deepEqual(t.Starlink, { t1: [0.02, 0.02] });          // t30 row has a non-number: skipped, not NaN
  assert.deepEqual(t.Unraid, { t1: [384, 125], t7: [2000, 500] });
  assert.ok(!('t14' in t.Cox));
  assert.deepEqual(parseNetTotals(null), {});
  assert.deepEqual(parseNetTotals({ error: 'x' }), {});
  assert.deepEqual(parseNetTotals(TOTALS, { Cox: 'nope' }), {});
});

test('totals are merged by link name, periods marked rolling, ok/st untouched; no totals leaves the section as it was', () => {
  const wan = parseWan([{ iface: 'eth0', name: 'Cox WAN', rx_mbps: 10, tx_mbps: 1 }, { iface: 'eth3', name: 'Starlink WAN', rx_mbps: 5, tx_mbps: 1 }]);
  const m = withNetTotals(wan, parseNetTotals(TOTALS)) as any;
  assert.deepEqual([m.ok, m.st, m.tp, m.src], [1, 0, 'r', 'wan']);
  assert.deepEqual(m.l[0], { n: 'Cox', d: 10, u: 1, t1: [227, 41.6], t7: [1200, 300], t30: [5000, 1500] });
  assert.equal(withNetTotals(wan, undefined), wan);
  assert.equal(withNetTotals(wan, {}), wan);
  assert.deepEqual(withNetTotals(down(), parseNetTotals(TOTALS)), down());
});

test('Unraid link is appended to the WAN links only, capped at MAX_LINKS', () => {
  const wan = parseWan([{ iface: 'eth0', name: 'Cox WAN', rx_mbps: 10, tx_mbps: 1 }]);
  const un = parseUnraidNet([
    { _time: iso(10), _measurement: 'unraid_net', _field: 'rx_bps', _value: '8000000', iface: 'br0' },
    { _time: iso(10), _measurement: 'unraid_net', _field: 'tx_bps', _value: '1000000', iface: 'br0' },
  ], Math.floor(NOW / 1000));
  assert.deepEqual((withExtraLink(wan, un) as any).l.map((x: any) => x.n), ['Cox', 'Unraid']);
  assert.equal(withExtraLink(wan, undefined), wan);
  assert.equal(withExtraLink(wan, down()), wan);
  assert.equal(withExtraLink(un, un), un);        // not a WAN section: unchanged
  const many = parseWan(Array.from({ length: 15 }, (_, i) => ({ iface: `e${i}`, name: `L${i}`, rx_mbps: 1, tx_mbps: 1 })));
  assert.equal((many as any).l.length, MAX_LINKS);
  assert.equal((withExtraLink(many, un) as any).l.length, MAX_LINKS);
});

// ---------- caps ----------

test('Macs and disks are capped for the firmware, but the disk totals still count every disk', () => {
  const live = { mac: Array.from({ length: 7 }, (_, i) => ({ machine_id: `m${i}`, watts: 10, darkbloom_state: 'serving', age_s: 5 })) };
  assert.equal((parseBloom({ live, alerts: [] }) as any).m.length, MAX_MACS);
  const rows: [string, string, string, string, string, string][] = [];
  for (let i = 1; i <= 20; i++) {
    rows.push([iso(30), 'unraid_disk', 'fs_size_kb', '1000000000', `disk${i}`, 'data'], [iso(30), 'unraid_disk', 'fs_used_kb', '500000000', `disk${i}`, 'data']);
  }
  const s = parseStorage(parseFluxCsv(csv(rows)), Math.floor(NOW / 1000)) as any;
  assert.equal(s.d.length, MAX_DISKS);
  assert.equal(s.nd, 20);
  assert.deepEqual(s.tot, { s: 20000, u: 10000 });
});

// ---------- night dimming ----------

test('dim config: off unless PANEL_NIGHT_HOURS is valid; defaults and clamping', () => {
  assert.equal(parseDimConfig({}), null);
  assert.equal(parseDimConfig({ PANEL_NIGHT_HOURS: 'late' }), null);
  assert.equal(parseDimConfig({ PANEL_NIGHT_HOURS: '25-7' }), null);
  assert.equal(parseDimConfig({ PANEL_NIGHT_HOURS: '7-7' }), null);
  assert.deepEqual(parseDimConfig({ PANEL_NIGHT_HOURS: '22-7' }), { startH: 22, endH: 7, night: 5, day: 60, tz: undefined });
  const c = parseDimConfig({ PANEL_NIGHT_HOURS: ' 23 - 6 ', PANEL_NIGHT_BRIGHTNESS: '0', PANEL_DAY_BRIGHTNESS: '250', PANEL_TZ: 'Not/AZone' })!;
  assert.deepEqual([c.startH, c.endH, c.night, c.day, c.tz], [23, 6, 1, 100, undefined]);   // bad zone dropped
});

test('dim: night window across midnight and within a day, in the configured zone', () => {
  const wrap = parseDimConfig({ PANEL_NIGHT_HOURS: '22-7', PANEL_TZ: 'America/Los_Angeles' })!;
  const la = (h: number) => Date.parse(`2026-11-12T${String(h).padStart(2, '0')}:30:00-08:00`);
  assert.deepEqual([22, 23, 0, 3, 6].map((h) => brightnessAt(wrap, la(h))), [5, 5, 5, 5, 5]);
  assert.deepEqual([7, 12, 21].map((h) => brightnessAt(wrap, la(h))), [60, 60, 60]);
  const same = parseDimConfig({ PANEL_NIGHT_HOURS: '1-5', PANEL_TZ: 'America/Los_Angeles', PANEL_NIGHT_BRIGHTNESS: '2' })!;
  assert.deepEqual([0, 1, 4, 5].map((h) => brightnessAt(same, la(h))), [60, 2, 2, 60]);
  // same instant, different zone: 20:30 in LA is 04:30 in UTC
  const utc = parseDimConfig({ PANEL_NIGHT_HOURS: '1-5', PANEL_TZ: 'UTC' })!;
  assert.equal(brightnessAt(utc, la(20)), 5);
});

test('body carries br only when dimming is configured; rr and go only on red', async () => {
  const off = await buildPanel(fakeSources(), { nowMs: NOW });
  assert.ok(!('br' in off) && !('rr' in off) && !('go' in off));
  const on = await buildPanel(fakeSources(), { nowMs: NOW, dim: parseDimConfig({ PANEL_NIGHT_HOURS: '12-13', PANEL_TZ: 'UTC' }) });
  assert.equal(on.br, 60);
  const red = await buildPanel(fakeSources({ sd: async () => ok({ arr: 'STOPPED', d: [] }) }), { nowMs: NOW });
  assert.deepEqual([red.lvl, red.go, red.rr], ['r', 'sd', ['array']]);
});

// ---------- size ----------

test('real parsers at the caps stay under PANEL_MAX_BYTES with totals on every link', async () => {
  const rows: [string, string, string, string, string, string][] = [];
  for (let i = 1; i <= MAX_DISKS; i++) {
    rows.push([iso(30), 'unraid_disk', 'fs_size_kb', '8000000000', `disk${i}`, 'data'], [iso(30), 'unraid_disk', 'fs_used_kb', '7000000000', `disk${i}`, 'data'],
      [iso(30), 'unraid_disk', 'temp_c', '34', `disk${i}`, 'data'], [iso(30), 'unraid_disk', 'spinning', '1', `disk${i}`, 'data']);
  }
  rows.push([iso(30), 'unraid_array', 'state', 'STARTED', '', '']);
  const live = { mac: Array.from({ length: MAX_MACS }, (_, i) => ({ machine_id: `studio-machine-${i}`, watts: 180.4, darkbloom_state: 'serving', sandbox_jobs: 2, age_s: 120, stale: false })) };
  const earned = Object.fromEntries(live.mac.map((m) => [m.machine_id, { day: 123456, month: 4567890 }]));
  const wan = withNetTotals(withExtraLink(
    parseWan(Array.from({ length: 3 }, (_, i) => ({ iface: `e${i}`, name: ['Cox', 'Starlink', 'Other'][i] + ' WAN', rx_mbps: 912.34, tx_mbps: 41.2 }))),
    parseUnraidNet([{ _time: iso(5), _measurement: 'unraid_net', _field: 'rx_bps', _value: '5e7', iface: 'br0' }], Math.floor(NOW / 1000)),
  ), parseNetTotals(TOTALS, { Cox: 'fw_wan_eth0', Starlink: 'fw_wan_eth0', Other: 'fw_wan_eth0', Unraid: 'fw_wan_eth0' }));
  const p = await buildPanel({
    bl: async () => parseBloom({ live, alerts: [], earned }),
    nw: async () => wan,
    sd: async () => parseStorage(parseFluxCsv(csv(rows)), Math.floor(NOW / 1000)),
    ev: async () => parseEv({ plugged: true, soc: 55, limit: 80, amps: 32, kwh: 21.4, windowHours: 7.5, targetPct: 80, shortfall: false, isWork: true }),
  }, { nowMs: NOW, dim: parseDimConfig({ PANEL_NIGHT_HOURS: '22-7' }) });
  const n = JSON.stringify(p).length;
  assert.ok(n < PANEL_MAX_BYTES, String(n));
  assert.equal((p.sd as any).d.length, MAX_DISKS);
  assert.ok((p.nw as any).l.every((l: any) => l.t30));
});

test('fitBody drops totals, then disk temps, then disk rows when a body is over the limit', () => {
  const bigDisks = Array.from({ length: 16 }, (_, i) => ({ n: `disk${i + 1}`, r: 'd', u: 90, sp: 1, t: 34 }));
  const links = Array.from({ length: 4 }, () => ({ n: 'Cox', d: 1, u: 1, t1: [1, 1], t7: [1, 1], t30: [1, 1] }));
  const mk = (pad: number): PanelBody => ({
    v: 1, t: 1, lvl: 'g', why: [],
    bl: ok({ pad: 'x'.repeat(pad) }), nw: ok({ src: 'wan', l: links }), sd: ok({ d: bigDisks }), ev: ok(),
  });
  const len = (b: PanelBody) => JSON.stringify(b).length;
  const noTotals = len(fitBody(mk(0)));   // mk(0) is under the limit, so this is its own size
  assert.ok(noTotals < PANEL_MAX_BYTES);
  assert.ok('t30' in (fitBody(mk(0)).nw as any).l[0]);   // already small: untouched
  const base = len(mk(0)), saveTotals = base - JSON.stringify({ ...mk(0), nw: ok({ src: 'wan', l: links.map(({ n, d, u }) => ({ n, d, u })) }) }).length;
  // just over the limit: dropping totals is enough
  const a = fitBody(mk(PANEL_MAX_BYTES - base + 1));
  assert.ok(len(a) <= PANEL_MAX_BYTES);
  assert.ok(!('t30' in (a.nw as any).l[0]) && 't' in (a.sd as any).d[0]);
  // needs more than the totals: disk temps go too
  const b = fitBody(mk(PANEL_MAX_BYTES - base + saveTotals + 1));
  assert.ok(len(b) <= PANEL_MAX_BYTES);
  assert.ok(!('t' in (b.sd as any).d[0]) && Array.isArray((b.sd as any).d));
  // far over: the disk rows go, the section stays ok
  const c = fitBody(mk(PANEL_MAX_BYTES));
  assert.ok(!('d' in c.sd) && c.sd.ok === 1);
});
