import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { recordRivianChargePoll } from './rivianChargeRunner.ts';

// The runner reads both at call time, so setting them after the import is fine.
const dir = mkdtempSync(join(tmpdir(), 'rivian-charge-'));
process.env.CHARGE_HISTORY_DIR = dir;
delete process.env.INFLUX_URL;

const H = 3_600_000;
const T0 = Date.parse('2026-11-10T06:00:00Z');
const rows = () => readFileSync(join(dir, 'rivian-charge-history.jsonl'), 'utf-8').trim().split('\n').map((l) => JSON.parse(l));

test('a restart reloads stale saved state: the open session closes as a gap row and the frozen kWh baseline survives', async () => {
  // Saved by a run that stopped mid-charge 8 hours before the next poll.
  writeFileSync(join(dir, 'rivian-charge-open.json'), JSON.stringify({
    open: {
      startMs: T0, lastChargingMs: T0 + 2 * H, lastPollMs: T0 + 2 * H,
      startSoc: 40, endSoc: 70, energyKwh: 40, baseline: null, trusted: true,
      anyHome: true, anyAway: false, evseType: null,
    },
    lastEnergy: 40,
    lastPollMs: T0 + 2 * H,
  }));

  // Next session starts with Parallax still frozen at the old 40 kWh.
  await recordRivianChargePoll({ isCharging: true, isPluggedIn: true, chargePercent: 50, totalChargedEnergyKwh: 40 }, true, T0 + 10 * H);
  let r = rows();
  assert.equal(r.length, 1);
  assert.equal(r[0].gap, true);
  assert.equal(r[0].energyKwh, 40);
  assert.equal(r[0].endedAt, new Date(T0 + 2 * H).toISOString());

  await recordRivianChargePoll({ isCharging: true, isPluggedIn: true, chargePercent: 55, totalChargedEnergyKwh: 6 }, true, T0 + 10.5 * H);
  await recordRivianChargePoll({ isCharging: false, isPluggedIn: false, chargePercent: 55, totalChargedEnergyKwh: 6 }, true, T0 + 11 * H);
  r = rows();
  assert.equal(r.length, 2);
  assert.equal(r[1].energyKwh, 6, 'frozen 40 kWh from the previous session is not taken');
  assert.equal(r[1].socDelta, 5);
  assert.equal(r[1].atHome, true);

  // lastEnergy is persisted even with no session open.
  const saved = JSON.parse(readFileSync(join(dir, 'rivian-charge-open.json'), 'utf-8'));
  assert.equal(saved.open, null);
  assert.equal(saved.lastEnergy, 6);
});
