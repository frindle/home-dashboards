// Desk Mac presence for the panel backlight: POST /api/panel/presence { "locked": true|false }.
// In memory only (a restart forgets it, and the Mac's 60 s heartbeat refills it). Off when PANEL_PRESENCE_TOKEN is unset.
import { createHash, timingSafeEqual } from 'node:crypto';
import { awayFromStates, parsePresenceConfig, type HaPresence, type PresenceConfig, type PresenceReport } from './panelFeed.ts';

const MAX_BODY = 256;
const KEY = '__panelPresence';

// One copy per process: Next may bundle the feed route and the presence route separately.
const g = globalThis as unknown as Record<string, { rep: PresenceReport | null } | undefined>;
const store = (g[KEY] ??= { rep: null });

export const readPresence = (): PresenceReport | null => store.rep;
export const recordPresence = (away: boolean, atMs: number): void => { store.rep = { away, atMs }; };   // the POST says "locked", which means away
export const clearPresence = (): void => { store.rep = null; };

// Constant-time compare: hash both sides so the lengths match and nothing leaks about the token's length.
export function tokenMatches(given: string, want: string): boolean {
  const a = createHash('sha256').update(given).digest();
  const b = createHash('sha256').update(want).digest();
  return timingSafeEqual(a, b);
}

const json = (status: number, body: unknown): Response =>
  new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });

export async function handlePresence(req: Request, cfg: PresenceConfig | null, now: () => number = Date.now): Promise<Response> {
  if (!cfg?.token) return json(404, { error: 'off' });
  const auth = req.headers.get('authorization') ?? '';
  const given = auth.startsWith('Bearer ') ? auth.slice(7).trim() : '';
  if (!given || !tokenMatches(given, cfg.token!)) return json(401, { error: 'unauthorized' });
  const len = Number(req.headers.get('content-length') ?? 0);
  if (len > MAX_BODY) return json(413, { error: 'too large' });
  let text: string;
  try { text = await req.text(); } catch { return json(400, { error: 'bad body' }); }
  if (text.length > MAX_BODY) return json(413, { error: 'too large' });
  let locked: unknown;
  try { locked = (JSON.parse(text) as { locked?: unknown })?.locked; } catch { return json(400, { error: 'bad json' }); }
  if (typeof locked !== 'boolean') return json(400, { error: 'locked must be true or false' });
  recordPresence(locked, now());
  return json(200, { ok: 1 });
}

export const presenceConfig = (env: Record<string, string | undefined>): PresenceConfig | null => parsePresenceConfig(env);

// ---------- Home Assistant (primary source) ----------

const HA_TIMEOUT_MS = 2_000;
const HA_OK_CACHE_MS = 2_000;     // the panel polls every 10 s; this keeps a change visible within about 10 s
const HA_FAIL_CACHE_MS = 10_000;  // a dead Home Assistant costs the feed at most one 2 s wait per 10 s

async function fetchState(ha: HaPresence, entity: string, fetchFn: typeof fetch): Promise<string | null> {
  try {
    const res = await fetchFn(`${ha.url}/api/states/${encodeURIComponent(entity)}`, {
      headers: { Authorization: `Bearer ${ha.haToken}` },
      signal: AbortSignal.timeout(HA_TIMEOUT_MS),
    });
    if (!res.ok) return null;
    const st = ((await res.json()) as { state?: unknown })?.state;
    return typeof st === 'string' ? st : null;
  } catch { return null; }
}

// All entities in parallel. Never throws; null when no entity gave a usable state. A state's age is not checked: a
// Mac that stays locked for hours keeps one state, and a dead Home Assistant returns null anyway.
export async function fetchHaPresence(ha: HaPresence, nowMs: number, fetchFn: typeof fetch = fetch): Promise<PresenceReport | null> {
  const away = awayFromStates(await Promise.all(ha.entities.map((e) => fetchState(ha, e, fetchFn))), ha.presentStates);
  return away === null ? null : { away, atMs: nowMs };
}

// Home Assistant first, then the POSTed report. The HA read is cached and shared by concurrent callers.
export function makePresenceReader(cfg: PresenceConfig | null, now: () => number = Date.now, fetchFn: typeof fetch = fetch) {
  let at = 0, ttl = 0;
  let last: PresenceReport | null = null;
  let inflight: Promise<PresenceReport | null> | null = null;
  return async function read(): Promise<PresenceReport | null> {
    if (cfg?.ha) {
      const ha = cfg.ha;
      if (!(at && now() - at < ttl)) {
        inflight ??= fetchHaPresence(ha, now(), fetchFn).then((r) => {
          last = r; at = now(); ttl = r ? HA_OK_CACHE_MS : HA_FAIL_CACHE_MS; return r;
        }).finally(() => { inflight = null; });
        await inflight;
      }
      if (last) return last;
    }
    return readPresence();
  };
}
