// Run: TZ=America/Los_Angeles node --experimental-strip-types --test lib/forecastSwitch.test.ts
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fillTarget, forecastOff, parseSwitch, switchFromChoice, switchKey, SWITCH_ON } from './forecastSwitch.ts';

const W = { startMin: 0, durationMin: 8 * 60 };   // EV_WINDOW: 00:00-08:00
const at = (d: number, h: number, m = 0) => new Date(2026, 10, d, h, m).getTime();

test('on by default; off until turned on; off for N days turns itself back on', () => {
  const now = at(15, 12);
  assert.equal(forecastOff(SWITCH_ON, now), false);
  const off = switchFromChoice('off', now)!;
  assert.deepEqual(off, { off: true, sinceMs: now, untilMs: null });
  assert.equal(forecastOff(off, now + 30 * 86_400_000), true);
  const three = switchFromChoice('3', now)!;
  assert.equal(three.untilMs, now + 3 * 86_400_000);
  assert.equal(forecastOff(three, now + 3 * 86_400_000 - 1), true);
  assert.equal(forecastOff(three, now + 3 * 86_400_000), false);
  assert.deepEqual(switchFromChoice('on', now), SWITCH_ON);
  assert.deepEqual(switchFromChoice(7, now)?.untilMs, now + 7 * 86_400_000);
  for (const bad of ['2', 0, '', null, undefined, 'maybe', 30]) assert.equal(switchFromChoice(bad, now), null);
});

test('switchKey changes when the switch is flipped or its off time runs out', () => {
  const now = at(15, 12);
  assert.equal(switchKey(SWITCH_ON, now), 'on');
  const one = switchFromChoice(1, now)!;
  assert.equal(switchKey(one, now), `off:${now}`);
  assert.equal(switchKey(one, now + 86_400_000), 'on');
  const again = switchFromChoice('off', now + 60_000)!;
  assert.notEqual(switchKey(again, now + 60_000), switchKey(one, now));
});

test('fillTarget: the car limit by the end of the coming window; 100 when unknown; clamped 50..100', () => {
  // Afternoon: tonight's window, ends 08:00 tomorrow.
  assert.deepEqual(fillTarget(80, at(15, 15), W), { targetPct: 80, byMs: at(16, 8) });
  // Inside the window: the window running now.
  assert.deepEqual(fillTarget(70, at(16, 3), W), { targetPct: 70, byMs: at(16, 8) });
  assert.equal(fillTarget(null, at(15, 15), W)?.targetPct, 100);
  assert.equal(fillTarget(undefined, at(15, 15), W)?.targetPct, 100);
  assert.equal(fillTarget(0, at(15, 15), W)?.targetPct, 100);
  assert.equal(fillTarget(40, at(15, 15), W)?.targetPct, 50);
  assert.equal(fillTarget(85.6, at(15, 15), W)?.targetPct, 86);
});

test('parseSwitch: anything but a clean off reads as on', () => {
  assert.deepEqual(parseSwitch(null), SWITCH_ON);
  assert.deepEqual(parseSwitch({ off: 'yes' }), SWITCH_ON);
  assert.deepEqual(parseSwitch({ off: true, sinceMs: 5, untilMs: 'x' }), { off: true, sinceMs: 5, untilMs: null });
});
