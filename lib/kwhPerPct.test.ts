// Run: node --experimental-strip-types --test lib/kwhPerPct.test.ts
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { kwhPerPctRows, measuredKwhPerPct } from './kwhPerPct.ts';
import type { CarChargeRecord } from './rivianChargeLog.ts';

const H = 3_600_000;
const T0 = Date.parse('2026-10-01T06:00:00Z');
const iso = (ms: number) => new Date(ms).toISOString();

function car(day: number, socDelta: number, extra: Partial<CarChargeRecord> = {}): CarChargeRecord {
  const s = T0 + day * 24 * H;
  return {
    source: 'roamer', startedAt: iso(s), endedAt: iso(s + 4 * H), durationMin: 240,
    energyKwh: socDelta * 1.4, startSoc: 40, endSoc: 40 + socDelta, socDelta, atHome: true, evseType: null, ...extra,
  };
}
// WC rows written before the WC log carried SoC.
const wc = (day: number, kwh: number) => {
  const s = T0 + day * 24 * H + 2 * 60_000;
  return { startedAt: iso(s), endedAt: iso(s + 4 * H - 5 * 60_000), energyKwh: kwh };
};

test('WC rows without SoC are measured against the paired car session gain', () => {
  const cars = [0, 1, 2, 3, 4, 5].map((d) => car(d, 20));
  const wcs = [0, 1, 2, 3, 4, 5].map((d) => wc(d, 30));
  assert.equal(measuredKwhPerPct(wcs, 1.44), 1.44, 'alone they leave the seed');
  const rows = kwhPerPctRows(cars, wcs);
  assert.equal(rows.length, 6);
  assert.equal(measuredKwhPerPct(rows, 1.44), 1.5);
});

test('no car history passes the WC rows through unchanged', () => {
  const wcs = [wc(0, 30)];
  assert.equal(kwhPerPctRows([], wcs), wcs);
});

test('car gap rows are not paired; an unpaired WC row keeps its own SoC', () => {
  const rows = kwhPerPctRows(
    [car(0, 5, { gap: true }), car(1, 20)],
    [{ ...wc(0, 30), startSoc: 30, endSoc: 50 }, wc(1, 31), wc(9, 12)],
  );
  const byStart = rows.map((r) => [r.energyKwh, (r.endSoc ?? NaN) - (r.startSoc ?? NaN)]);
  assert.deepEqual(byStart.slice(0, 1), [[31, 20]]);
  assert.ok(byStart.some(([k, d]) => k === 30 && d === 20), 'gap-day WC row uses its own 20% gain, not the gap row 5%');
  assert.ok(byStart.some(([k, d]) => k === 12 && Number.isNaN(d)), 'unpaired WC row without SoC passes through');
});
