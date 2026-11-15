import { test } from 'node:test';
import assert from 'node:assert/strict';
import { dedupeChargeRows } from './chargeRowDedupe.ts';

const row = (side: string, startedAt: string, energyKwh: number) => ({ side, startedAt, energyKwh });

test('drops a repeat of the same side and start, keeping the first', () => {
  const rows = [
    row('LEFT', '2026-11-12T08:02:00.000Z', 21.3),
    row('LEFT', '2026-11-14T08:00:00.000Z', 23.6),
    row('LEFT', '2026-11-14T08:00:00.000Z', 23.61),
  ];
  assert.deepEqual(dedupeChargeRows(rows), rows.slice(0, 2));
});

test('keeps sessions that differ by side or start', () => {
  const rows = [
    row('LEFT', '2026-11-14T08:00:00.000Z', 23.6),
    row('RIGHT', '2026-11-14T08:00:00.000Z', 10),
    row('LEFT', '2026-11-14T09:00:00.000Z', 1),
  ];
  assert.deepEqual(dedupeChargeRows(rows), rows);
  assert.deepEqual(dedupeChargeRows([]), []);
});
