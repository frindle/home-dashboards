// Work days for scripts/backtest-8a.mjs straight from the shift calendar, fetched the way the app
// does (createShiftCalendarSource: SHIFTS_ICS_URL, optional SHIFTS_ICS_USER/PASSWORD; the URL is
// never printed). Returns YYYY-MM-DD strings plus counts only.
import { createShiftCalendarSource } from './shiftCalendar.ts';
import { dateOf } from './needModels.ts';

export interface WorkdaysFromIcs {
  days: string[];
  found: number;
  inRange: number;            // inside since..last (both inclusive; null bound = open)
  earliestMonth: string | null;   // YYYY-MM
}

export async function workdaysFromCalendar(
  since: string | null, last: string | null,
  env: Record<string, string | undefined> = process.env,
  fetchImpl?: Parameters<ReturnType<typeof createShiftCalendarSource>>[2],
): Promise<WorkdaysFromIcs | null> {
  const cal = await createShiftCalendarSource()(Date.now(), env, fetchImpl, { refresh: true });
  if (!cal) return null;
  const days = [...cal.workDays].sort((a, b) => a - b).map(dateOf);
  const inRange = days.filter((d) => (!since || d >= since) && (!last || d <= last)).length;
  return { days, found: days.length, inRange, earliestMonth: days.length ? days[0].slice(0, 7) : null };
}
