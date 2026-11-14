// Pure state machine: logs every Rivian charge session as the CAR reports it,
// independent of the Tesla Wall Connector log (charge-history.jsonl). Fed one
// dashboard poll at a time; no IO, no clock, type-only imports.
//
// Parallax freezes totalChargedEnergyKwh at its last value for hours after a
// session ends, so the first energy reading of a new session can be the
// PREVIOUS session's total. We remember the last energy seen before the
// session opened and ignore readings equal to it until the value changes.

export const END_DEBOUNCE_MS = 10 * 60_000;
export const MAX_GAP_MS = 3 * 3_600_000;

export interface ChargePoll {
  ms: number;
  isCharging: boolean;
  isPluggedIn: boolean;
  soc: number | null;
  energyKwh: number | null;
  atHome: boolean | null;
  evseType: number | null;
  tempF?: number | null;         // outside temp at this poll
  powerKw?: number | null;       // Parallax charging power, null when stale
}

export interface OpenCharge {
  startMs: number;
  lastChargingMs: number;
  lastPollMs: number;
  startSoc: number | null;
  endSoc: number | null;
  energyKwh: number | null;      // highest trusted reading
  baseline: number | null;       // frozen value to ignore until it changes
  trusted: boolean;
  anyHome: boolean;
  anyAway: boolean;
  evseType: number | null;
  // Optional so state saved before these existed still loads.
  tempSum?: number;
  tempN?: number;
  kwSum?: number;
  kwN?: number;
  peakKw?: number | null;
}

export interface ChargeState {
  open: OpenCharge | null;
  lastEnergy: number | null;     // last energy reading seen on any poll
  lastPollMs: number | null;
}

export interface CarChargeRecord {
  source: 'rivian-car' | 'roamer';
  startedAt: string;
  endedAt: string;
  durationMin: number;
  energyKwh: number | null;
  startSoc: number | null;
  endSoc: number | null;
  socDelta: number | null;
  atHome: boolean | null;
  evseType: number | null;
  gap?: true;
  tempF?: number | null;         // mean outside temp over charging polls
  avgKw?: number | null;         // live: mean Parallax power over charging polls
  peakKw?: number | null;
  // Roamer backfill rows only.
  packKwh?: number | null;
  efficiencyPct?: number | null;
}

export interface ChargeStep {
  state: ChargeState;
  session: CarChargeRecord | null;
  dropped: boolean;
}

export function initialChargeState(): ChargeState {
  return { open: null, lastEnergy: null, lastPollMs: null };
}

const round1 = (n: number) => Math.round(n * 10) / 10;
const round2 = (n: number) => Math.round(n * 100) / 100;

function finish(o: OpenCharge, gap: boolean): CarChargeRecord | null {
  const socDelta = o.startSoc !== null && o.endSoc !== null ? round1(o.endSoc - o.startSoc) : null;
  const noSoc = socDelta === null || socDelta < 1;
  const noEnergy = o.energyKwh === null || o.energyKwh < 0.2;
  if (noSoc && noEnergy) return null;
  const rec: CarChargeRecord = {
    source: 'rivian-car',
    startedAt: new Date(o.startMs).toISOString(),
    endedAt: new Date(o.lastChargingMs).toISOString(),
    durationMin: round1((o.lastChargingMs - o.startMs) / 60_000),
    energyKwh: o.energyKwh === null ? null : Math.round(o.energyKwh * 100) / 100,
    startSoc: o.startSoc,
    endSoc: o.endSoc,
    socDelta,
    atHome: o.anyHome ? true : o.anyAway ? false : null,
    evseType: o.evseType,
    tempF: o.tempN ? round1(o.tempSum! / o.tempN) : null,
    avgKw: o.kwN ? round2(o.kwSum! / o.kwN) : null,
    peakKw: o.peakKw ?? null,
  };
  if (gap) rec.gap = true;
  return rec;
}

function num(n: number | null): number | null {
  return typeof n === 'number' && Number.isFinite(n) ? n : null;
}

export function stepChargeLog(prev: ChargeState, p: ChargePoll): ChargeStep {
  let session: CarChargeRecord | null = null;
  let dropped = false;
  let open = prev.open ? { ...prev.open } : null;
  let lastEnergy = prev.lastEnergy;
  const energy = num(p.energyKwh);
  const soc = num(p.soc);

  const close = (gap: boolean) => {
    if (!open) return;
    session = finish(open, gap);
    dropped = session === null;
    open = null;
  };

  // Gap inside a session (container down): close at the last seen charging poll.
  if (open && p.ms - open.lastPollMs > MAX_GAP_MS) close(true);

  if (!open && p.isCharging) {
    open = {
      startMs: p.ms, lastChargingMs: p.ms, lastPollMs: p.ms,
      startSoc: soc, endSoc: soc, energyKwh: null,
      baseline: lastEnergy, trusted: false,
      anyHome: false, anyAway: false, evseType: null,
      tempSum: 0, tempN: 0, kwSum: 0, kwN: 0, peakKw: null,
    };
  }

  if (open) {
    open.lastPollMs = p.ms;
    if (p.isCharging) {
      open.lastChargingMs = p.ms;
      if (soc !== null) {
        if (open.startSoc === null) open.startSoc = soc;
        open.endSoc = soc;
      }
      if (energy !== null) {
        if (!open.trusted && open.baseline !== null && energy === open.baseline) {
          // frozen value from the previous session: ignore
        } else {
          open.trusted = true;
          if (open.energyKwh === null || energy > open.energyKwh) open.energyKwh = energy;
        }
      }
      if (p.atHome === true) open.anyHome = true;
      else if (p.atHome === false) open.anyAway = true;
      if (open.evseType === null && p.evseType !== null && p.evseType !== undefined) open.evseType = p.evseType;
      const t = num(p.tempF ?? null);
      if (t !== null) { open.tempSum = (open.tempSum ?? 0) + t; open.tempN = (open.tempN ?? 0) + 1; }
      const kw = num(p.powerKw ?? null);
      if (kw !== null && kw > 0) {
        open.kwSum = (open.kwSum ?? 0) + kw; open.kwN = (open.kwN ?? 0) + 1;
        if (open.peakKw === null || open.peakKw === undefined || kw > open.peakKw) open.peakKw = kw;
      }
    } else if (!p.isPluggedIn || p.ms - open.lastChargingMs >= END_DEBOUNCE_MS) {
      close(false);
    }
  }

  if (energy !== null) lastEnergy = energy;
  return { state: { open, lastEnergy, lastPollMs: p.ms }, session, dropped };
}
