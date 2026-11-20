// Commute floor (Penn 2026-11-19): a work day needs at least the drive to work
// and back. The work-day need models count drive-log miles and recent days, and a
// thin or lossy log can plan a work day below the commute itself.
//
// Work is a point: the place picked on /trips, else the shift calendar's place
// (the shift exporter writes it on every shift since v1.72). The round trip is
// measured first: drive-log drives from home that end within a cell of work
// (out) and drives from there that end at home (back), median of the recent ones
// each way; one way only counts twice. With fewer than 2 such legs, the routed
// road distance there and back is used instead (LAN Valhalla first; online only
// as lib/valhalla.ts allows). Cells are HMACs (lib/driveCell.ts), so this
// matches places without storing any.
import { cellOf, type Leg } from './valhalla.ts';
import type { LatLon } from './ics.ts';
import { pctForMiles } from './tripPlan.ts';

const CELL_M = 600;                 // lib/driveCell.ts grid
const M_PER_DEG_LAT = 111_320;
export const COMMUTE_WINDOW_DAYS = 60;
export const COMMUTE_RECENT_LEGS = 10;
export const COMMUTE_MIN_LEGS = 2;

export interface CommuteDrive {
  start: string;
  miles: number;
  at_home?: boolean | null;
  end_at_home?: boolean | null;
  start_cell?: string | null;
  end_cell?: string | null;
}

export interface CommuteFloor {
  pct: number;                      // % of the battery for the round trip, no margin
  miles: number;                    // round trip
  source: 'drives' | 'route';
  legs: number;                     // measured legs found (0 when routed)
}

// The work point's cell and its 8 neighbours: a lot that straddles a cell edge,
// or a work point a little off the parking spot, still matches.
export function workCells(salt: Buffer | string, work: LatLon): Set<string> {
  const dLat = CELL_M / M_PER_DEG_LAT;
  const dLon = CELL_M / (M_PER_DEG_LAT * Math.max(0.01, Math.cos((work.lat * Math.PI) / 180)));
  const out = new Set<string>();
  for (const dy of [-1, 0, 1]) for (const dx of [-1, 0, 1]) out.add(cellOf(salt, { lat: work.lat + dy * dLat, lon: work.lon + dx * dLon }));
  return out;
}

function median(xs: number[]): number | null {
  if (!xs.length) return null;
  const s = [...xs].sort((a, b) => a - b);
  const m = Math.floor(s.length / 2);
  return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
}

// Medians of the recent home->work and work->home drives in the window.
export function commuteLegs(
  drives: CommuteDrive[], cells: Set<string>, nowMs: number,
): { out: number | null; back: number | null; n: number } {
  const since = nowMs - COMMUTE_WINDOW_DAYS * 86_400_000;
  const sorted = drives
    .filter((d) => { const t = Date.parse(d.start); return Number.isFinite(t) && t >= since && t <= nowMs && d.miles > 0; })
    .sort((a, b) => Date.parse(a.start) - Date.parse(b.start));
  const out: number[] = [];
  const back: number[] = [];
  for (const d of sorted) {
    const toWork = d.at_home === true && !!d.end_cell && cells.has(d.end_cell);
    const fromWork = d.end_at_home === true && !!d.start_cell && cells.has(d.start_cell);
    if (toWork && !fromWork) out.push(d.miles);
    else if (fromWork && !toWork) back.push(d.miles);
  }
  return {
    out: median(out.slice(-COMMUTE_RECENT_LEGS)),
    back: median(back.slice(-COMMUTE_RECENT_LEGS)),
    n: out.length + back.length,
  };
}

// The daily-use need with the floor under its work-day value, so the status,
// Influx and /trips show what the plan used. Unchanged when the need is higher.
export function flooredNeed<T extends { work: number | null; workSource: string }>(
  n: T | null, f: CommuteFloor | null,
): T | null {
  if (!n || !f || (n.work !== null && n.work >= f.pct)) return n;
  return { ...n, work: f.pct, workSource: 'commute' };
}

export async function commuteFloor(p: {
  work: LatLon | null;
  home: LatLon | null;
  drives: CommuteDrive[];
  salt: Buffer | string;
  nowMs: number;
  highF: number | null;
  route: (a: LatLon, b: LatLon) => Promise<Leg | null>;
}): Promise<CommuteFloor | null> {
  if (!p.work) return null;
  const legs = commuteLegs(p.drives, workCells(p.salt, p.work), p.nowMs);
  const one = legs.out ?? legs.back;
  const measured = one === null ? null : legs.out !== null && legs.back !== null ? legs.out + legs.back : 2 * one;
  let miles: number | null = null;
  let source: CommuteFloor['source'] = 'drives';
  if (measured !== null && legs.n >= COMMUTE_MIN_LEGS) miles = measured;
  else if (p.home) {
    const [there, back] = await Promise.all([p.route(p.home, p.work), p.route(p.work, p.home)]);
    const routed = there && back ? there.miles + back.miles : there ? 2 * there.miles : back ? 2 * back.miles : null;
    if (routed !== null && routed > 0) { miles = routed; source = 'route'; }
  }
  if (miles === null && measured !== null) miles = measured;   // one leg beats nothing
  if (miles === null || !(miles > 0)) return null;
  return { pct: pctForMiles(miles, p.highF), miles, source, legs: source === 'route' ? 0 : legs.n };
}
