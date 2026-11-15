// The manual off switch for forecast charging (Penn 11-15: "give us a manual way to turn off
// forecasted charging"). While it is off, the forecasts (daily use, calendar trips, typed needs)
// no longer set the night's charge: the car fills to its own charge limit every night inside the
// EV window, at the lowest amps that gets there by the window's end (8A minimum), and the trip
// pushes stop. A stored "N% by a time" override still counts when it asks for more. The switch can
// turn itself back on at a set time; once on, the forecast plan is written again at once (in a
// write slot) or at the next slot.
//
// KEYS_DIR/forecast-switch.json  { off: boolean, sinceMs: number | null, untilMs: number | null }
import { mkdir, readFile, rename, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { OVERRIDE_MAX_PCT, OVERRIDE_MIN_PCT, windowsBetween, type ChargeOverride, type ChargeWindow } from './chargeOverride.ts';

export interface ForecastSwitch {
  off: boolean;
  sinceMs: number | null;   // when it was turned off
  untilMs: number | null;   // turns itself back on then; null = until turned on by hand
}

export const SWITCH_ON: ForecastSwitch = { off: false, sinceMs: null, untilMs: null };
export const OFF_FOR_DAYS = [1, 3, 7] as const;   // the page's choices, besides "until I turn it on"

const DAY_MS = 86_400_000;

export function forecastOff(s: ForecastSwitch, nowMs: number): boolean {
  return s.off && (s.untilMs === null || nowMs < s.untilMs);
}

// Changes whenever the switch's effect changes (turned off or on, or the off time ran out), so the
// controller can write the schedule again right away instead of waiting a day for the slot.
export function switchKey(s: ForecastSwitch, nowMs: number): string {
  return forecastOff(s, nowMs) ? `off:${s.sinceMs ?? 0}` : 'on';
}

// "Off": the car's charge limit (Rivian allows 50..100; 100 when unknown, since the car stops at
// its own limit anyway) by the end of the window it charges in next. null when there is no window.
export function fillTarget(limitPct: number | null | undefined, nowMs: number, w: ChargeWindow): ChargeOverride | null {
  const lim = typeof limitPct === 'number' && Number.isFinite(limitPct) && limitPct > 0 ? Math.round(limitPct) : OVERRIDE_MAX_PCT;
  const win = windowsBetween(nowMs, nowMs + 2 * DAY_MS, w)[0];
  if (!win) return null;
  return { targetPct: Math.min(OVERRIDE_MAX_PCT, Math.max(OVERRIDE_MIN_PCT, lim)), byMs: win.endMs };
}

// The form's choice: 'on', 'off' (until turned on) or a number of days from OFF_FOR_DAYS.
export function switchFromChoice(choice: unknown, nowMs: number): ForecastSwitch | null {
  if (choice === 'on') return SWITCH_ON;
  if (choice === 'off') return { off: true, sinceMs: nowMs, untilMs: null };
  const days = Number(choice);
  if ((OFF_FOR_DAYS as readonly number[]).includes(days)) return { off: true, sinceMs: nowMs, untilMs: nowMs + days * DAY_MS };
  return null;
}

const path = () => join(process.env.KEYS_DIR ?? join(process.cwd(), 'keys'), 'forecast-switch.json');

export function parseSwitch(raw: unknown): ForecastSwitch {
  const x = raw as Partial<ForecastSwitch> | null;
  if (!x || x.off !== true) return SWITCH_ON;
  const num = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : null);
  return { off: true, sinceMs: num(x.sinceMs), untilMs: num(x.untilMs) };
}

export async function readForecastSwitch(): Promise<ForecastSwitch> {
  try {
    return parseSwitch(JSON.parse(await readFile(path(), 'utf-8')));
  } catch {
    return SWITCH_ON;
  }
}

export async function writeForecastSwitch(s: ForecastSwitch): Promise<void> {
  const p = path();
  await mkdir(join(p, '..'), { recursive: true });
  await writeFile(p + '.tmp', JSON.stringify(s));
  await rename(p + '.tmp', p);
}
