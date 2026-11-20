// Where work is, and the commute floor from it (lib/commuteFloor.ts). Shared by
// the night plan and the trip layer. No place or coordinate is logged.
import { readConfig } from '@/lib/config';
import { commuteFloor, type CommuteFloor } from '@/lib/commuteFloor';
import { driveCellSalt, readDrives } from '@/lib/driveLogStore';
import { searchPlaces, WORK_KEY } from '@/lib/geocode';
import type { LatLon } from '@/lib/ics';
import { getShiftCalendar } from '@/lib/shiftCalendar';
import { readGeoOverrides } from '@/lib/tripStore';
import { fileRouteCache, routeLeg } from '@/lib/valhalla';

export const routeCache = fileRouteCache();

// The place picked on /trips, else the shift calendar's place: its coordinates,
// else its address looked up (lib/geocode.ts decides LAN or online).
export async function placeWork(nowMs: number): Promise<LatLon | null> {
  const picked = (await readGeoOverrides())[WORK_KEY];
  if (picked) return picked;
  const wp = (await getShiftCalendar(nowMs))?.workPlace ?? null;
  if (!wp) return null;
  if (wp.geo) return wp.geo;
  const [h] = wp.location ? await searchPlaces(wp.location, process.env, undefined, 1, nowMs) : [];
  return h ? { lat: h.lat, lon: h.lon } : null;
}

// The serve day's commute floor; null when work is unknown or nothing measures
// the trip. Never throws: the plan goes on without a floor.
export async function commuteFloorNow(nowMs: number, highF: number | null): Promise<CommuteFloor | null> {
  try {
    const work = await placeWork(nowMs);
    if (!work) return null;
    const h = readConfig().home;
    const home: LatLon | null = h.lat !== null && h.lon !== null ? { lat: h.lat, lon: h.lon } : null;
    const [drives, salt] = await Promise.all([readDrives(), driveCellSalt()]);
    await routeCache.load();
    return await commuteFloor({
      work, home, drives, salt, nowMs, highF,
      route: (a, b) => routeLeg(a, b, { cache: routeCache, salt }),
    });
  } catch (e) {
    console.error('[plan] commute floor failed:', e instanceof Error ? e.name : 'error');
    return null;
  }
}
