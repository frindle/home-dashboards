// Claude usage for the panel: POST /api/panel/usage { ... } stores the newest numbers in memory, the panel feed reads
// them into its "us" section. The dashboard never sees a Claude login: a small reporter on the machine that runs
// Claude Code posts the numbers (firmware/crowpanel-21/usage-reporter). Off when PANEL_USAGE_TOKEN is unset (the route
// answers 404 and the feed carries no "us" key and no usage alerts, so its bytes are what they were before).
//
// JSON contract (v1). Only these keys are allowed; anything else is a 400:
//   {
//     "v": 1,
//     "five_hour": { "used_pct": 37.5, "resets_at": 1766250000 },   // 5-hour window
//     "weekly":    { "used_pct": 12,   "resets_at": 1766800000 },   // 7-day window
//     "models": [ { "name": "Opus", "used_pct": 20, "resets_at": 1766800000 } ]   // optional per-model weekly limits, at most 4
//   }
//   - at least one of five_hour / weekly / models
//   - used_pct: number 0..1000 (above 100 is shown as 100)
//   - resets_at: epoch seconds (or an ISO 8601 string); optional, but without it the panel cannot show the reset
//   - name: 1..12 characters from A-Z a-z 0-9 space . _ -
//   - body at most MAX_BODY bytes; Authorization: Bearer <PANEL_USAGE_TOKEN>
// The server stamps the report with its own receive time, so a reporter with a wrong clock cannot make data look fresh.
// Replies: 200 {ok:1}, 400 bad body, 401 bad token, 404 feature off, 413 too large.
import { tokenMatches } from './panelPresence.ts';
import type { Section } from './panelFeed.ts';

export const MAX_BODY = 1024;
export const MAX_MODELS = 4;
export const FIVE_HOUR_S = 5 * 3600;
const KEY = '__panelUsage';

export interface UsageWindow { used: number; resetsAt?: number }   // used: percent, resetsAt: epoch seconds
export interface UsageReport { atMs: number; h5?: UsageWindow; wk?: UsageWindow; models: (UsageWindow & { name: string })[] }
export interface UsageConfig { token: string; staleMs: number }

const g = globalThis as unknown as Record<string, { rep: UsageReport | null } | undefined>;
const store = (g[KEY] ??= { rep: null });
export const readUsage = (): UsageReport | null => store.rep;
export const recordUsage = (r: UsageReport): void => { store.rep = r; };
export const clearUsage = (): void => { store.rep = null; };

export function parseUsageConfig(env: Record<string, string | undefined>): UsageConfig | null {
  const token = env.PANEL_USAGE_TOKEN?.trim();
  if (!token) return null;
  const s = Number(env.PANEL_USAGE_STALE_S);
  const staleS = env.PANEL_USAGE_STALE_S?.trim() && Number.isFinite(s) && s >= 60 ? Math.min(86_400, Math.round(s)) : 1200;
  return { token, staleMs: staleS * 1000 };
}

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const onlyKeys = (o: Record<string, unknown>, allowed: string[]) => Object.keys(o).every((k) => allowed.includes(k));

function epoch(v: unknown): number | undefined | null {   // undefined: absent, null: invalid
  if (v === undefined) return undefined;
  let s: number;
  if (typeof v === 'number') s = v;
  else if (typeof v === 'string' && v.length <= 40) s = Date.parse(v) / 1000;
  else return null;
  return Number.isFinite(s) && s > 1e9 && s < 4e9 ? Math.round(s) : null;
}

function win(v: unknown): UsageWindow | string {
  if (!isObj(v) || !onlyKeys(v, ['used_pct', 'resets_at', 'name'])) return 'bad window';
  const u = v.used_pct;
  if (typeof u !== 'number' || !Number.isFinite(u) || u < 0 || u > 1000) return 'used_pct must be a number 0..1000';
  const r = epoch(v.resets_at);
  if (r === null) return 'resets_at must be epoch seconds or an ISO date';
  return { used: u, ...(r !== undefined ? { resetsAt: r } : {}) };
}

/** Strict validation. Returns the report (stamped with atMs) or a short error text. */
export function validateUsage(body: unknown, atMs: number): UsageReport | string {
  if (!isObj(body) || !onlyKeys(body, ['v', 'five_hour', 'weekly', 'models'])) return 'unknown or missing keys';
  if (body.v !== 1) return 'v must be 1';
  const rep: UsageReport = { atMs, models: [] };
  if (body.five_hour !== undefined) { const w = win(body.five_hour); if (typeof w === 'string') return `five_hour: ${w}`; rep.h5 = w; }
  if (body.weekly !== undefined) { const w = win(body.weekly); if (typeof w === 'string') return `weekly: ${w}`; rep.wk = w; }
  if (body.models !== undefined) {
    if (!Array.isArray(body.models) || body.models.length > MAX_MODELS) return `models: at most ${MAX_MODELS}`;
    for (const m of body.models) {
      const w = win(m);
      if (typeof w === 'string') return `models: ${w}`;
      const name = (m as Record<string, unknown>).name;
      if (typeof name !== 'string' || !/^[A-Za-z0-9 ._-]{1,12}$/.test(name)) return 'models: bad name';
      rep.models.push({ name, ...w });
    }
  }
  if (!rep.h5 && !rep.wk && !rep.models.length) return 'send at least one of five_hour, weekly, models';
  return rep;
}

const json = (status: number, body: unknown): Response =>
  new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });

export async function handleUsage(req: Request, cfg: UsageConfig | null, now: () => number = Date.now): Promise<Response> {
  if (!cfg) return json(404, { error: 'off' });
  const auth = req.headers.get('authorization') ?? '';
  const given = auth.startsWith('Bearer ') ? auth.slice(7).trim() : '';
  if (!given || !tokenMatches(given, cfg.token)) return json(401, { error: 'unauthorized' });
  const len = Number(req.headers.get('content-length') ?? 0);
  if (len > MAX_BODY) return json(413, { error: 'too large' });
  let text: string;
  try { text = await req.text(); } catch { return json(400, { error: 'bad body' }); }
  if (Buffer.byteLength(text) > MAX_BODY) return json(413, { error: 'too large' });
  let parsed: unknown;
  try { parsed = JSON.parse(text); } catch { return json(400, { error: 'bad json' }); }
  const rep = validateUsage(parsed, now());
  if (typeof rep === 'string') return json(400, { error: rep });
  recordUsage(rep);
  return json(200, { ok: 1 });
}

const rnd1 = (n: number) => Math.round(n * 10) / 10;

/**
 * The feed's "us" section:
 *   st 1 = the newest report is older than the stale window (numbers are shown as old data, raise no alerts)
 *   age  seconds since the report arrived
 *   h5 / wk: { p percent used (0..100), r seconds until reset }, m: [{ n, p, r }] per-model weekly limits
 *   bp  5-hour pace, percent per hour averaged over the part of the window that has passed (needs 10 min of it)
 *   lm  seconds until the 5-hour limit at that pace, only when that is before the window resets
 * A window whose reset time has passed is left out (its number is from the previous window). ok 0 = nothing to show.
 */
export function usageSection(rep: UsageReport | null, staleMs: number, nowMs: number): Section {
  if (!rep) return { ok: 0, st: 0 };
  const nowS = Math.floor(nowMs / 1000);
  const one = (w: UsageWindow | undefined) => {
    if (!w) return undefined;
    if (w.resetsAt !== undefined && w.resetsAt <= nowS) return undefined;
    return { p: Math.min(100, Math.round(w.used)), ...(w.resetsAt !== undefined ? { r: w.resetsAt - nowS } : {}) };
  };
  const h5 = one(rep.h5), wk = one(rep.wk);
  const m = rep.models.flatMap((x) => { const w = one(x); return w ? [{ n: x.name, ...w }] : []; });
  if (!h5 && !wk && !m.length) return { ok: 0, st: 0 };
  let bp: number | undefined, lm: number | undefined;
  if (h5 && h5.r !== undefined && h5.r <= FIVE_HOUR_S) {
    const elapsed = FIVE_HOUR_S - h5.r;
    if (elapsed >= 600 && rep.h5) {
      const pace = rep.h5.used / (elapsed / 3600);   // percent per hour
      bp = rnd1(pace);
      if (pace > 0) {
        const left = rep.h5.used >= 100 ? 0 : Math.round(((100 - rep.h5.used) / pace) * 3600);
        if (left < h5.r) lm = left;
      }
    }
  }
  const out: Section = { ok: 1, st: nowMs - rep.atMs > staleMs ? 1 : 0, age: Math.max(0, Math.round((nowMs - rep.atMs) / 1000)) };
  if (h5) out.h5 = h5;
  if (wk) out.wk = wk;
  if (m.length) out.m = m;
  if (bp !== undefined) out.bp = bp;
  if (lm !== undefined) out.lm = lm;
  return out;
}
