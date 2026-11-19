// The personal calendar (EV charge plan #8): errands and trips, from a second
// ICS feed. Inert unless PERSONAL_CAL_URL is set: nothing is fetched and the
// plan is untouched. Only events with a location count. Event titles are
// never kept (the planner has no use for them) and nothing here is logged but
// counts.
//
// Env: PERSONAL_CAL_URL (a Nextcloud `...?export` URL, webcal:// is fetched as
// https://), optional PERSONAL_CAL_USER + PERSONAL_CAL_PASSWORD for basic auth.
//
// Extra calendars (a dogs calendar, say) join the same list: PERSONAL_CAL_EXTRA is
// `label=url|label=url`, with the login in PERSONAL_CAL_EXTRA_USER / _PASSWORD (else the
// personal calendar's). Their events count as drives like the personal calendar's, but an
// event with no location counts as one round trip of PERSONAL_CAL_EXTRA_DEFAULT_MILES
// (default 10). A feed that is missing or fails is logged by name and left out; the rest
// of the plan carries on.
import { localDayNumber } from './chargePlan.ts';
import { parseIcsEvents, type LatLon } from './ics.ts';

export const TRIP_DAYS_AHEAD = 7;
const REFRESH_MS = 6 * 3_600_000;
const RETRY_MS = 15 * 60_000;
const DAY_MS = 86_400_000;

export type HomeTag = 'home' | 'straight';

export interface PersonalEvent {
  uid: string;
  day: number;            // local start day
  startMs: number;        // all-day: local midnight of the first day
  endMs: number;          // all-day: exclusive end (midnight after the last day)
  allDay: boolean;
  multiDay: boolean;      // away overnight: 24h or more, or an all-day event over 2+ days
  location: string;       // in memory only
  geo: LatLon | null;     // structured location first, then GEO
  tag: HomeTag | null;    // #home / #straight in the notes
  source?: string;        // the extra calendar's label; absent for the personal calendar
  assumedMiles?: number;  // no location: assume this many round-trip miles
}

export function tagFromNotes(notes: string): HomeTag | null {
  const m = /(?:^|[^\w#])#(home|straight)\b/i.exec(notes);
  return m ? (m[1].toLowerCase() as HomeTag) : null;
}

// Every non-cancelled event that has a location (or a map point); for an extra calendar, every event.
export function parsePersonalEvents(ics: string, extra?: { label: string; defaultMiles: number }): PersonalEvent[] {
  const out: PersonalEvent[] = [];
  for (const e of parseIcsEvents(ics)) {
    if (!e.start || e.cancelled || e.open) continue;
    const located = !!(e.location || e.structuredGeo || e.geo);
    if (!located && !extra) continue;
    const startMs = e.start.ms;
    const endMs = e.end && e.end.ms > startMs ? e.end.ms : startMs;
    const allDay = e.start.allDay;
    const multiDay = allDay
      ? localDayNumber(Math.max(startMs, endMs - 1)) > localDayNumber(startMs)
      : endMs - startMs >= DAY_MS;
    out.push({
      uid: extra ? `${extra.label}:${e.uid}` : e.uid, day: localDayNumber(startMs), startMs, endMs, allDay, multiDay: located ? multiDay : false,
      location: e.location, geo: e.structuredGeo ?? e.geo, tag: tagFromNotes(e.description),
      ...(extra ? { source: extra.label } : {}),
      ...(extra && !located ? { assumedMiles: extra.defaultMiles } : {}),
    });
  }
  return out;
}

// Events starting today through `days` ahead, in time order.
export function eventsWithin(all: PersonalEvent[], nowMs: number, days = TRIP_DAYS_AHEAD): PersonalEvent[] {
  const today = localDayNumber(nowMs);
  return all.filter((e) => e.day >= today && e.day <= today + days).sort((a, b) => a.startMs - b.startMs);
}

type FetchLike = (url: string, init: { headers: Record<string, string>; signal: AbortSignal }) =>
  Promise<{ ok: boolean; status: number; text(): Promise<string> }>;

export const DEFAULT_EXTRA_MILES = 10;

export interface ExtraSource { label: string; url: string }

// `label=url|label=url`; entries without a label or a URL are dropped; a label is letters, digits, - and _.
export function extraSources(env: Record<string, string | undefined> = process.env): ExtraSource[] {
  const out: ExtraSource[] = [];
  const seen = new Set<string>();
  for (const part of (env.PERSONAL_CAL_EXTRA ?? '').split('|')) {
    const m = /^\s*([\w-]+)\s*=\s*(\S+)\s*$/.exec(part);
    if (!m || seen.has(m[1])) continue;
    seen.add(m[1]);
    out.push({ label: m[1], url: m[2] });
  }
  return out;
}

export function extraDefaultMiles(env: Record<string, string | undefined> = process.env): number {
  const n = Number(env.PERSONAL_CAL_EXTRA_DEFAULT_MILES);
  return env.PERSONAL_CAL_EXTRA_DEFAULT_MILES?.trim() && Number.isFinite(n) && n > 0 ? n : DEFAULT_EXTRA_MILES;
}

export const personalCalendarOn = (env: Record<string, string | undefined> = process.env): boolean =>
  !!env.PERSONAL_CAL_URL || extraSources(env).length > 0;

// One feed with its own cache: 6h, a failed fetch keeps the last good copy and waits 15 min.
function createFeed() {
  let cache: { events: PersonalEvent[]; atMs: number } | null = null;
  let lastTryMs = -Infinity;
  return async function fetchFeed(p: {
    now: number; raw: string; user?: string; password?: string; fetchImpl: FetchLike; refresh?: boolean;
    parse: (ics: string) => PersonalEvent[]; logName: string;
  }): Promise<PersonalEvent[] | null> {
    if (!p.refresh) {
      if (cache && p.now - cache.atMs < REFRESH_MS) return cache.events;
      if (p.now - lastTryMs < RETRY_MS) return cache?.events ?? null;
    }
    lastTryMs = p.now;
    const url = p.raw.replace(/^webcal:\/\//i, 'https://');
    try {
      const headers: Record<string, string> = {};
      if (p.user) headers.Authorization = 'Basic ' + Buffer.from(`${p.user}:${p.password ?? ''}`).toString('base64');
      const res = await p.fetchImpl(url, { headers, signal: AbortSignal.timeout(15_000) });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      cache = { events: p.parse(await res.text()), atMs: p.now };
      return cache.events;
    } catch (e) {
      console.error(`[trips] ${p.logName} fetch failed:`, e instanceof Error ? e.name : 'error');
      return cache?.events ?? null;
    }
  };
}

// The same event in two feeds (same start, end and place) counts once; the personal calendar's copy wins.
function dedupe(lists: PersonalEvent[][]): PersonalEvent[] {
  const seen = new Set<string>();
  const out: PersonalEvent[] = [];
  for (const list of lists) {
    const mine = new Set<string>();
    for (const e of list) {
      const k = `${e.startMs}|${e.endMs}|${e.location.trim().toLowerCase()}`;
      if (seen.has(k)) continue;
      mine.add(k);
      out.push(e);
    }
    for (const k of mine) seen.add(k);
  }
  return out;
}

// The personal calendar plus the extra ones, one cache per process. Never throws; null when
// nothing is configured or every feed has failed with nothing cached.
export function createPersonalCalendarSource() {
  const main = createFeed();
  const extras = new Map<string, ReturnType<typeof createFeed>>();
  return async function getPersonalEvents(
    now = Date.now(),
    env: Record<string, string | undefined> = process.env,
    fetchImpl: FetchLike = fetch as unknown as FetchLike,
    opts: { refresh?: boolean } = {},
  ): Promise<PersonalEvent[] | null> {
    const raw = env.PERSONAL_CAL_URL;
    const ex = extraSources(env);
    if (!raw && !ex.length) return null;
    const lists: PersonalEvent[][] = [];
    if (raw) {
      const l = await main({ now, raw, user: env.PERSONAL_CAL_USER, password: env.PERSONAL_CAL_PASSWORD, fetchImpl, refresh: opts.refresh, parse: (ics) => parsePersonalEvents(ics), logName: 'calendar' });
      if (l) lists.push(l);
    }
    const miles = extraDefaultMiles(env);
    for (const x of ex) {
      let feed = extras.get(x.label);
      if (!feed) { feed = createFeed(); extras.set(x.label, feed); }
      const l = await feed({
        now, raw: x.url, user: env.PERSONAL_CAL_EXTRA_USER || env.PERSONAL_CAL_USER, password: env.PERSONAL_CAL_EXTRA_USER ? env.PERSONAL_CAL_EXTRA_PASSWORD : env.PERSONAL_CAL_PASSWORD,
        fetchImpl, refresh: opts.refresh, parse: (ics) => parsePersonalEvents(ics, { label: x.label, defaultMiles: miles }), logName: `extra calendar "${x.label}"`,
      });
      if (l) lists.push(l);
    }
    return lists.length ? dedupe(lists) : null;
  };
}

export const getPersonalEvents = createPersonalCalendarSource();
