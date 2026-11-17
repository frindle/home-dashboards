// Files for the drive and daily-use logs (EV charge plan #6), all in KEYS_DIR:
//   rivian-drives.jsonl       one row per drive (truth; also holds salted cell hashes)
//   rivian-daily-use.jsonl    derived per-day use, rewritten whole after each charge
//   rivian-poll-days.json     local dates that had a poll (so outages read as unknown)
//   drive-cell-salt           32 random bytes (hex), created on first use
//   rivian-drive-state.json   the drive log's machine state (the drive in progress survives a restart)
// No coordinates, odometer or address is ever written.
import { randomBytes } from 'node:crypto';
import { readFile, rename, writeFile, appendFile } from 'node:fs/promises';
import { join } from 'node:path';
import { initialDriveState, type DriveRecord, type DriveState } from './rivianDriveLog.ts';
import { computeDailyUse, type DailyUseRow, type UseCharge } from './rivianDailyUse.ts';
import { dailyUseLine, driveLine, writeInfluxLines } from './driveLogInflux.ts';

function dir(): string {
  return process.env.KEYS_DIR ?? join(process.cwd(), 'keys');
}
const drivesPath = () => join(dir(), 'rivian-drives.jsonl');
const dailyPath = () => join(dir(), 'rivian-daily-use.jsonl');
const pollDaysPath = () => join(dir(), 'rivian-poll-days.json');
const saltPath = () => join(dir(), 'drive-cell-salt');
const statePath = () => join(dir(), 'rivian-drive-state.json');

// The drive log's machine state, saved after each poll that changes it, so a
// container restart mid-drive keeps the drive (11-16 22:36: a restart at 22:45
// lost the drive home). Snapshots hold SoC, odometer, temp and the salted cell
// hash only, like the drive rows. A stale open drive is dropped by stepDriveLog
// as a gap, as before.
const num = (x: unknown): number | null => (typeof x === 'number' && Number.isFinite(x) ? x : null);
function snapOf(x: unknown): DriveState['lastParked'] {
  if (!x || typeof x !== 'object') return null;
  const o = x as Record<string, unknown>;
  const ms = num(o.ms);
  if (ms === null) return null;
  return {
    ms, soc: num(o.soc), odometerMi: num(o.odometerMi),
    atHome: typeof o.atHome === 'boolean' ? o.atHome : null, tempF: num(o.tempF),
    cell: typeof o.cell === 'string' ? o.cell : null,
  };
}
export async function readDriveState(): Promise<DriveState> {
  try {
    const s = JSON.parse(await readFile(statePath(), 'utf-8')) as Record<string, unknown> | null;
    if (s && typeof s === 'object') {
      const o = s.open as { start?: unknown; lastMs?: unknown } | null | undefined;
      const start = o ? snapOf(o.start) : null;
      const lastMs = o ? num(o.lastMs) : null;
      return {
        lastParked: snapOf(s.lastParked),
        open: start && lastMs !== null ? { start, lastMs } : null,
        skipUntilPark: s.skipUntilPark === true,
      };
    }
  } catch { /* none or unreadable */ }
  return initialDriveState();
}
export async function saveDriveState(s: DriveState): Promise<void> {
  try {
    const tmp = statePath() + '.tmp';
    await writeFile(tmp, JSON.stringify(s), { mode: 0o600 });
    await rename(tmp, statePath());
  } catch (e) { console.error('[drive-log] state save failed:', e instanceof Error ? e.message : e); }
}

let saltCache: Buffer | null = null;
export async function driveCellSalt(): Promise<Buffer> {
  if (saltCache) return saltCache;
  try {
    const hex = (await readFile(saltPath(), 'utf-8')).trim();
    if (/^[0-9a-f]{64}$/.test(hex)) return (saltCache = Buffer.from(hex, 'hex'));
  } catch { /* create below */ }
  const salt = randomBytes(32);
  await writeFile(saltPath(), salt.toString('hex'), { mode: 0o600 });
  return (saltCache = salt);
}

async function readJsonl<T>(path: string): Promise<T[]> {
  let raw: string;
  try { raw = await readFile(path, 'utf-8'); } catch { return []; }
  const out: T[] = [];
  for (const line of raw.split('\n')) {
    if (!line) continue;
    try { out.push(JSON.parse(line) as T); } catch { /* skip malformed */ }
  }
  return out;
}

// One row per drive start: a drive logged twice (a restart between the row and the
// saved state, before 11-17) counts once.
export async function readDrives(): Promise<DriveRecord[]> {
  const seen = new Set<string>();
  return (await readJsonl<DriveRecord>(drivesPath())).filter((d) => {
    if (typeof d.start !== 'string' || seen.has(d.start)) return false;
    seen.add(d.start);
    return true;
  });
}
export const readDailyUse = () => readJsonl<DailyUseRow>(dailyPath());

// jsonl = truth, Influx = best-effort copy.
export async function appendDrive(row: DriveRecord): Promise<void> {
  try { await appendFile(drivesPath(), JSON.stringify(row) + '\n'); }
  catch (e) { console.error('[drive-log] append failed:', e); }
  await writeInfluxLines([driveLine(row)]);
}

let pollDaysCache: Set<string> | null = null;
export async function readPollDays(): Promise<Set<string>> {
  if (pollDaysCache) return pollDaysCache;
  try {
    const raw = JSON.parse(await readFile(pollDaysPath(), 'utf-8')) as { days?: string[] };
    pollDaysCache = new Set(raw.days ?? []);
  } catch { pollDaysCache = new Set(); }
  return pollDaysCache;
}

export async function markPollDay(date: string): Promise<void> {
  const days = await readPollDays();
  if (days.has(date)) return;
  days.add(date);
  try { await writeFile(pollDaysPath(), JSON.stringify({ days: [...days].sort() })); }
  catch (e) { console.error('[drive-log] poll-days write failed:', e); }
}

// Recompute every day from the drive log, the Rivian's charge rows and the
// poll days; rewrite the jsonl (atomically) and mirror to Influx. Never throws.
export async function refreshDailyUse(charges: UseCharge[]): Promise<number> {
  try {
    const rows = computeDailyUse(await readDrives(), charges, await readPollDays());
    const tmp = dailyPath() + '.tmp';
    await writeFile(tmp, rows.map((r) => JSON.stringify(r)).join('\n') + (rows.length ? '\n' : ''));
    await rename(tmp, dailyPath());
    await writeInfluxLines(rows.map((r) => dailyUseLine(r)));
    return rows.length;
  } catch (e) {
    console.error('[drive-log] daily-use refresh failed:', e);
    return 0;
  }
}
