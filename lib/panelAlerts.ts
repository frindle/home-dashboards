// Priority alerts for the ESP32 panel. Pure: turns the finished feed sections into one short, ordered list that the
// panel shows on every screen (badge) and in its alert view. The panel feed (lib/panelFeed.ts) puts it in "al".
//
// One alert = { k, tp, sv, m, a }:
//   k   stable key ("silent:m4", "cache", "usage:5h"). It does not change when a number inside the message changes, so the
//       panel can tell a NEW alert from the same one moving, and can keep a snooze on it.
//   tp  topic: "bl" Darkbloom, "nw" network, "sd" storage, "ev" EV, "us" Claude usage
//   sv  "r" red (act now) or "a" amber (worth knowing; never interrupts the panel)
//   m   the plain-language message, at most ALERT_MSG_MAX characters
//   a   seconds the alert has been active (see trackSince); the panel adds its own clock to it
//
// Order: red before amber, then topic (storage, Darkbloom, network, Claude usage, EV), then key. At most MAX_ALERTS are
// sent; the full count goes in "ac" when some were cut.
//
// Thresholds have defaults and can be changed in the environment (parseAlertConfig); see .env.example.
import type { Section } from './panelFeed.ts';

export const MAX_ALERTS = 6;
export const ALERT_MSG_MAX = 90;
export const CACHE_AMBER_PCT = 80;
export const CACHE_RED_PCT = 90;

export type AlertTopic = 'bl' | 'nw' | 'sd' | 'ev' | 'us';
export interface Alert { k: string; tp: AlertTopic; sv: 'r' | 'a'; m: string; a?: number }

export interface AlertConfig {
  cacheWarn: number; cacheRed: number;     // cache pool percent full
  diskWarn: number;                        // any single array disk percent full (amber only)
  arrayWarn: number; arrayRed: number;     // whole-array percent full
  primaryWan?: string;                     // name of the main internet link; set to detect a failover
  capGb?: number; capLink?: string;        // monthly data cap in GB (off when unset); link name, default all WAN links together
  capWarn: number; capRed: number;         // percent of the cap
  usageWarn: number; usageRed: number;     // Claude usage percent (5-hour, weekly and per-model windows)
}

const pctOf = (v: string | undefined, dflt: number): number => {
  const n = Number(v);
  return v?.trim() && Number.isFinite(n) && n > 0 && n <= 100 ? n : dflt;
};

export function parseAlertConfig(env: Record<string, string | undefined>): AlertConfig {
  const cap = Number(env.PANEL_DATA_CAP_GB);
  const cfg: AlertConfig = {
    cacheWarn: pctOf(env.PANEL_CACHE_WARN_PCT, CACHE_AMBER_PCT),
    cacheRed: pctOf(env.PANEL_CACHE_RED_PCT, CACHE_RED_PCT),
    diskWarn: pctOf(env.PANEL_DISK_WARN_PCT, 95),
    arrayWarn: pctOf(env.PANEL_ARRAY_WARN_PCT, 90),
    arrayRed: pctOf(env.PANEL_ARRAY_RED_PCT, 97),
    primaryWan: env.PANEL_WAN_PRIMARY?.trim() || undefined,
    capGb: env.PANEL_DATA_CAP_GB?.trim() && Number.isFinite(cap) && cap > 0 ? cap : undefined,
    capLink: env.PANEL_DATA_CAP_LINK?.trim() || undefined,
    capWarn: pctOf(env.PANEL_CAP_WARN_PCT, 80),
    capRed: pctOf(env.PANEL_CAP_RED_PCT, 95),
    usageWarn: pctOf(env.PANEL_USAGE_WARN_PCT, 80),
    usageRed: pctOf(env.PANEL_USAGE_RED_PCT, 95),
  };
  // A warn level above its red level makes no sense: red wins, warn falls back below it.
  if (cfg.cacheWarn >= cfg.cacheRed) cfg.cacheWarn = Math.max(1, cfg.cacheRed - 1);
  if (cfg.arrayWarn >= cfg.arrayRed) cfg.arrayWarn = Math.max(1, cfg.arrayRed - 1);
  if (cfg.capWarn >= cfg.capRed) cfg.capWarn = Math.max(1, cfg.capRed - 1);
  if (cfg.usageWarn >= cfg.usageRed) cfg.usageWarn = Math.max(1, cfg.usageRed - 1);
  return cfg;
}

export const DEFAULT_ALERT_CONFIG: AlertConfig = parseAlertConfig({});

/** "5 min", "1 h 20 min", "2 d 3 h" */
export function durText(sec: number): string {
  const s = Math.max(0, Math.round(sec));
  if (s < 90) return 'under 2 min';
  const min = Math.round(s / 60);
  if (min < 120) return `${min} min`;
  const h = Math.floor(min / 60);
  if (h < 48) return `${h} h ${min % 60} min`.replace(' 0 min', '');
  return `${Math.floor(h / 24)} d ${h % 24} h`.replace(' 0 h', '');
}

const plural = (n: number, one: string, many = `${one}s`): string => `${n} ${n === 1 ? one : many}`;
const tb = (gb: number): string => (gb >= 1000 ? `${(gb / 1000).toFixed(1)} TB` : `${Math.round(gb)} GB`);

const TOPIC_RANK: Record<AlertTopic, number> = { sd: 0, bl: 1, nw: 2, us: 3, ev: 4 };

type Sections = { bl: Section; nw: Section; sd: Section; ev: Section; us?: Section };

/** What a usage window looks like inside the "us" section (see lib/panelUsage.ts). */
interface UsageWin { p?: number; r?: number }

export function collectAlerts(sec: Sections, cfg: AlertConfig = DEFAULT_ALERT_CONFIG): Alert[] {
  const out: Alert[] = [];
  const add = (k: string, tp: AlertTopic, sv: 'r' | 'a', m: string) => out.push({ k, tp, sv, m: m.slice(0, ALERT_MSG_MAX) });

  // ---- Darkbloom
  const bl = sec.bl as Section & { m?: { n: string; sl?: number }[]; al?: number };
  let silent = 0;
  for (const m of bl.m ?? []) if (m.sl) { silent++; add(`silent:${m.n}`, 'bl', 'r', `${m.n} has stopped serving Darkbloom (no activity for over 30 min).`); }
  if (bl.ok && (bl.al ?? 0) > 0 && !silent) add('bloom', 'bl', 'a', `${plural(bl.al!, 'open alert')} in the Darkbloom ledger.`);

  // ---- Storage
  const sd = sec.sd as Section & {
    arr?: string; pe?: number; na?: number; nwn?: number; tot?: { s?: number; u?: number };
    d?: { n: string; r?: string; u?: number }[];
  };
  if (sd.ok) {
    if (sd.arr && sd.arr.toUpperCase() !== 'STARTED') add('array', 'sd', 'r', `The Unraid array is ${sd.arr}. Files may be unavailable.`);
    if ((sd.pe ?? 0) > 0) add('parity', 'sd', 'r', `Parity found ${plural(sd.pe!, 'error')}. Check the array in Unraid.`);
    const cache = (sd.d ?? []).find((d) => d.r === 'c' && d.u !== undefined);
    if (cache && (cache.u ?? 0) >= cfg.cacheRed) add('cache', 'sd', 'r', `Cache pool is ${cache.u}% full. Run the mover or free space.`);
    else if (cache && (cache.u ?? 0) >= cfg.cacheWarn) add('cache', 'sd', 'a', `Cache pool is ${cache.u}% full.`);
    if ((sd.na ?? 0) > 0) add('unraid', 'sd', 'r', `${plural(sd.na!, 'unread Unraid alert')}. Read them in Unraid.`);
    else if ((sd.nwn ?? 0) > 0) add('unraid', 'sd', 'a', `${plural(sd.nwn!, 'unread Unraid warning')}.`);
    const ts = sd.tot?.s ?? 0, tu = sd.tot?.u ?? 0;
    if (ts > 0) {
      const p = Math.round((tu / ts) * 100);
      const msg = `The array is ${p}% full (${tb(tu)} of ${tb(ts)}).`;
      if (p >= cfg.arrayRed) add('arrayfull', 'sd', 'r', msg);
      else if (p >= cfg.arrayWarn) add('arrayfull', 'sd', 'a', msg);
    }
    const full = (sd.d ?? []).filter((d) => d.r !== 'c' && d.r !== 'p' && (d.u ?? 0) >= cfg.diskWarn).sort((a, b) => (b.u ?? 0) - (a.u ?? 0))[0];
    if (full) add('full', 'sd', 'a', `Disk ${full.n} is ${full.u}% full.`);
  }

  // ---- Network
  const nw = sec.nw as Section & { src?: string; l?: { n?: string; d?: number; u?: number; t1?: number[]; t7?: number[]; t30?: number[] }[] };
  const links = nw.l ?? [];
  const idle = (l: { d?: number; u?: number }) => (l.d ?? 0) < 0.01 && (l.u ?? 0) < 0.01;
  if (nw.ok && nw.src === 'wan' && links.length && links.every(idle)) add('wan', 'nw', 'a', 'No traffic on any internet link. The internet may be down.');
  if (nw.ok && nw.src === 'wan' && cfg.primaryWan) {
    const want = cfg.primaryWan.toLowerCase();
    const primary = links.find((l) => (l.n ?? '').toLowerCase() === want);
    const other = links.find((l) => l !== primary && (l.n ?? '').toLowerCase() !== 'unraid' && (l.d ?? 0) + (l.u ?? 0) >= 1);
    if (primary && idle(primary) && other) add('failover', 'nw', 'a', `${primary.n} looks down. Traffic is on ${other.n}.`);
  }
  if (nw.ok && cfg.capGb) {
    const want = cfg.capLink?.toLowerCase();
    const picked = links.filter((l) => (want ? (l.n ?? '').toLowerCase() === want : (l.n ?? '').toLowerCase() !== 'unraid'));
    const t = picked.map((l) => l.t30).filter((x): x is number[] => Array.isArray(x) && x.length === 2);
    if (t.length) {
      const used = t.reduce((s, x) => s + x[0] + x[1], 0);
      const p = Math.round((used / cfg.capGb) * 100);
      const name = cfg.capLink ?? 'Internet';
      const msg = `${name} used ${tb(used)} of its ${tb(cfg.capGb)} data cap (${p}%, last 30 days).`;
      if (p >= cfg.capRed) add('cap', 'nw', 'r', msg);
      else if (p >= cfg.capWarn) add('cap', 'nw', 'a', msg);
    }
  }

  // ---- Claude usage (only fresh numbers raise alerts; see lib/panelUsage.ts)
  const us = sec.us as (Section & { h5?: UsageWin; wk?: UsageWin; m?: (UsageWin & { n?: string })[] }) | undefined;
  if (us?.ok && !us.st) {
    const win = (k: string, label: string, w: UsageWin | undefined) => {
      if (!w || w.p === undefined) return;
      const reset = w.r !== undefined ? ` Resets in ${durText(w.r)}.` : '';
      const msg = `Claude ${label} usage is ${w.p}%.${reset}`;
      if (w.p >= cfg.usageRed) add(k, 'us', 'r', msg);
      else if (w.p >= cfg.usageWarn) add(k, 'us', 'a', msg);
    };
    win('usage:5h', '5-hour', us.h5);
    win('usage:wk', 'weekly', us.wk);
    for (const m of us.m ?? []) win(`usage:${(m.n ?? '?').toLowerCase()}`, `${m.n ?? '?'} weekly`, m);
  }

  // ---- EV
  if ((sec.ev as Section & { sf?: number }).sf) add('ev', 'ev', 'a', "Tonight's charge will fall short of the target.");

  return sortAlerts(out);
}

export function sortAlerts(list: Alert[]): Alert[] {
  return [...list].sort((a, b) => (a.sv === b.sv ? 0 : a.sv === 'r' ? -1 : 1) || TOPIC_RANK[a.tp] - TOPIC_RANK[b.tp] || a.k.localeCompare(b.k));
}

/**
 * Stamps each alert with how long it has been active. `since` (key -> first seen ms) is kept by the caller between
 * builds; keys that are gone are dropped, so an alert that clears and comes back starts again from zero. A restart of
 * the dashboard also starts every alert from zero (documented: "active" is since the dashboard first saw it).
 */
export function trackSince(since: Map<string, number>, list: Alert[], nowMs: number): Alert[] {
  const live = new Set(list.map((x) => x.k));
  for (const k of [...since.keys()]) if (!live.has(k)) since.delete(k);
  return list.map((x) => {
    if (!since.has(x.k)) since.set(x.k, nowMs);
    return { ...x, a: Math.max(0, Math.floor((nowMs - since.get(x.k)!) / 1000)) };
  });
}

export function capAlerts(list: Alert[], max = MAX_ALERTS): { al: Alert[]; ac?: number } {
  return list.length > max ? { al: list.slice(0, max), ac: list.length } : { al: list };
}
