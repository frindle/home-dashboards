// Pure: compares the car-reported Rivian charge log against the Wall Connector
// log (charge-history.jsonl rows for the Rivian) so the two sources' accuracy
// can be judged. The logs stay separate; this only pairs them by time overlap.
import type { CarChargeRecord } from './rivianChargeLog.ts';

export interface WcRow {
  startedAt: string;
  endedAt: string;
  energyKwh: number;
  startSoc?: number;
  endSoc?: number;
}

export const OVERLAP_SLACK_MS = 5 * 60_000;

export interface ComparePair {
  carStartedAt: string;
  carEndedAt: string;
  wcRows: number;
  wcKwh: number;
  carKwh: number | null;
  socDelta: number | null;
  carMinusWcKwh: number | null;
  ratio: number | null;          // carKwh / wcKwh
  wcKwhPerSocPct: number | null;
  carKwhPerSocPct: number | null;
}

export interface CompareSummary {
  pairs: number;
  ratioPairs: number;
  medianRatio: number | null;
  meanRatio: number | null;
  meanAbsDiffKwh: number | null;
  wcKwhPerSocPct: number | null;   // sum(wcKwh)/sum(socDelta) over pairs with socDelta >= 1
  carKwhPerSocPct: number | null;
  bySource: { 'rivian-car': number; roamer: number; roamerDroppedAsDuplicate: number };
}

export interface CompareResult {
  pairs: ComparePair[];
  carOnly: CarChargeRecord[];
  wcOnly: WcRow[];
  summary: CompareSummary;
}

const r3 = (n: number) => Math.round(n * 1000) / 1000;
const per = (kwh: number | null, soc: number | null) =>
  kwh !== null && soc !== null && soc >= 1 ? r3(kwh / soc) : null;

function overlapMs(aS: number, aE: number, bS: number, bE: number): number {
  return Math.min(aE, bE) - Math.max(aS, bS) + 2 * OVERLAP_SLACK_MS;
}

export function compareSources(carAll: CarChargeRecord[], wc: WcRow[]): CompareResult {
  // Live car rows win: a roamer backfill row overlapping one is a duplicate.
  const live = carAll.filter((c) => c.source !== 'roamer')
    .map((c) => ({ s: Date.parse(c.startedAt), e: Date.parse(c.endedAt) }));
  let roamerDropped = 0;
  const car = carAll.filter((c) => {
    if (c.source !== 'roamer') return true;
    const s = Date.parse(c.startedAt), e = Date.parse(c.endedAt);
    const dup = live.some((l) => overlapMs(s, e, l.s, l.e) > 0);
    if (dup) roamerDropped++;
    return !dup;
  });
  const carT = car.map((c) => ({ c, s: Date.parse(c.startedAt), e: Date.parse(c.endedAt) }));
  const groups = new Map<number, WcRow[]>();
  const wcOnly: WcRow[] = [];
  for (const w of wc) {
    const ws = Date.parse(w.startedAt), we = Date.parse(w.endedAt);
    let best = -1, bestOv = 0;
    if (Number.isFinite(ws) && Number.isFinite(we)) {
      carT.forEach((t, i) => {
        if (!Number.isFinite(t.s) || !Number.isFinite(t.e)) return;
        const ov = overlapMs(ws, we, t.s, t.e);
        if (ov > 0 && (best < 0 || ov > bestOv)) { best = i; bestOv = ov; }
      });
    }
    if (best < 0) { wcOnly.push(w); continue; }
    const g = groups.get(best) ?? [];
    g.push(w);
    groups.set(best, g);
  }

  const pairs: ComparePair[] = [];
  const carOnly: CarChargeRecord[] = [];
  car.forEach((c, i) => {
    const g = groups.get(i);
    if (!g) { carOnly.push(c); return; }
    const wcKwh = r3(g.reduce((a, w) => a + (Number.isFinite(w.energyKwh) ? w.energyKwh : 0), 0));
    const carKwh = c.energyKwh;
    let socDelta = c.socDelta;
    if (socDelta === null) {
      const withSoc = g.filter((w) => typeof w.startSoc === 'number' && typeof w.endSoc === 'number');
      if (withSoc.length) socDelta = r3(withSoc.reduce((a, w) => a + (w.endSoc! - w.startSoc!), 0));
    }
    pairs.push({
      carStartedAt: c.startedAt, carEndedAt: c.endedAt, wcRows: g.length,
      wcKwh, carKwh, socDelta,
      carMinusWcKwh: carKwh === null ? null : r3(carKwh - wcKwh),
      ratio: carKwh !== null && wcKwh > 0 ? r3(carKwh / wcKwh) : null,
      wcKwhPerSocPct: per(wcKwh, socDelta),
      carKwhPerSocPct: per(carKwh, socDelta),
    });
  });

  const ratios = pairs.map((p) => p.ratio).filter((x): x is number => x !== null).sort((a, b) => a - b);
  const diffs = pairs.map((p) => p.carMinusWcKwh).filter((x): x is number => x !== null);
  const mid = Math.floor(ratios.length / 2);
  const socPairs = pairs.filter((p) => p.socDelta !== null && p.socDelta >= 1);
  const socSum = socPairs.reduce((a, p) => a + p.socDelta!, 0);
  const carSocPairs = socPairs.filter((p) => p.carKwh !== null);
  const carSocSum = carSocPairs.reduce((a, p) => a + p.socDelta!, 0);
  return {
    pairs, carOnly, wcOnly,
    summary: {
      bySource: {
        'rivian-car': car.filter((c) => c.source !== 'roamer').length,
        roamer: car.filter((c) => c.source === 'roamer').length,
        roamerDroppedAsDuplicate: roamerDropped,
      },
      pairs: pairs.length,
      ratioPairs: ratios.length,
      medianRatio: ratios.length ? r3(ratios.length % 2 ? ratios[mid] : (ratios[mid - 1] + ratios[mid]) / 2) : null,
      meanRatio: ratios.length ? r3(ratios.reduce((a, b) => a + b, 0) / ratios.length) : null,
      meanAbsDiffKwh: diffs.length ? r3(diffs.reduce((a, b) => a + Math.abs(b), 0) / diffs.length) : null,
      wcKwhPerSocPct: socSum > 0 ? r3(socPairs.reduce((a, p) => a + p.wcKwh, 0) / socSum) : null,
      carKwhPerSocPct: carSocSum > 0 ? r3(carSocPairs.reduce((a, p) => a + p.carKwh!, 0) / carSocSum) : null,
    },
  };
}
