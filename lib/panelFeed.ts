// Panel feed: one small JSON for the ESP32 round display (Elecrow CrowPanel 2.1"), GET /api/panel.
// Pure: parsers for each source and the aggregator. The IO that reads the sources is lib/panelSources.ts.
//
// Every section is { ok, st, ... }: ok=1 data present, ok=0 source down and nothing recent to show; st=1 the data is
// older than it should be (the source answered with old points, or the last good copy is shown because the source
// failed just now). One dead source never blanks the panel.
//
// Keys (short on purpose; the whole body stays under 4 KB, see PANEL_MAX_BYTES):
//   v   format version (1)                       t   server time, epoch seconds
//   lvl overall alert level: "g" green, "a" amber, "r" red
//   why up to 3 short reasons for amber/red, e.g. "silent:m4max", "array:STOPPED", "wan:down", "src:nw"
//   go  only on red: the section the panel jumps to, "bl" (a silent Mac) or "sd" (array, parity, cache >= 90%)
//   rr  only on red: stable keys of the red reasons ("silent:<mac>", "array", "parity", "cache", "unraid"), at most 4.
//       The key does not change when a number inside the reason changes (cache 91% -> 92%), so the panel can tell a NEW
//       reason (a key it has not seen in this red spell) from the same one moving. why[] is for display only.
//   al  only when something needs attention: priority alerts, most important first, at most MAX_ALERTS (6):
//       [{ k stable key, tp topic "bl"|"nw"|"sd"|"ev"|"us", sv "r" red | "a" amber, m plain-language message (<= 90 chars),
//          a seconds active }]. See lib/panelAlerts.ts. ac = true alert count, only when the list was cut.
//       Red here is the same thing as lvl "r"; rr and go stay for older firmware, the panel itself uses al.
//   br  only when PANEL_NIGHT_HOURS is set, or presence is on and says nobody is at the desk: backlight brightness the
//       panel should use now, percent 1..100
//   bl  Darkbloom per Mac (at most MAX_MACS):  m: [{ n name, on 1 serving, s state text, w watts, jobs sandbox jobs,
//                                 td earned today (cents), tm earned this month (cents), age seconds since last
//                                 power minute, sl 1 silent (alert) }],   al: open bloom-ledger alert count
//   nw  network:            src "wan" | "unraid",  l: [{ n name, d down Mbps, u up Mbps,
//                                 t1 / t7 / t30: [down GB, up GB] over the last 1 / 7 / 30 days (optional, per link) }],
//                                 tp "r" the t* periods are rolling windows (a later calendar endpoint would send "c")
//                           Links are Cox, Starlink (from the monitor) and Unraid (from Influx), at most MAX_LINKS.
//   sd  storage:            tot: { s size GB, u used GB },  d: [{ n name, r role d|c|p, u used %, sp 1 spinning,
//                                 (a multi-SSD cache pool is one "cache" row)
//                                 t temp C }],  spin spinning disk count, nd disk count,  arr array state,
//                                 par 1 parity check running, pp parity progress %, pe parity errors,
//                                 na unread Unraid alert notifications, nwn unread warnings
//   ev  tonight's charge plan (Rivian): plug 1 plugged in, soc %, lim charge limit %, amps, kwh wall kWh tonight,
//                                 h charge window hours, tgt target SoC %, sf 1 shortfall, wk 1 work day
//   us  Claude usage, only when PANEL_USAGE_TOKEN is set: h5 / wk { p % used, r seconds to reset }, m [{ n, p, r }] per-model
//                                 weekly limits, bp 5-hour pace %/h, lm seconds to the limit at that pace, age seconds since
//                                 the report, st 1 = report older than the stale window. See lib/panelUsage.ts.
//
// Missing numbers are left out of the object, never sent as null, to save bytes and parse work on the device.

export const PANEL_MAX_BYTES = 4096;
export const CACHE_MS = 15_000;
export const SOURCE_TIMEOUT_MS = 6_000;
export const LAST_GOOD_MAX_MS = 10 * 60_000;   // a failed source shows its last good copy (st=1) this long
export const INFLUX_FRESH_S = 5 * 60;          // Unraid points older than this make storage/network stale
export const MAX_MACS = 4;                     // the firmware has room for this many Darkbloom pages
export const MAX_LINKS = 4;                    // and this many network links
export const MAX_DISKS = 16;                   // and this many disk rows

import { DEFAULT_ALERT_CONFIG, capAlerts, collectAlerts, trackSince, type Alert, type AlertConfig } from './panelAlerts.ts';

export type Level = 'g' | 'a' | 'r';
export type Section = { ok: 0 | 1; st: 0 | 1; [k: string]: unknown };

// ---------- small helpers ----------

const num = (v: unknown): number | undefined => (typeof v === 'number' && Number.isFinite(v) ? v : undefined);
const rnd = (v: unknown, d = 0): number | undefined => {
  const n = num(v);
  if (n === undefined) return undefined;
  const f = 10 ** d;
  return Math.round(n * f) / f;
};
const str = (v: unknown, max = 16): string => String(v ?? '').slice(0, max);
const compact = <T extends Record<string, unknown>>(o: T): T => {
  for (const k of Object.keys(o)) if (o[k] === undefined) delete o[k];
  return o;
};

// ---------- Flux annotated CSV (Influx /api/v2/query) ----------

export type FluxRow = Record<string, string>;

// Tables are separated by blank lines; each starts with its own header row; "#..." lines are annotations.
export function parseFluxCsv(text: string): FluxRow[] {
  const rows: FluxRow[] = [];
  let header: string[] | null = null;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line) { header = null; continue; }
    if (line.startsWith('#')) continue;
    const cells = splitCsv(line);
    if (!header) { header = cells; continue; }
    const r: FluxRow = {};
    header.forEach((h, i) => { if (h) r[h] = cells[i] ?? ''; });
    rows.push(r);
  }
  return rows;
}

function splitCsv(line: string): string[] {
  const out: string[] = [];
  let cur = '';
  let q = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (q) {
      if (c === '"' && line[i + 1] === '"') { cur += '"'; i++; } else if (c === '"') q = false; else cur += c;
    } else if (c === '"') q = true;
    else if (c === ',') { out.push(cur); cur = ''; } else cur += c;
  }
  out.push(cur);
  return out;
}

// ---------- storage (Influx unraid_disk + unraid_array) ----------

const DISK_FIELDS = new Set(['fs_size_kb', 'fs_used_kb', 'fs_free_kb', 'spinning', 'temp_c']);
const ARRAY_FIELDS = new Set(['state', 'parity_running', 'parity_progress', 'parity_errors', 'notif_unread_alert', 'notif_unread_warning']);

// rows: last() of each field per series, as FluxRow (_measurement, _field, _value, _time, name, role).
export function parseStorage(rows: FluxRow[], nowS: number): Section {
  const disks = new Map<string, { role: string; f: Record<string, number> }>();
  const arr: Record<string, string | number> = {};
  let newest = 0;
  for (const r of rows) {
    const t = Date.parse(r._time ?? '') / 1000;
    if (Number.isFinite(t) && t > newest) newest = t;
    if (r._measurement === 'unraid_disk' && DISK_FIELDS.has(r._field)) {
      const key = r.name || '?';
      const d = disks.get(key) ?? { role: r.role || '', f: {} };
      const v = Number(r._value);
      if (Number.isFinite(v)) d.f[r._field] = v;
      disks.set(key, d);
    } else if (r._measurement === 'unraid_array' && ARRAY_FIELDS.has(r._field)) {
      arr[r._field] = r._field === 'state' ? (r._value ?? '') : Number(r._value);
    }
  }
  if (!disks.size && !Object.keys(arr).length) return { ok: 0, st: 0 };
  const list = [...disks.entries()].map(([name, d]) => {
    const size = d.f.fs_size_kb, used = d.f.fs_used_kb;
    return compact({
      n: str(name, 8),
      r: d.role === 'cache' ? 'c' : d.role === 'parity' ? 'p' : 'd',
      u: size && used !== undefined ? Math.round((used / size) * 100) : undefined,
      sp: d.f.spinning === undefined ? undefined : d.f.spinning ? 1 : 0,
      t: rnd(d.f.temp_c),
      _sz: size, _us: used,
    });
  }).sort((a, b) => a.n.localeCompare(b.n, undefined, { numeric: true }));
  mergeCachePool(list);
  // Parity holds no files: leave it out of the capacity total.
  let s = 0, u = 0;
  for (const d of list) {
    if (d.r !== 'p') { s += (d._sz as number | undefined) ?? 0; u += (d._us as number | undefined) ?? 0; }
    delete (d as Record<string, unknown>)._sz; delete (d as Record<string, unknown>)._us;
  }
  const nDisks = list.length;
  const spinning = list.filter((d) => d.sp === 1).length;
  list.splice(MAX_DISKS);   // the firmware shows at most MAX_DISKS rows; totals above already counted every disk
  const kbToGb = (kb: number) => Math.round(kb / 1_000_000);
  return {
    ok: 1,
    st: newest && nowS - newest > INFLUX_FRESH_S ? 1 : 0,
    ...compact({
      tot: { s: kbToGb(s), u: kbToGb(u) },
      d: list,
      spin: spinning,
      nd: nDisks,
      arr: typeof arr.state === 'string' && arr.state ? str(arr.state, 12) : undefined,
      par: arr.parity_running === undefined ? undefined : arr.parity_running ? 1 : 0,
      pp: rnd(arr.parity_progress),
      pe: rnd(arr.parity_errors),
      na: rnd(arr.notif_unread_alert),
      nwn: rnd(arr.notif_unread_warning),
    }),
  };
}

// Unraid reports each SSD of a pool as its own disk, but only the first carries the pool's size and use. Show the
// pool as one "cache" row: use % from the members that report size, hottest member's temp, spinning if any is.
type DiskRow = { n: string; r: string; u?: number; sp?: number; t?: number; _sz?: unknown; _us?: unknown };
function mergeCachePool(list: DiskRow[]): void {
  const members = list.filter((d) => d.r === 'c');
  if (members.length < 2) return;
  let sz = 0, us = 0;
  for (const m of members) if (typeof m._sz === 'number' && typeof m._us === 'number') { sz += m._sz; us += m._us; }
  const temps = members.map((m) => m.t).filter((t): t is number => t !== undefined);
  const spin = members.map((m) => m.sp).filter((x): x is number => x !== undefined);
  const pool = compact({
    n: 'cache', r: 'c',
    u: sz ? Math.round((us / sz) * 100) : undefined,
    sp: spin.length ? (spin.some((x) => x === 1) ? 1 : 0) : undefined,
    t: temps.length ? Math.max(...temps) : undefined,
    _sz: sz || undefined, _us: sz ? us : undefined,
  }) as DiskRow;
  const at = list.indexOf(members[0]);
  for (let i = list.length - 1; i >= 0; i--) if (list[i].r === 'c') list.splice(i, 1);
  list.splice(at, 0, pool);
}

// ---------- network ----------

// network-bandwidth-monitor /api/fw_wan_rates: [{ iface, name, rx_mbps, tx_mbps }]
export function parseWan(body: unknown): Section {
  if (!Array.isArray(body)) return { ok: 0, st: 0 };
  const l = body.flatMap((r: Record<string, unknown>) => {
    const d = num(r.rx_mbps), u = num(r.tx_mbps);
    if (d === undefined && u === undefined) return [];
    return [compact({ n: str(String(r.name ?? r.iface ?? '').replace(/ WAN$/, ''), 10), d: rnd(d, 1), u: rnd(u, 1) })];
  });
  return l.length ? { ok: 1, st: 0, src: 'wan', l: l.slice(0, MAX_LINKS) } : { ok: 0, st: 0 };
}

// Fallback: Unraid's own traffic, Influx unraid_net last() rx_bps / tx_bps per iface (bits/s).
export function parseUnraidNet(rows: FluxRow[], nowS: number): Section {
  const by = new Map<string, { d?: number; u?: number }>();
  let newest = 0;
  for (const r of rows) {
    if (r._measurement !== 'unraid_net') continue;
    const t = Date.parse(r._time ?? '') / 1000;
    if (Number.isFinite(t) && t > newest) newest = t;
    const v = Number(r._value);
    if (!Number.isFinite(v)) continue;
    const e = by.get(r.iface) ?? {};
    if (r._field === 'rx_bps') e.d = v / 1e6;
    else if (r._field === 'tx_bps') e.u = v / 1e6;
    by.set(r.iface, e);
  }
  const main = by.get('br0') ?? [...by.values()][0];
  if (!main) return { ok: 0, st: 0 };
  return { ok: 1, st: newest && nowS - newest > INFLUX_FRESH_S ? 1 : 0, src: 'unraid', l: [compact({ n: 'Unraid', d: rnd(main.d, 1), u: rnd(main.u, 1) })] };
}

// network-bandwidth-monitor GET /api/totals: { "1d": [{ iface, rx_bytes, tx_bytes, total_bytes }], "7d": [...], "30d": [...] }
// (it also sends 14d, which is ignored). Rolling windows, not calendar periods.
export type NetTotals = { t1?: [number, number]; t7?: [number, number]; t30?: [number, number] };
export const DEFAULT_TOTALS_MAP: Record<string, string> = { Cox: 'fw_wan_eth0', Starlink: 'fw_wan_eth3', Unraid: 'br0' };

// "Cox=fw_wan_eth0,Starlink=fw_wan_eth3,Unraid=br0" -> link name to the monitor's interface name. Junk entries are skipped.
export function parseTotalsMap(text: string | undefined): Record<string, string> {
  if (!text?.trim()) return { ...DEFAULT_TOTALS_MAP };
  const out: Record<string, string> = {};
  for (const part of text.split(',')) {
    const [k, v] = part.split('=').map((x) => x.trim());
    if (k && v) out[str(k, 10)] = v;
  }
  return Object.keys(out).length ? out : { ...DEFAULT_TOTALS_MAP };
}

const gb = (bytes: number): number => {
  const v = bytes / 1e9;
  return v >= 100 ? Math.round(v) : v >= 10 ? Math.round(v * 10) / 10 : Math.round(v * 100) / 100;
};

export function parseNetTotals(body: unknown, map: Record<string, string> = DEFAULT_TOTALS_MAP): Record<string, NetTotals> {
  const out: Record<string, NetTotals> = {};
  if (!body || typeof body !== 'object') return out;
  const periods = [['t1', '1d'], ['t7', '7d'], ['t30', '30d']] as const;
  for (const [link, iface] of Object.entries(map)) {
    const t: NetTotals = {};
    for (const [key, period] of periods) {
      const rows = (body as Record<string, unknown>)[period];
      if (!Array.isArray(rows)) continue;
      const r = rows.find((x) => x && (x as Record<string, unknown>).iface === iface) as Record<string, unknown> | undefined;
      const rx = num(r?.rx_bytes), tx = num(r?.tx_bytes);
      if (rx !== undefined && tx !== undefined) t[key] = [gb(rx), gb(tx)];
    }
    if (Object.keys(t).length) out[link] = t;
  }
  return out;
}

// Adds t1/t7/t30 to each link that has totals and marks the periods as rolling. Never changes ok/st.
export function withNetTotals(sec: Section, totals: Record<string, NetTotals> | undefined): Section {
  const l = (sec as Section & { l?: Record<string, unknown>[] }).l;
  if (!sec.ok || !l || !totals) return sec;
  let any = false;
  const l2 = l.map((x) => {
    const t = totals[String(x.n)];
    if (!t) return x;
    any = true;
    return { ...x, ...t };
  });
  return any ? { ...sec, l: l2, tp: 'r' } : sec;
}

// Adds the Unraid link (from Influx) after the monitor's WAN links. Only for a WAN section; capped at MAX_LINKS.
export function withExtraLink(sec: Section, extra: Section | undefined): Section {
  const l = (sec as Section & { l?: unknown[] }).l;
  const e = extra as (Section & { l?: unknown[] }) | undefined;
  if (!sec.ok || sec.src !== 'wan' || !l || !e?.ok || !e.l?.length) return sec;
  return { ...sec, l: [...l, e.l[0]].slice(0, MAX_LINKS) };
}

// ---------- Darkbloom (bloom-ledger) ----------

export interface BloomInputs {
  live: unknown;                                   // GET /v1/live
  alerts: unknown;                                 // GET /v1/alerts
  earned?: Record<string, { day?: number; month?: number }>;   // cents per machine id, from /v1/accounting/summary
}

export function parseBloom(inp: BloomInputs): Section {
  const mac = (inp.live as { mac?: unknown })?.mac;
  if (!Array.isArray(mac)) return { ok: 0, st: 0 };
  const alerts = Array.isArray(inp.alerts) ? (inp.alerts as Record<string, unknown>[]) : [];
  const silent = new Set(alerts.filter((a) => a.alert === 'machine-silent').map((a) => String(a.machine_id)));
  let anyStale = false;
  const m = mac.slice(0, MAX_MACS).map((r: Record<string, unknown>) => {
    const id = String(r.machine_id ?? '?');
    const e = inp.earned?.[id];
    if (r.stale === true) anyStale = true;
    const state = typeof r.darkbloom_state === 'string' ? r.darkbloom_state : '';
    return compact({
      n: str(id, 12),
      on: /serv|run|online|active/i.test(state) && !silent.has(id) ? 1 : 0,
      s: str(state, 12) || undefined,
      w: rnd(r.watts),
      jobs: rnd(r.sandbox_jobs),
      td: e?.day,
      tm: e?.month,
      age: rnd(r.age_s),
      sl: silent.has(id) ? 1 : undefined,
    });
  });
  return { ok: 1, st: anyStale ? 1 : 0, m, al: alerts.length };
}

// /v1/accounting/summary?period=day|month&machine=ID -> cents earned. totals.earned_micro is millionths of a dollar.
export function earnedCents(summary: unknown): number | undefined {
  const micro = num((summary as { totals?: { earned_micro?: unknown } })?.totals?.earned_micro);
  return micro === undefined ? undefined : Math.round(micro / 10_000);
}

// ---------- EV plan ----------

export interface EvInput {
  plugged?: boolean | null; soc?: number | null; limit?: number | null;
  amps?: number | null; kwh?: number | null; windowHours?: number | null; targetPct?: number | null;
  shortfall?: boolean | null; isWork?: boolean | null;
}
export function parseEv(i: EvInput | null): Section {
  if (!i) return { ok: 0, st: 0 };
  return {
    ok: 1, st: 0,
    ...compact({
      plug: i.plugged == null ? undefined : i.plugged ? 1 : 0,
      soc: rnd(i.soc), lim: rnd(i.limit), amps: rnd(i.amps), kwh: rnd(i.kwh, 1), h: rnd(i.windowHours, 1), tgt: rnd(i.targetPct),
      sf: i.shortfall ? 1 : undefined, wk: i.isWork == null ? undefined : i.isWork ? 1 : 0,
    }),
  };
}

// ---------- alert level ----------

export { CACHE_AMBER_PCT, CACHE_RED_PCT } from './panelAlerts.ts';

// go: the section the panel jumps to on red ("bl" Darkbloom, "sd" storage), from the first red reason.
// The older reasons are decided here; the newer ones (array full, data cap, failover, Claude usage) come from the alert
// list (lib/panelAlerts.ts) so the ring colour and the alert list always agree.
const NEWER_ALERT = (k: string) => k === 'arrayfull' || k === 'cap' || k === 'failover' || k.startsWith('usage:');

export function alertLevel(sec: { bl: Section; nw: Section; sd: Section; ev: Section; us?: Section }, cfg: AlertConfig = DEFAULT_ALERT_CONFIG): { lvl: Level; why: string[]; go?: string; rr?: string[] } {
  const red: string[] = [], amber: string[] = [], keys: string[] = [];
  let go: string | undefined;
  const bl = sec.bl as Section & { m?: { n: string; sl?: number }[]; al?: number };
  for (const m of bl.m ?? []) if (m.sl) { red.push(`silent:${m.n}`); keys.push(`silent:${m.n}`); go ??= 'bl'; }
  if (bl.ok && (bl.al ?? 0) > 0 && !red.length) amber.push(`bloom:${bl.al}`);
  const sd = sec.sd as Section & { arr?: string; pe?: number; na?: number; nwn?: number; d?: { n: string; r?: string; u?: number }[] };
  if (sd.ok) {
    const n = red.length;
    if (sd.arr && sd.arr.toUpperCase() !== 'STARTED') { red.push(`array:${sd.arr}`); keys.push('array'); }
    if ((sd.pe ?? 0) > 0) { red.push('parity:err'); keys.push('parity'); }
    const cache = (sd.d ?? []).find((d) => d.r === 'c' && d.u !== undefined);
    if (cache && (cache.u ?? 0) >= cfg.cacheRed) { red.push(`cache:${cache.u}%`); keys.push('cache'); }
    if ((sd.na ?? 0) > 0) { red.push(`unraid:${sd.na}!`); keys.push('unraid'); }
    else if ((sd.nwn ?? 0) > 0) amber.push(`unraid:${sd.nwn}w`);
    if (cache && (cache.u ?? 0) >= cfg.cacheWarn && (cache.u ?? 0) < cfg.cacheRed) amber.push(`cache:${cache.u}%`);
    const full = (sd.d ?? []).find((d) => d.r !== 'c' && (d.u ?? 0) >= cfg.diskWarn);
    if (full) amber.push(`full:${full.n}`);
    if (red.length > n) go ??= 'sd';
  }
  const nw = sec.nw as Section & { l?: { d?: number; u?: number }[] };
  if (nw.ok && nw.src === 'wan' && (nw.l ?? []).length && (nw.l ?? []).every((x) => (x.d ?? 0) < 0.01 && (x.u ?? 0) < 0.01)) amber.push('wan:idle');
  if ((sec.ev as Section & { sf?: number }).sf) amber.push('ev:short');
  for (const a of collectAlerts(sec, cfg)) {
    if (!NEWER_ALERT(a.k)) continue;
    if (a.sv === 'r') { red.push(a.k); keys.push(a.k); } else amber.push(a.k);
  }
  for (const k of ['bl', 'nw', 'sd', 'ev'] as const) {
    if (!sec[k].ok) amber.push(`src:${k}`);
    else if (sec[k].st) amber.push(`old:${k}`);
  }
  if (red.length) return { lvl: 'r', why: [...red, ...amber].slice(0, 3), ...(go ? { go } : {}), rr: keys.slice(0, 4) };
  if (amber.length) return { lvl: 'a', why: amber.slice(0, 3) };
  return { lvl: 'g', why: [] };
}

// ---------- night dimming (optional) ----------

// PANEL_NIGHT_HOURS="22-7" turns it on: between those local hours the panel is told to use PANEL_NIGHT_BRIGHTNESS
// (default 5), otherwise PANEL_DAY_BRIGHTNESS (default 60). PANEL_TZ picks the zone (default: the server's own).
export interface DimConfig { startH: number; endH: number; night: number; day: number; tz?: string }

const pct = (v: string | undefined, dflt: number): number => {
  const n = Number(v);
  return v?.trim() && Number.isFinite(n) ? Math.min(100, Math.max(1, Math.round(n))) : dflt;
};

export function parseDimConfig(env: Record<string, string | undefined>): DimConfig | null {
  const m = /^(\d{1,2})\s*-\s*(\d{1,2})$/.exec(env.PANEL_NIGHT_HOURS?.trim() ?? '');
  if (!m) return null;
  const startH = Number(m[1]), endH = Number(m[2]);
  if (startH > 23 || endH > 23 || startH === endH) return null;
  let tz = env.PANEL_TZ?.trim() || undefined;
  if (tz) { try { new Intl.DateTimeFormat('en-US', { timeZone: tz }); } catch { tz = undefined; } }
  return { startH, endH, night: pct(env.PANEL_NIGHT_BRIGHTNESS, 5), day: pct(env.PANEL_DAY_BRIGHTNESS, 60), tz };
}

export function brightnessAt(cfg: DimConfig, nowMs: number): number {
  const parts = new Intl.DateTimeFormat('en-US', { hour: 'numeric', hourCycle: 'h23', timeZone: cfg.tz }).formatToParts(new Date(nowMs));
  const h = Number(parts.find((p) => p.type === 'hour')?.value ?? 0) % 24;
  const night = cfg.startH < cfg.endH ? h >= cfg.startH && h < cfg.endH : h >= cfg.startH || h < cfg.endH;
  return night ? cfg.night : cfg.day;
}

// ---------- desk presence (optional) ----------

// Dims the panel when nobody is at the desk. Off unless one of these is set:
//  - Home Assistant (primary): PANEL_PRESENCE_ENTITY, a comma list of entity ids (with HA_URL and HA_TOKEN, as for the
//    Ecobee sync): the Mac app's "active" binary sensor, an ecobee room occupancy sensor, a person entity.
//    An entity says "present" when its state is in PANEL_PRESENCE_PRESENT_STATES (default "on,home"). Any other usable
//    state ("off", "not_home") says "away". unavailable, unknown and failed reads are ignored.
//    The panel keeps its normal level if ANY usable entity says present, and dims only when ALL usable ones say away.
//    No usable entity at all: normal day/night level.
//  - POST /api/panel/presence { locked } with PANEL_PRESENCE_TOKEN (fallback, used only when Home Assistant gave no
//    usable answer; needs the optional Mac agent in firmware/crowpanel-21/mac-presence).
// Away means PANEL_AWAY_BRIGHTNESS (default 1; the firmware treats 0 as 1, so the backlight never goes fully dark)
// instead of the day/night level. A POSTed report older than PANEL_PRESENCE_STALE_S (default 180) is ignored.
export interface HaPresence { url: string; haToken: string; entities: string[]; presentStates: string[] }
export interface PresenceConfig { token?: string; ha?: HaPresence; awayBr: number; staleMs: number }
export interface PresenceReport { away: boolean; atMs: number }

const list = (v: string | undefined): string[] => (v ?? '').split(',').map((x) => x.trim()).filter(Boolean);

export function parsePresenceConfig(env: Record<string, string | undefined>): PresenceConfig | null {
  const token = env.PANEL_PRESENCE_TOKEN?.trim() || undefined;
  const entities = list(env.PANEL_PRESENCE_ENTITY).slice(0, 8);
  const url = env.HA_URL?.trim().replace(/\/$/, ''), haToken = env.HA_TOKEN?.trim();
  const present = list(env.PANEL_PRESENCE_PRESENT_STATES);
  const ha = entities.length && url && haToken
    ? { url, haToken, entities, presentStates: (present.length ? present : ['on', 'home']).map((x) => x.toLowerCase()) }
    : undefined;
  if (!token && !ha) return null;
  const s = Number(env.PANEL_PRESENCE_STALE_S);
  const staleS = env.PANEL_PRESENCE_STALE_S?.trim() && Number.isFinite(s) && s >= 10 ? Math.min(86_400, Math.round(s)) : 180;
  return { token, ha, awayBr: pct(env.PANEL_AWAY_BRIGHTNESS, 1), staleMs: staleS * 1000 };
}

// states: one raw state per entity, null = read failed. true = away, false = present, null = nothing usable.
export function awayFromStates(states: (string | null)[], presentStates: string[]): boolean | null {
  let usable = 0;
  for (const raw of states) {
    const st = raw?.trim().toLowerCase();
    if (!st || st === 'unavailable' || st === 'unknown') continue;
    if (presentStates.includes(st)) return false;
    usable++;
  }
  return usable ? true : null;
}

// base: the day/night level (undefined when dimming is off). Returns what "br" should be, undefined = leave it out.
export function effectiveBrightness(base: number | undefined, cfg: PresenceConfig | null | undefined, rep: PresenceReport | null | undefined, nowMs: number): number | undefined {
  if (!cfg || !rep || !rep.away) return base;
  const age = nowMs - rep.atMs;
  return age >= -5_000 && age <= cfg.staleMs ? cfg.awayBr : base;   // a Home Assistant read is stamped with the read time
}

// Copy of the body with "br" set (or removed), keeping the key order buildPanel uses (br just before bl).
export function withBr(body: PanelBody, br: number | undefined): PanelBody {
  if (body.br === br) return body;
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(body)) {
    if (k === 'br') continue;
    if (k === 'bl' && br !== undefined) out.br = br;
    out[k] = v;
  }
  return out as unknown as PanelBody;
}

// ---------- aggregator ----------

export type SourceFn = () => Promise<Section>;
export interface Sources { bl: SourceFn; nw: SourceFn; sd: SourceFn; ev: SourceFn; us?: SourceFn }   // us: Claude usage, only when enabled

export interface PanelBody {
  v: 1; t: number; lvl: Level; why: string[]; go?: string; rr?: string[]; br?: number; al?: Alert[]; ac?: number;
  bl: Section; nw: Section; sd: Section; ev: Section; us?: Section;
}

export interface AggOpts {
  nowMs?: number;
  timeoutMs?: number;
  lastGood?: Map<string, { at: number; sec: Section }>;   // survives across calls (route keeps one)
  dim?: DimConfig | null;                                  // night dimming; null/undefined leaves "br" out
  alertCfg?: AlertConfig;                                  // alert thresholds (defaults when omitted)
  alertSince?: Map<string, number>;                        // alert key -> first seen ms; survives across calls
}

async function withTimeout<T>(p: Promise<T>, ms: number): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    return await Promise.race([p, new Promise<never>((_, rej) => { timer = setTimeout(() => rej(new Error('timeout')), ms); })]);
  } finally { if (timer) clearTimeout(timer); }
}

// Run every source in parallel with its own timeout. A source that throws, times out or returns ok=0 shows its last
// good copy marked st=1 for up to LAST_GOOD_MAX_MS, else { ok:0 }.
export async function buildPanel(src: Sources, opts: AggOpts = {}): Promise<PanelBody> {
  const nowMs = opts.nowMs ?? Date.now();
  const timeoutMs = opts.timeoutMs ?? SOURCE_TIMEOUT_MS;
  const good = opts.lastGood ?? new Map();
  const keys = (src.us ? ['bl', 'nw', 'sd', 'ev', 'us'] : ['bl', 'nw', 'sd', 'ev']) as ('bl' | 'nw' | 'sd' | 'ev' | 'us')[];
  const out = await Promise.all(keys.map(async (k): Promise<Section> => {
    let sec: Section;
    try { sec = await withTimeout(src[k]!(), timeoutMs); } catch { sec = { ok: 0, st: 0 }; }
    if (k === 'us') return sec;   // in-memory numbers with their own stale window: no last-good copy
    if (sec.ok) { good.set(k, { at: nowMs, sec }); return sec; }
    const g = good.get(k);
    if (g && nowMs - g.at <= LAST_GOOD_MAX_MS) return { ...g.sec, st: 1 };
    return { ok: 0, st: 0 };
  }));
  const [bl, nw, sd, ev] = out;
  const us = src.us ? out[4] : undefined;
  const cfg = opts.alertCfg ?? DEFAULT_ALERT_CONFIG;
  const { lvl, why, go, rr } = alertLevel({ bl, nw, sd, ev, ...(us ? { us } : {}) }, cfg);
  const { al, ac } = capAlerts(trackSince(opts.alertSince ?? new Map(), collectAlerts({ bl, nw, sd, ev, ...(us ? { us } : {}) }, cfg), nowMs));
  const body: PanelBody = {
    v: 1, t: Math.floor(nowMs / 1000), lvl, why, ...(go ? { go } : {}), ...(rr ? { rr } : {}),
    ...(opts.dim ? { br: brightnessAt(opts.dim, nowMs) } : {}),
    ...(al.length ? { al } : {}), ...(ac ? { ac } : {}), bl, nw, sd, ev, ...(us ? { us } : {}),
  };
  return fitBody(body);
}

// The firmware reads at most PANEL_MAX_BYTES. If a body is over (many disks, long names), drop the least important
// detail first: network totals, then disk temperatures, then per-model Claude limits, then the per-disk rows (the totals
// stay), and only then the lowest-priority alerts.
export function fitBody(body: PanelBody): PanelBody {
  const size = () => JSON.stringify(body).length;
  if (size() <= PANEL_MAX_BYTES) return body;
  const nw = body.nw as Section & { l?: Record<string, unknown>[] };
  if (nw.l) body.nw = { ...nw, l: nw.l.map(({ t1, t7, t30, ...r }) => r) };
  if (size() <= PANEL_MAX_BYTES) return body;
  const sd = body.sd as Section & { d?: Record<string, unknown>[] };
  if (sd.d) body.sd = { ...sd, d: sd.d.map(({ t, ...r }) => r) };
  if (size() <= PANEL_MAX_BYTES) return body;
  const us = body.us as (Section & { m?: unknown[] }) | undefined;
  if (us?.m) { const { m, ...rest } = us; body.us = rest as Section; }
  if (size() <= PANEL_MAX_BYTES) return body;
  const sd2 = body.sd as Section & { d?: unknown[] };
  if (sd2.d) { const { d, ...rest } = sd2; body.sd = rest as Section; }
  // Last resort: fewer alerts (they are ordered by priority, so the most important stay; "ac" keeps the true count).
  while (body.al && body.al.length > 1 && size() > PANEL_MAX_BYTES) {
    body.ac = body.ac ?? body.al.length;
    body.al = body.al.slice(0, -1);
  }
  return body;
}

// A 15 s cache around buildPanel; concurrent requests share one in-flight build. The cached build is kept as an
// object so "br" can follow a fresh lock/unlock report on every request without waiting for the cache to expire.
export interface PresenceSource { cfg: PresenceConfig | null; read: () => PresenceReport | null | Promise<PresenceReport | null> }

export function makePanelCache(src: Sources, ttlMs = CACHE_MS, now: () => number = Date.now, dim: DimConfig | null = null, presence?: PresenceSource, alertCfg?: AlertConfig) {
  const lastGood = new Map<string, { at: number; sec: Section }>();
  const alertSince = new Map<string, number>();
  let at = 0;
  let body: PanelBody | null = null;
  let inflight: Promise<PanelBody> | null = null;
  const render = async (b: PanelBody): Promise<string> => {
    if (!presence?.cfg) return JSON.stringify(b);
    const base = dim ? brightnessAt(dim, now()) : undefined;
    let rep: PresenceReport | null = null;
    try { rep = await presence.read(); } catch { rep = null; }
    return JSON.stringify(withBr(b, effectiveBrightness(base, presence.cfg, rep, now())));
  };
  return async function get(): Promise<string> {
    if (body && now() - at < ttlMs) return render(body);
    if (!inflight) {
      inflight = (async () => {
        try {
          const b = await buildPanel(src, { nowMs: now(), lastGood, dim, alertCfg, alertSince });
          body = b;
          at = now();
          return b;
        } finally { inflight = null; }
      })();
    }
    return render(await inflight);
  };
}
