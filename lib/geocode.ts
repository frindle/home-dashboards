// Coordinates for a personal-calendar event (EV charge plan #8), in order:
// the event's X-APPLE-STRUCTURED-LOCATION geo, its GEO, a pick saved in
// keys/geo-overrides.json (by UID), then the self-hosted Nominatim
// (NOMINATIM_URL, LAN, Nevada only), then the public Nominatim for anything
// the LAN copy does not know (lib/onlineGeo.ts; off with GEO_ONLINE=0).
// Unplaced -> null (flagged on /trips, with a push).
//
// Location text goes to the LAN Nominatim first and to the public one only
// when the LAN copy found nothing. It is never logged; failures log a status only.
//
// Apple Calendar writes a location as "Place name\nstreet, city, ST zip, United
// States", and Nominatim finds none of those as written (2026-11-22: 0 of 4
// upcoming events). So a text that finds nothing is tried again in simpler forms
// (queryForms): the address line alone, without the zip and country, a
// structured street/city/state search, the street and zip without the city (a
// Las Vegas mailing city is often not the OSM place), the place name and city,
// then the street without its house number. The first form with a hit wins.
import type { LatLon } from './ics.ts';
import { ONLINE_UA, onlineGeo, spaced } from './onlineGeo.ts';

export type GeoSource = 'structured' | 'event' | 'override' | 'nominatim' | 'online';
export interface Placed extends LatLon { source: GeoSource }

// uid -> point; WORK_KEY holds the one-time work-location pick.
export type GeoOverrides = Record<string, LatLon>;
export const WORK_KEY = '__work__';

type FetchLike = (url: string, init: { signal: AbortSignal; headers?: Record<string, string> }) =>
  Promise<{ ok: boolean; status: number; json(): Promise<unknown> }>;

export interface PlaceHit extends LatLon { label: string; online?: true }

const cache = new Map<string, { hits: PlaceHit[]; atMs: number }>();
const HIT_TTL_MS = 7 * 86_400_000;
const MISS_TTL_MS = 12 * 3_600_000;   // an unplaceable event retries twice a day, so an array disk can sleep

export function clearGeocodeCache(): void { cache.clear(); }

// One Nominatim search: free text ({ q }) or structured ({ street, city, state, ... }).
export type QueryForm = Record<string, string>;

const COUNTRY = /,\s*(?:united states(?: of america)?|usa|us)\.?\s*$/i;
const ST_ZIP = /^([A-Za-z]{2})(?:\s+(\d{5})(?:-\d{4})?)?$/;
const UNIT_PART = /^(?:(?:ste|suite|unit|apt|apartment|bldg|building|fl|floor|rm|room)\b\.?\s*#?\s*|#\s*)[\w-]+$/i;
const UNIT_TAIL = /\s+(?:(?:ste|suite|unit|apt|bldg)\b\.?\s*#?\s*|#\s*)[\w-]+$/i;
const HOUSE = /^\d+[A-Za-z]?\s+/;

// The text as given, then simpler forms of it (see the header), without repeats.
export function queryForms(text: string): QueryForm[] {
  const t = text.trim();
  if (!t) return [];
  const forms: QueryForm[] = [{ q: t }];
  const lines = t.split('\n').map((s) => s.trim()).filter(Boolean);
  const addr = lines[lines.length - 1];
  let name = lines.length > 1 ? lines.slice(0, -1).join(', ') : '';
  if (lines.length > 1) forms.push({ q: addr });
  const parts = addr.replace(COUNTRY, '').split(',').map((s) => s.trim()).filter(Boolean);
  const m = parts.length >= 2 ? ST_ZIP.exec(parts[parts.length - 1]) : null;
  if (!m) {
    forms.push({ q: parts.join(', ') });
  } else {
    const state = m[1].toUpperCase(), zip = m[2] ?? '';
    const city = parts[parts.length - 2];
    let pre = parts.slice(0, -2).filter((p) => !UNIT_PART.test(p)).map((p) => p.replace(UNIT_TAIL, ''));
    // One line with the place name first ("Name, 12 Main St, City, ST"): the street starts at the house number.
    const at = pre.findIndex((p) => HOUSE.test(p));
    if (at > 0) { name ||= pre.slice(0, at).join(', '); pre = pre.slice(at); }
    const street = pre.join(', ');
    const bare = street.replace(HOUSE, '');
    if (zip) forms.push({ q: [street, city, `${state} ${zip}`].filter(Boolean).join(', ') });
    forms.push({ q: [street, city, state].filter(Boolean).join(', ') });
    if (street) forms.push({ street, city, state, country: 'us' });
    if (street && zip) forms.push({ q: `${street}, ${state} ${zip}` });
    if (name) forms.push({ q: `${name}, ${city}, ${state}` });
    if (street && bare !== street) forms.push({ street: bare, city, state, country: 'us' });
  }
  const seen = new Set<string>();
  return forms.filter((f) => {
    const k = JSON.stringify(Object.entries(f).map(([a, b]) => [a, b.toLowerCase()]));
    if (seen.has(k) || !Object.values(f).every(Boolean)) return false;
    seen.add(k);
    return true;
  });
}

// The public Nominatim asks for at most 1 request a second, so it gets only the first few forms.
const ONLINE_FORMS = 4;

async function nominatim(base: string, form: QueryForm, limit: number, online: boolean, fetchImpl: FetchLike): Promise<PlaceHit[] | null> {
  try {
    const qs = new URLSearchParams({ format: 'jsonv2', limit: String(limit), ...(online ? { countrycodes: 'us' } : {}), ...form });
    const url = `${base.replace(/\/+$/, '')}/search?${qs}`;
    const get = () => fetchImpl(url, { signal: AbortSignal.timeout(8_000), ...(online ? { headers: { 'User-Agent': ONLINE_UA } } : {}) });
    const res = online ? await spaced(url, get) : await get();
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const body = (await res.json()) as Array<{ lat?: string; lon?: string; display_name?: string }>;
    const hits: PlaceHit[] = [];
    for (const r of Array.isArray(body) ? body : []) {
      const lat = parseFloat(r.lat ?? ''), lon = parseFloat(r.lon ?? '');
      if (Number.isFinite(lat) && Number.isFinite(lon)) hits.push({ lat, lon, label: String(r.display_name ?? '').slice(0, 200), ...(online ? { online: true as const } : {}) });
    }
    return hits;
  } catch (e) {
    console.error(`[trips] ${online ? 'online ' : ''}geocoder failed:`, (e instanceof Error ? e.name : 'error'));
    return null;
  }
}

// The first form (in order) the geocoder at `base` has a hit for. null when every
// request failed; [] when at least one answered and none found anything.
async function firstHit(base: string, forms: QueryForm[], limit: number, online: boolean, fetchImpl: FetchLike): Promise<PlaceHit[] | null> {
  let answered = false;
  for (const f of forms) {
    const hits = await nominatim(base, f, limit, online, fetchImpl);
    if (hits === null) continue;
    answered = true;
    if (hits.length) return hits;
  }
  return answered ? [] : null;
}

// Up to `limit` matches for a text, in its first form that finds any
// (queryForms): the LAN Nominatim, then the public one when the LAN copy has
// none; [] when both are off, unreachable or find nothing.
export async function searchPlaces(
  text: string, env: Record<string, string | undefined> = process.env, fetchImpl: FetchLike = fetch as unknown as FetchLike,
  limit = 5, nowMs = Date.now(),
): Promise<PlaceHit[]> {
  const local = env.NOMINATIM_URL || null;
  const online = onlineGeo(env).nominatim;
  const q = text.trim();
  if ((!local && !online) || !q) return [];
  const key = `${limit}|${q.toLowerCase()}`;
  const hit = cache.get(key);
  if (hit && nowMs - hit.atMs < (hit.hits.length ? HIT_TTL_MS : MISS_TTL_MS)) return hit.hits;
  const forms = queryForms(q);
  let hits = local ? await firstHit(local, forms, limit, false, fetchImpl) : null;
  let answered = hits !== null;
  if (!hits?.length && online) {
    const o = await firstHit(online, forms.slice(0, ONLINE_FORMS), limit, true, fetchImpl);
    if (o !== null) { hits = o; answered = true; }
  }
  if (!answered) return [];          // both failed: not cached, the next run tries again
  cache.set(key, { hits: hits ?? [], atMs: nowMs });
  return hits ?? [];
}

export interface Placeable { uid: string; location: string; geo: LatLon | null }

export async function placeEvent(
  ev: Placeable, overrides: GeoOverrides, env: Record<string, string | undefined> = process.env,
  fetchImpl?: FetchLike, nowMs?: number,
): Promise<Placed | null> {
  if (ev.geo) return { ...ev.geo, source: 'event' };
  const o = ev.uid ? overrides[ev.uid] : undefined;
  if (o) return { ...o, source: 'override' };
  if (!ev.location) return null;
  const [h] = await searchPlaces(ev.location, env, fetchImpl, 1, nowMs);
  return h ? { lat: h.lat, lon: h.lon, source: h.online ? 'online' : 'nominatim' } : null;
}
