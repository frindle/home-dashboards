// "Lab" for the panel: POST /api/panel/lab stores the newest list of things waiting on a person plus the model queue
// size, in memory. The panel feed reads it into its "lb" section. Off when PANEL_LAB_TOKEN is unset (the route answers
// 404 and the feed carries no "lb" key, so the panel hides the Lab topic and its bytes are what they were before).
// No reporter is shipped; whatever runs the lab posts this contract.
//
// JSON contract (v1). Only these keys are allowed; anything else is a 400:
//   {
//     "v": 1,
//     "items": [ { "text": "Approve model swap", "since": 1766250000, "warn": true } ],   // 0..6 items
//     "queue": { "total": 7, "running": 2 }                                               // optional
//   }
//   - text: 1..28 printable characters (letters, digits, space and . , : ; - _ / ' # + %)
//   - since: epoch seconds or ISO 8601 string, when the item started waiting; OR "age_s": seconds already waiting (one of them, both optional)
//   - warn: optional boolean, shows the item in amber
//   - queue.total / queue.running: whole numbers 0..9999, running <= total
//   - body at most MAX_BODY bytes; Authorization: Bearer <PANEL_LAB_TOKEN>
// The server stamps the report with its own receive time. Replies: 200 {ok:1}, 400 bad body, 401 bad token,
// 404 feature off, 413 too large.
import { tokenMatches } from './panelPresence.ts';
import type { Section } from './panelFeed.ts';

export const MAX_BODY = 1024;
export const MAX_ITEMS = 6;
const KEY = '__panelLab';

export interface LabItem { text: string; sinceS: number; warn: boolean }   // sinceS: epoch seconds
export interface LabReport { atMs: number; items: LabItem[]; queue?: { total: number; running: number } }
export interface LabConfig { token: string; staleMs: number }

const g = globalThis as unknown as Record<string, { rep: LabReport | null } | undefined>;
const store = (g[KEY] ??= { rep: null });
export const readLab = (): LabReport | null => store.rep;
export const recordLab = (r: LabReport): void => { store.rep = r; };
export const clearLab = (): void => { store.rep = null; };

export function parseLabConfig(env: Record<string, string | undefined>): LabConfig | null {
  const token = env.PANEL_LAB_TOKEN?.trim();
  if (!token) return null;
  const s = Number(env.PANEL_LAB_STALE_S);
  const staleS = env.PANEL_LAB_STALE_S?.trim() && Number.isFinite(s) && s >= 60 ? Math.min(86_400, Math.round(s)) : 1200;
  return { token, staleMs: staleS * 1000 };
}

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const onlyKeys = (o: Record<string, unknown>, allowed: string[]) => Object.keys(o).every((k) => allowed.includes(k));
const whole = (v: unknown) => typeof v === 'number' && Number.isInteger(v) && v >= 0 && v <= 9999;

function epoch(v: unknown): number | null {
  const s = typeof v === 'number' ? v : typeof v === 'string' && v.length <= 40 ? Date.parse(v) / 1000 : NaN;
  return Number.isFinite(s) && s > 1e9 && s < 4e9 ? Math.round(s) : null;
}

/** Strict validation. Returns the report (stamped with atMs) or a short error text. */
export function validateLab(body: unknown, atMs: number): LabReport | string {
  if (!isObj(body) || !onlyKeys(body, ['v', 'items', 'queue'])) return 'unknown or missing keys';
  if (body.v !== 1) return 'v must be 1';
  if (!Array.isArray(body.items) || body.items.length > MAX_ITEMS) return `items: a list of at most ${MAX_ITEMS}`;
  const rep: LabReport = { atMs, items: [] };
  for (const it of body.items) {
    if (!isObj(it) || !onlyKeys(it, ['text', 'since', 'age_s', 'warn'])) return 'items: bad item';
    if (typeof it.text !== 'string' || !/^[A-Za-z0-9 .,:;_/'#+%-]{1,28}$/.test(it.text)) return 'items: bad text';
    if (it.since !== undefined && it.age_s !== undefined) return 'items: send since or age_s, not both';
    if (it.warn !== undefined && typeof it.warn !== 'boolean') return 'items: warn must be true or false';
    let sinceS = Math.floor(atMs / 1000);
    if (it.since !== undefined) { const e = epoch(it.since); if (e === null) return 'items: bad since'; sinceS = Math.min(e, sinceS); }
    if (it.age_s !== undefined) {
      if (typeof it.age_s !== 'number' || !Number.isFinite(it.age_s) || it.age_s < 0 || it.age_s > 3_153_600_000) return 'items: bad age_s';
      sinceS = Math.floor(atMs / 1000) - Math.round(it.age_s);
    }
    rep.items.push({ text: it.text, sinceS, warn: it.warn === true });
  }
  if (body.queue !== undefined) {
    const q = body.queue;
    if (!isObj(q) || !onlyKeys(q, ['total', 'running']) || !whole(q.total) || !whole(q.running)) return 'queue: whole numbers 0..9999';
    if ((q.running as number) > (q.total as number)) return 'queue: running is more than total';
    rep.queue = { total: q.total as number, running: q.running as number };
  }
  return rep;
}

const json = (status: number, body: unknown): Response =>
  new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });

export async function handleLab(req: Request, cfg: LabConfig | null, now: () => number = Date.now): Promise<Response> {
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
  const rep = validateLab(parsed, now());
  if (typeof rep === 'string') return json(400, { error: rep });
  recordLab(rep);
  return json(200, { ok: 1 });
}

/**
 * The feed's "lb" section:
 *   st 1 = the newest report is older than the stale window, age = seconds since it arrived
 *   i: [{ t text, a seconds waiting, w 1 when warn }]   q total in the model queue, r running
 * ok 0 = nothing reported yet.
 */
export function labSection(rep: LabReport | null, staleMs: number, nowMs: number): Section {
  if (!rep) return { ok: 0, st: 0 };
  const nowS = Math.floor(nowMs / 1000);
  const out: Section = { ok: 1, st: nowMs - rep.atMs > staleMs ? 1 : 0, age: Math.max(0, Math.round((nowMs - rep.atMs) / 1000)) };
  out.i = rep.items.map((x) => ({ t: x.text, a: Math.max(0, nowS - x.sinceS), ...(x.warn ? { w: 1 } : {}) }));
  if (rep.queue) { out.q = rep.queue.total; out.r = rep.queue.running; }
  return out;
}
