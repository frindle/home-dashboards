// Run: TZ=America/Los_Angeles node --experimental-strip-types --test lib/commuteFloor.test.ts
// The commute floor (lib/commuteFloor.ts) and the plan builder's use of it. Synthetic
// places and drives only; nothing here routes, reads the drive log or the calendar.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { commuteFloor, commuteLegs, flooredNeed, workCells, type CommuteDrive } from './commuteFloor.ts';
import { cellOf, type Leg } from './valhalla.ts';
import { pctForMiles } from './tripPlan.ts';
import { buildRivianPlan } from './rivianPlan.ts';
import { localDayNumber } from './chargePlan.ts';
import type { LatLon } from './ics.ts';

const SALT = 'test-salt';
const WORK: LatLon = { lat: 40.0, lon: -100.0 };
const HOME: LatLon = { lat: 40.2, lon: -100.3 };
const OTHER: LatLon = { lat: 39.5, lon: -99.5 };
const NOW = Date.parse('2026-11-19T22:00:00-08:00');
const DAY = 86_400_000;
const W = cellOf(SALT, WORK), H = cellOf(SALT, HOME), O = cellOf(SALT, OTHER);

const toWork = (daysAgo: number, miles: number): CommuteDrive => ({
  start: new Date(NOW - daysAgo * DAY).toISOString(), miles, at_home: true, end_at_home: false, start_cell: H, end_cell: W,
});
const fromWork = (daysAgo: number, miles: number): CommuteDrive => ({
  start: new Date(NOW - daysAgo * DAY + 9 * 3_600_000).toISOString(), miles, at_home: false, end_at_home: true, start_cell: W, end_cell: H,
});
const noRoute = async (): Promise<Leg | null> => null;

test('workCells: the work cell and its 8 neighbours; a point 500 m off still matches', () => {
  const cells = workCells(SALT, WORK);
  assert.equal(cells.size, 9);
  assert.ok(cells.has(W));
  const dLat = 500 / 111_320;
  const dLon = 500 / (111_320 * Math.cos((WORK.lat * Math.PI) / 180));
  for (const [dy, dx] of [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [-1, -1]]) {
    assert.ok(cells.has(cellOf(SALT, { lat: WORK.lat + dy * dLat, lon: WORK.lon + dx * dLon })), `${dy},${dx}`);
  }
  assert.ok(!cells.has(O));
  assert.ok(!cells.has(H));
});

test('commuteLegs: medians each way, only home<->work drives, inside the window', () => {
  const cells = workCells(SALT, WORK);
  const drives: CommuteDrive[] = [
    toWork(1, 15), fromWork(1, 16), toWork(2, 14), fromWork(2, 18), toWork(3, 16), fromWork(3, 17),
    // Home to somewhere else, work to somewhere else, a drive with no cells: none count.
    { start: new Date(NOW - DAY).toISOString(), miles: 40, at_home: true, end_at_home: false, start_cell: H, end_cell: O },
    { start: new Date(NOW - 2 * DAY).toISOString(), miles: 30, at_home: false, end_at_home: false, start_cell: W, end_cell: O },
    { start: new Date(NOW - 2 * DAY).toISOString(), miles: 30, at_home: true, end_at_home: true },
    // Outside the 60-day window, in the future, zero miles.
    toWork(61, 99), fromWork(70, 99), toWork(-1, 99), toWork(4, 0),
  ];
  const l = commuteLegs(drives, cells, NOW);
  assert.equal(l.out, 15);
  assert.equal(l.back, 17);
  assert.equal(l.n, 6);
});

test('commuteLegs: a drive that both starts and ends at work does not count either way', () => {
  const cells = workCells(SALT, WORK);
  const loop: CommuteDrive = { start: new Date(NOW - DAY).toISOString(), miles: 3, at_home: true, end_at_home: true, start_cell: W, end_cell: W };
  assert.deepEqual(commuteLegs([loop], cells, NOW), { out: null, back: null, n: 0 });
});

test('commuteLegs: only the 10 most recent legs each way count', () => {
  const cells = workCells(SALT, WORK);
  const drives = [...Array.from({ length: 10 }, (_, i) => toWork(i + 1, 10)), ...Array.from({ length: 10 }, (_, i) => toWork(i + 20, 50))];
  assert.equal(commuteLegs(drives, cells, NOW).out, 10);
});

test('commuteFloor: measured round trip from the drive log', async () => {
  const f = await commuteFloor({
    work: WORK, home: HOME, drives: [toWork(1, 15), fromWork(1, 17)], salt: SALT, nowMs: NOW, highF: null, route: noRoute,
  });
  assert.deepEqual(f, { pct: pctForMiles(32, null), miles: 32, source: 'drives', legs: 2 });
});

test('commuteFloor: legs logged one way only count twice', async () => {
  const f = await commuteFloor({
    work: WORK, home: HOME, drives: [toWork(1, 15), toWork(2, 15)], salt: SALT, nowMs: NOW, highF: 70, route: noRoute,
  });
  assert.equal(f?.miles, 30);
  assert.equal(f?.pct, pctForMiles(30, 70));
  assert.equal(f?.source, 'drives');
});

test('commuteFloor: fewer than 2 legs uses the routed road distance there and back', async () => {
  const seen: Array<[LatLon, LatLon]> = [];
  const route = async (a: LatLon, b: LatLon): Promise<Leg | null> => {
    seen.push([a, b]);
    return a === HOME ? { miles: 14, seconds: 1200 } : { miles: 16, seconds: 1300 };
  };
  const f = await commuteFloor({ work: WORK, home: HOME, drives: [toWork(1, 9)], salt: SALT, nowMs: NOW, highF: null, route });
  assert.deepEqual(f, { pct: pctForMiles(30, null), miles: 30, source: 'route', legs: 0 });
  assert.deepEqual(seen, [[HOME, WORK], [WORK, HOME]]);
  // Only one direction routes: twice that leg.
  const one = await commuteFloor({
    work: WORK, home: HOME, drives: [], salt: SALT, nowMs: NOW, highF: null,
    route: async (a) => (a === HOME ? { miles: 14, seconds: 1200 } : null),
  });
  assert.equal(one?.miles, 28);
});

test('commuteFloor: routing fails -> the single measured leg, doubled; nothing at all -> null', async () => {
  const f = await commuteFloor({ work: WORK, home: HOME, drives: [fromWork(1, 12)], salt: SALT, nowMs: NOW, highF: null, route: noRoute });
  assert.equal(f?.miles, 24);
  assert.equal(f?.source, 'drives');
  assert.equal(f?.legs, 1);
  assert.equal(await commuteFloor({ work: WORK, home: HOME, drives: [], salt: SALT, nowMs: NOW, highF: null, route: noRoute }), null);
  assert.equal(await commuteFloor({ work: WORK, home: null, drives: [], salt: SALT, nowMs: NOW, highF: null, route: noRoute }), null);
  assert.equal(await commuteFloor({ work: null, home: HOME, drives: [toWork(1, 15), fromWork(1, 17)], salt: SALT, nowMs: NOW, highF: null, route: noRoute }), null);
});

test('flooredNeed: raises a lower or missing work need, leaves a higher one and the day off alone', () => {
  const floor = { pct: 18, miles: 31, source: 'drives' as const, legs: 4 };
  const n = { work: 12, off: 8, workSource: 'curve', tempF: 50 };
  assert.deepEqual(flooredNeed(n, floor), { work: 18, off: 8, workSource: 'commute', tempF: 50 });
  assert.deepEqual(flooredNeed({ ...n, work: null, workSource: 'measured' }, floor), { work: 18, off: 8, workSource: 'commute', tempF: 50 });
  assert.equal(flooredNeed({ ...n, work: 25 }, floor)!.work, 25);
  assert.equal(flooredNeed(n, null), n);
  assert.equal(flooredNeed(null, floor), null);
});

test('buildRivianPlan: the commute floor raises a lower work-day use and says so', () => {
  const now = Date.parse('2026-11-12T23:00:00-08:00');
  const tomorrow = localDayNumber(now) + 1;
  const est = { ewmaKwh: 22, plannedKwh: 22, rates: { workKwh: 14.4, offKwh: 14 }, nextDayWork: true } as any;
  const common = {
    nowMs: now, shifts: [], workDays: new Set([tomorrow]), est, state: { chargePercent: 30, chargeLimit: 70, isPluggedIn: true },
    kwhPerPct: 1.44, floorPct: 25, margin: 1.1, forecastHighF: null, minAmps: 8, maxAmps: 48,
  };
  // Daily-use need below the floor: the floor plans the work day.
  const a = buildRivianPlan({ ...common, useOverride: { work: 10, off: 8 }, workFloorPct: 18 })!;
  assert.equal(a.usePctWork, 18);
  assert.equal(a.needSource.work, 'commute');
  assert.equal(a.usePctOff, 8);
  assert.equal(a.needSource.off, 'daily-use');
  // Daily-use need above the floor: unchanged.
  const b = buildRivianPlan({ ...common, useOverride: { work: 22, off: 8 }, workFloorPct: 18 })!;
  assert.equal(b.usePctWork, 22);
  assert.equal(b.needSource.work, 'daily-use');
  // No work need from the log: the floor beats the charge history when higher.
  const hist = buildRivianPlan({ ...common, useOverride: { work: null, off: 8 } })!;
  assert.equal(hist.needSource.work, 'charge-history');
  const c = buildRivianPlan({ ...common, useOverride: { work: null, off: 8 }, workFloorPct: hist.usePctWork + 5 })!;
  assert.equal(c.usePctWork, hist.usePctWork + 5);
  assert.equal(c.needSource.work, 'commute');
  assert.ok(c.result.plan.amps! >= hist.result.plan.amps!);
  // No floor, a zero or junk floor: the old plan.
  for (const workFloorPct of [null, 0, Number.NaN]) {
    const d = buildRivianPlan({ ...common, useOverride: { work: 10, off: 8 }, workFloorPct })!;
    assert.equal(d.usePctWork, 10);
    assert.equal(d.needSource.work, 'daily-use');
  }
});
