#!/usr/bin/env node
// Backtest of the day-off average and the 8A night rule (Penn 2026-11-27), live nightPlan walk.
//   (a) main before the change: 90-day day-off average, no hold
//   (b) new day-off average (every day off with a drive), no hold
//   (c) (b) + the 8A rule: a covered night is held unless a full 00:00-08:00 window at 8A fits under the limit
// Usage (TZ matters: drive timestamps are bucketed into local days):
//   TZ=America/Los_Angeles node --experimental-strip-types scripts/backtest-8a.mjs \
//     <rivian-daily-use.jsonl> <rivian-drives.jsonl> (<workdays.json> | --workdays-ics) [--limit 70] [--floor 25] [--since YYYY-MM-DD]
// Prints counts and aggregates only: never a row, a date or a per-day value.
import { readFileSync } from 'node:fs';
import { workdaysFromCalendar } from '../lib/backtestWorkdays.ts';
import { buildNeedData, legacyOffDayNeed, LIVE_BAND_OVERRIDES, modelCombined, NEED_RULE, nightStats, simulate } from '../lib/needModels.ts';

const args = process.argv.slice(2);
const opt = (name, dflt, str = false) => {
  const i = args.indexOf(name);
  if (i < 0) return dflt;
  const v = str ? args[i + 1] : Number(args[i + 1]);
  args.splice(i, 2);
  return str ? v : Number.isFinite(v) ? v : dflt;
};
const limit = opt('--limit', 70);
const floor = opt('--floor', 25);
const since = opt('--since', null, true);   // vehicle historySince; rows before it are ignored
const useIcs = args.includes('--workdays-ics');
if (useIcs) args.splice(args.indexOf('--workdays-ics'), 1);
const [dailyPath, drivesPath, workPath] = args;
if (!dailyPath || !drivesPath || (!workPath && !useIcs)) {
  console.error('usage: backtest-8a.mjs <daily-use.jsonl> <drives.jsonl> (<workdays.json> | --workdays-ics) [--limit 70] [--floor 25] [--since YYYY-MM-DD]');
  process.exit(2);
}
const jsonl = (p) => readFileSync(p, 'utf8').split('\n').filter((l) => l.trim()).map((l) => JSON.parse(l));
const rows = jsonl(dailyPath).filter((r) => !since || r.date >= since);
const drives = jsonl(drivesPath).filter((d) => !since || String(d.start).slice(0, 10) >= since);
let workdays;
if (useIcs) {
  const lastDaily = rows.reduce((m, r) => (r.date > m ? r.date : m), '');
  const w = await workdaysFromCalendar(since, lastDaily || null);
  if (!w) { console.error('workdays: calendar not fetched (SHIFTS_ICS_URL unset or fetch failed)'); process.exit(3); }
  console.log(`work days from the calendar: ${w.found} found, ${w.inRange} inside the history range, earliest month ${w.earliestMonth ?? 'none'}`);
  workdays = w.days;
} else workdays = JSON.parse(readFileSync(workPath, 'utf8'));
const data = buildNeedData(rows, drives, workdays);

const combined = modelCombined(data, LIVE_BAND_OVERRIDES, NEED_RULE);
const legacy = (date, isWork, curve) => {
  const n = combined(date, isWork, curve);
  if (!n || isWork) return n;
  const o = legacyOffDayNeed(data, date);
  return o ? { ...n, pct: o.pct } : n;
};
const o = { limit, floor, mode: 'live' };
const plans = [
  ['a main today', simulate(data, legacy, o)],
  ['b new avg', simulate(data, combined, o)],
  ['c new avg + 8A rule', simulate(data, combined, { ...o, rule8: true })],
];
console.log(`limit ${limit}%  floor ${floor}%  window 8h at 8A min  arrival = morning SoC - the day's used_pct`);
for (const [name, days] of plans) {
  const s = nightStats(days, floor);
  const f = (x, d = 1) => (Number.isFinite(x) ? x.toFixed(d) : '-');
  console.log(`\n${name}: ${s.nights} nights`);
  console.log(`  nights: full 8h at 8A ${s.fullAt8} | partial ${s.partial} | held ${s.held} | above 8A ${s.above8}`);
  console.log(`  came home < ${floor}%: ${s.short25.afterHeld} after a held night, ${s.short25.afterCharged} after a charged night`);
  console.log(`  came home < 15%: ${s.short15.afterHeld} after a held night, ${s.short15.afterCharged} after a charged night`);
  console.log(`  lowest arrival ${f(s.minArrival)}%  total ${f(s.kwh, 0)} kWh  amps on charged nights: max ${s.maxAmps}, mean ${f(s.meanAmps)}`);
}
