// Feeds the car-reported Rivian charge log (rivianChargeLog.ts) from the
// dashboard poll. Serialized, never throws. The open session is persisted to
// rivian-charge-open.json so a restart mid-charge keeps an overnight session.
// Closed sessions append to rivian-charge-history.jsonl and mirror to Influx
// (measurement ev_charge_session_car), best-effort like appendChargeHistory.
import { mkdir, readFile, writeFile } from 'fs/promises';
import { join } from 'path';
import { escapeTag, influxTarget, writeLines } from './chargeSessionInflux.ts';
import {
  initialChargeState, stepChargeLog, MAX_GAP_MS,
  type CarChargeRecord, type ChargeState,
} from './rivianChargeLog.ts';

export const CAR_MEASUREMENT = 'ev_charge_session_car';

export interface ChargeRunnerInput {
  isCharging?: boolean;
  isPluggedIn?: boolean;
  chargePercent?: number;
  totalChargedEnergyKwh?: number | null;
  evseType?: number | null;
}

function dir(): string {
  return process.env.CHARGE_HISTORY_DIR ?? process.env.KEYS_DIR ?? join(process.cwd(), 'keys');
}
const historyPath = () => join(dir(), 'rivian-charge-history.jsonl');
const openPath = () => join(dir(), 'rivian-charge-open.json');

export async function readCarChargeHistory(): Promise<CarChargeRecord[]> {
  let raw: string;
  try { raw = await readFile(historyPath(), 'utf-8'); } catch { return []; }
  const out: CarChargeRecord[] = [];
  for (const line of raw.split('\n')) {
    if (!line) continue;
    try { out.push(JSON.parse(line) as CarChargeRecord); } catch { /* skip malformed */ }
  }
  return out;
}

export function carSessionLine(r: CarChargeRecord): string | null {
  const ts = Date.parse(r.startedAt);
  if (!Number.isFinite(ts)) return null;
  const location = r.atHome === true ? 'home' : r.atHome === false ? 'away' : 'unknown';
  const evse = r.evseType === null || r.evseType === undefined ? 'unknown' : String(r.evseType);
  const fields: string[] = [];
  if (r.energyKwh !== null) fields.push(`energy_kwh=${r.energyKwh}`);
  fields.push(`duration_min=${r.durationMin}`);
  for (const [k, v] of [['start_soc', r.startSoc], ['end_soc', r.endSoc], ['soc_delta', r.socDelta]] as const) {
    if (v !== null && v !== undefined) fields.push(`${k}=${v}`);
  }
  return `${CAR_MEASUREMENT},vehicle=rivian,location=${location},evse=${escapeTag(evse)} ${fields.join(',')} ${Math.round(ts)}`;
}

async function persistOpen(s: ChargeState): Promise<void> {
  try {
    await mkdir(dir(), { recursive: true });
    await writeFile(openPath(), JSON.stringify(s));
  } catch (e) { console.error('[rivian-charge] persist failed:', e instanceof Error ? e.message : e); }
}

async function loadState(now: number): Promise<ChargeState> {
  try {
    const s = JSON.parse(await readFile(openPath(), 'utf-8')) as ChargeState;
    const last = s?.lastPollMs;
    if (s && typeof last === 'number' && now - last <= MAX_GAP_MS) return s;
  } catch { /* none or unreadable */ }
  return initialChargeState();
}

async function appendSession(r: CarChargeRecord): Promise<void> {
  try {
    await mkdir(dir(), { recursive: true });
    await writeFile(historyPath(), JSON.stringify(r) + '\n', { flag: 'a' });
  } catch (e) { console.error('[rivian-charge] append failed:', e instanceof Error ? e.message : e); }
  try {
    const target = influxTarget();
    const line = carSessionLine(r);
    if (target && line) await writeLines([line], target, undefined, 5_000);
  } catch (e) { console.error('[rivian-charge] influx mirror failed:', e instanceof Error ? e.message : e); }
}

let state: ChargeState | null = null;
let chain: Promise<unknown> = Promise.resolve();

export function recordRivianChargePoll(
  s: ChargeRunnerInput, atHome: boolean | null, now = Date.now(),
): Promise<void> {
  const run = chain.then(async () => {
    try {
      if (!state) state = await loadState(now);
      const out = stepChargeLog(state, {
        ms: now,
        isCharging: !!s.isCharging,
        isPluggedIn: !!s.isPluggedIn,
        soc: typeof s.chargePercent === 'number' ? s.chargePercent : null,
        energyKwh: typeof s.totalChargedEnergyKwh === 'number' ? s.totalChargedEnergyKwh : null,
        atHome,
        evseType: typeof s.evseType === 'number' ? s.evseType : null,
      });
      const hadOpen = state.open !== null;
      state = out.state;
      if (out.session) await appendSession(out.session);
      if (state.open || hadOpen || out.session) await persistOpen(state);
    } catch (e) {
      console.error('[rivian-charge] poll failed:', e instanceof Error ? e.message : e);
    }
  });
  chain = run;
  return run;
}
