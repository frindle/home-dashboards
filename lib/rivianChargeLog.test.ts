// Run: node --experimental-strip-types --test lib/rivianChargeLog.test.ts
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  initialChargeState, stepChargeLog, MAX_GAP_MS,
  type ChargePoll, type CarChargeRecord,
} from './rivianChargeLog.ts';

const T0 = Date.parse('2026-11-02T03:00:00Z');
const MIN = 60_000;

function poll(min: number, over: Partial<ChargePoll> = {}): ChargePoll {
  return { ms: T0 + min * MIN, isCharging: true, isPluggedIn: true, soc: 50, energyKwh: null, atHome: null, evseType: null, ...over };
}

function run(polls: ChargePoll[]) {
  let state = initialChargeState();
  const sessions: CarChargeRecord[] = [];
  let dropped = 0;
  for (const p of polls) {
    const r = stepChargeLog(state, p);
    state = r.state;
    if (r.session) sessions.push(r.session);
    if (r.dropped) dropped++;
  }
  return { sessions, dropped, state };
}

const idle = (min: number, over: Partial<ChargePoll> = {}) => poll(min, { isCharging: false, ...over });

test('normal session closes after the debounce with energy, SoC and duration', () => {
  const { sessions, state } = run([
    poll(0, { soc: 40, energyKwh: 0.5, atHome: true, evseType: 1 }),
    poll(60, { soc: 55, energyKwh: 12.4, atHome: true }),
    poll(120, { soc: 70, energyKwh: 25.1, atHome: true }),
    idle(125, { soc: 70 }),
    idle(131, { soc: 70 }),
  ]);
  assert.equal(sessions.length, 1);
  const s = sessions[0];
  assert.equal(s.source, 'rivian-car');
  assert.equal(s.startedAt, new Date(T0).toISOString());
  assert.equal(s.endedAt, new Date(T0 + 120 * MIN).toISOString());
  assert.equal(s.durationMin, 120);
  assert.equal(s.energyKwh, 25.1);
  assert.equal(s.startSoc, 40);
  assert.equal(s.endSoc, 70);
  assert.equal(s.socDelta, 30);
  assert.equal(s.atHome, true);
  assert.equal(s.evseType, 1);
  assert.equal(state.open, null);
});

test('a flap shorter than 10 minutes stays one session', () => {
  const { sessions } = run([
    poll(0, { soc: 40, energyKwh: 1 }),
    idle(4),
    poll(8, { soc: 45, energyKwh: 4 }),
    poll(90, { soc: 60, energyKwh: 15 }),
    idle(100), idle(111),
  ]);
  assert.equal(sessions.length, 1);
  assert.equal(sessions[0].energyKwh, 15);
});

test('a flap longer than the debounce splits into two sessions', () => {
  const { sessions } = run([
    poll(0, { soc: 40, energyKwh: 1 }), poll(30, { soc: 50, energyKwh: 8 }),
    idle(35), idle(41),
    poll(60, { soc: 52, energyKwh: 2 }), poll(90, { soc: 60, energyKwh: 9 }),
    idle(95), idle(101),
  ]);
  assert.equal(sessions.length, 2);
});

test('frozen energy from the previous session is ignored until it changes', () => {
  const { sessions } = run([
    poll(0, { soc: 30, energyKwh: 20 }), poll(60, { soc: 50, energyKwh: 31 }),
    idle(61), idle(72),
    // new session starts with the frozen 31, then real data
    poll(600, { soc: 50, energyKwh: 31 }),
    poll(630, { soc: 55, energyKwh: 31 }),
    poll(660, { soc: 60, energyKwh: 4.2 }),
    idle(661), idle(672),
  ]);
  assert.equal(sessions.length, 2);
  assert.equal(sessions[1].energyKwh, 4.2);
});

test('only frozen readings means energy null but the session is logged with SoC', () => {
  const { sessions } = run([
    poll(0, { soc: 30, energyKwh: 20 }), poll(60, { soc: 50, energyKwh: 31 }), idle(61), idle(72),
    poll(600, { soc: 50, energyKwh: 31 }), poll(660, { soc: 62, energyKwh: 31 }), idle(661), idle(672),
  ]);
  assert.equal(sessions[1].energyKwh, null);
  assert.equal(sessions[1].socDelta, 12);
});

test('energy null is logged with SoC', () => {
  const { sessions } = run([poll(0, { soc: 40 }), poll(60, { soc: 48 }), idle(61), idle(72)]);
  assert.equal(sessions.length, 1);
  assert.equal(sessions[0].energyKwh, null);
  assert.equal(sessions[0].socDelta, 8);
});

test('a gap beyond MAX_GAP_MS closes at the last charging poll with gap: true', () => {
  const gapMin = MAX_GAP_MS / MIN + 5;
  const { sessions } = run([
    poll(0, { soc: 40, energyKwh: 1 }), poll(60, { soc: 50, energyKwh: 8 }),
    idle(60 + gapMin),
  ]);
  assert.equal(sessions.length, 1);
  assert.equal(sessions[0].gap, true);
  assert.equal(sessions[0].endedAt, new Date(T0 + 60 * MIN).toISOString());
});

test('unplugging closes immediately', () => {
  const { sessions, state } = run([
    poll(0, { soc: 40, energyKwh: 1 }), poll(60, { soc: 50, energyKwh: 8 }),
    idle(62, { isPluggedIn: false }),
  ]);
  assert.equal(sessions.length, 1);
  assert.equal(state.open, null);
});

test('atHome: true if any poll home, false if all known away, null if unknown', () => {
  const mk = (flags: (boolean | null)[]) => run([
    ...flags.map((h, i) => poll(i * 20, { soc: 40 + i * 5, atHome: h })),
    idle(200), idle(211),
  ]).sessions[0].atHome;
  assert.equal(mk([false, true, false]), true);
  assert.equal(mk([false, null, false]), false);
  assert.equal(mk([null, null, null]), null);
});

test('noise (under 1% and no energy) is dropped; small energy alone keeps it', () => {
  const noise = run([poll(0, { soc: 50 }), poll(5, { soc: 50, energyKwh: 0.1 }), idle(6), idle(17)]);
  assert.equal(noise.sessions.length, 0);
  assert.equal(noise.dropped, 1);
  const kept = run([poll(0, { soc: 50, energyKwh: 0.1 }), poll(5, { soc: 50, energyKwh: 0.6 }), idle(6), idle(17)]);
  assert.equal(kept.sessions.length, 1);
});
