// Run: node --experimental-strip-types --test lib/chargeSourceCompare.test.ts
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { compareSources, type WcRow } from './chargeSourceCompare.ts';
import type { CarChargeRecord } from './rivianChargeLog.ts';

const T0 = Date.parse('2026-11-02T03:00:00Z');
const H = 3_600_000;
const iso = (h: number) => new Date(T0 + h * H).toISOString();

function car(s: number, e: number, kwh: number | null, socDelta: number | null): CarChargeRecord {
  return { source: 'rivian-car', startedAt: iso(s), endedAt: iso(e), durationMin: (e - s) * 60, energyKwh: kwh,
    startSoc: 20, endSoc: socDelta === null ? null : 20 + socDelta, socDelta, atHome: true, evseType: null };
}
const wc = (s: number, e: number, kwh: number): WcRow => ({ startedAt: iso(s), endedAt: iso(e), energyKwh: kwh });

test('pairs overlapping sessions and computes diff, ratio and kWh per SoC%', () => {
  const r = compareSources([car(0, 4, 44, 40)], [wc(0.1, 3.9, 40)]);
  assert.equal(r.pairs.length, 1);
  const p = r.pairs[0];
  assert.equal(p.wcKwh, 40);
  assert.equal(p.carMinusWcKwh, 4);
  assert.equal(p.ratio, 1.1);
  assert.equal(p.wcKwhPerSocPct, 1);
  assert.equal(p.carKwhPerSocPct, 1.1);
  assert.equal(r.summary.medianRatio, 1.1);
  assert.equal(r.summary.meanAbsDiffKwh, 4);
  assert.equal(r.summary.wcKwhPerSocPct, 1);
  assert.equal(r.summary.carKwhPerSocPct, 1.1);
});

test('unpaired sessions land in carOnly and wcOnly', () => {
  const r = compareSources([car(0, 2, 20, 20), car(30, 32, 10, 10)], [wc(0, 2, 18), wc(60, 62, 9)]);
  assert.equal(r.pairs.length, 1);
  assert.equal(r.carOnly.length, 1);
  assert.equal(r.carOnly[0].startedAt, iso(30));
  assert.equal(r.wcOnly.length, 1);
  assert.equal(r.wcOnly[0].startedAt, iso(60));
});

test('multiple WC rows overlapping one car session are summed', () => {
  const r = compareSources([car(0, 6, 30, 30)], [wc(0, 2, 10), wc(3, 6, 18)]);
  assert.equal(r.pairs[0].wcRows, 2);
  assert.equal(r.pairs[0].wcKwh, 28);
});

test('median of an even count averages the middle two', () => {
  const r = compareSources(
    [car(0, 1, 10, 10), car(10, 11, 12, 10), car(20, 21, 20, 10), car(30, 31, 30, 10)],
    [wc(0, 1, 10), wc(10, 11, 10), wc(20, 21, 10), wc(30, 31, 10)],
  );
  assert.equal(r.summary.medianRatio, 1.6);
  assert.equal(r.summary.meanRatio, 1.8);
});

test('no division by zero: zero WC kWh, null car kWh, tiny socDelta', () => {
  const r = compareSources([car(0, 1, 5, 0.5), car(10, 11, null, 10)], [wc(0, 1, 0), wc(10, 11, 8)]);
  assert.equal(r.pairs[0].ratio, null);
  assert.equal(r.pairs[0].carKwhPerSocPct, null);
  assert.equal(r.pairs[1].carMinusWcKwh, null);
  assert.equal(r.pairs[1].ratio, null);
  assert.equal(r.summary.medianRatio, null);
  assert.equal(r.summary.meanAbsDiffKwh, 5);
});

test('empty inputs give an empty summary', () => {
  const r = compareSources([], []);
  assert.equal(r.summary.pairs, 0);
  assert.equal(r.summary.meanRatio, null);
});
