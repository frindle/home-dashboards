// /charging/compare: the car-reported Rivian charge log next to the Wall Connector log, as a
// page instead of the /api/admin/charge-compare JSON (Penn 11-14: "its just json so its hard
// for me to parse"). Same data and pairing (lib/chargeSourceCompare.ts); read-only.
import Link from 'next/link';
import { readChargeHistory } from '@/lib/chargeHistory';
import { readConfig } from '@/lib/config';
import { vehicleIdResolver } from '@/lib/chargeSessionInflux';
import { readCarChargeHistory } from '@/lib/rivianChargeRunner';
import { compareSources, type ComparePair } from '@/lib/chargeSourceCompare';

export const dynamic = 'force-dynamic';

const RANGES = [30, 90, 200, 365];
// A pair this far from the usual car/wall ratio is shown as an outlier: usually a session split
// or joined differently by the two logs, not a real difference in energy.
const OUTLIER = 0.15;

const day = (iso: string) => new Date(iso).toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' });
const time = (iso: string) => new Date(iso).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
const n1 = (n: number | null | undefined) => (n === null || n === undefined ? '–' : n.toFixed(1));
const n2 = (n: number | null | undefined) => (n === null || n === undefined ? '–' : n.toFixed(2));

function Row({ p, median }: { p: ComparePair; median: number | null }) {
  const off = p.ratio !== null && median !== null && Math.abs(p.ratio - median) > OUTLIER;
  return (
    <tr className={off ? 'cmp-off' : undefined}>
      <td>{day(p.carStartedAt)}<span className="cmp-sub">{time(p.carStartedAt)}</span></td>
      <td className="mono">{n1(p.wcKwh)}</td>
      <td className="mono">{n1(p.carKwh)}</td>
      <td className="mono">{p.carMinusWcKwh === null ? '–' : `${p.carMinusWcKwh > 0 ? '+' : ''}${p.carMinusWcKwh.toFixed(1)}`}</td>
      <td className="mono">{p.socDelta === null ? '–' : `${Math.round(p.socDelta)}%`}</td>
      <td className="mono">{n2(p.wcKwhPerSocPct)}</td>
      <td className="mono">{n1(p.avgKw)}</td>
      <td className="mono">{p.tempF === null ? '–' : `${Math.round(p.tempF)}°`}</td>
    </tr>
  );
}

export default async function ChargeComparePage({ searchParams }: { searchParams: Promise<{ days?: string }> }) {
  const raw = Number((await searchParams).days ?? '90');
  const days = Math.max(1, Math.min(730, Number.isFinite(raw) ? Math.floor(raw) : 90));
  const cutoff = Date.now() - days * 86_400_000;
  const inRange = (iso: string) => Date.parse(iso) >= cutoff;
  let data: ReturnType<typeof compareSources> | null = null;
  try {
    const idFor = vehicleIdResolver(readConfig());
    const wc = (await readChargeHistory()).filter((r) => idFor(r) === 'rivian' && inRange(r.endedAt));
    const car = (await readCarChargeHistory()).filter((r) => inRange(r.endedAt));
    data = compareSources(car, wc);
  } catch { /* shown below */ }

  const s = data?.summary;
  const pct = s?.medianRatio != null ? Math.round((1 - s.medianRatio) * 100) : null;
  const pairs = data ? [...data.pairs].sort((a, b) => Date.parse(b.carStartedAt) - Date.parse(a.carStartedAt)) : [];
  const outliers = pairs.filter((p) => p.ratio !== null && s?.medianRatio != null && Math.abs(p.ratio - s.medianRatio) > OUTLIER);
  const wcOnlyKwh = data ? data.wcOnly.reduce((a, w) => a + (Number.isFinite(w.energyKwh) ? w.energyKwh : 0), 0) : 0;
  const lastPaired = pairs[0]?.carStartedAt ?? null;

  return (
    <div className="cc-page">
      <main className="cc cmp">
        <div className="cc-top">
          <h1>Car vs wall charger</h1>
          <nav aria-label="Related pages"><Link href="/charging">Charging</Link> <Link href="/">Dashboard</Link></nav>
        </div>
        <nav className="cmp-range" aria-label="Range">
          {RANGES.map((d) => <Link key={d} href={`/charging/compare?days=${d}`} className={d === days ? 'on' : undefined}>{d} days</Link>)}
        </nav>
        {!data && <p className="cc-warn">The charge logs could not be read right now.</p>}
        {s && (
          <>
            <section className="cc-head" aria-label="Summary">
              <div className="cc-stat">
                <span className="cc-k">Car vs wall</span>
                <span className="cc-v mono">{pct === null ? 'n/a' : pct >= 0 ? `${pct}% less` : `${-pct}% more`}</span>
                <span className="cc-s">Median of {s.ratioPairs} matched sessions. The gap is charging loss: the wall counts what leaves the outlet, the car what reaches the battery.</span>
              </div>
              <div className="cc-stat">
                <span className="cc-k">kWh per 1% of battery</span>
                <span className="cc-v mono">{n2(s.wcKwhPerSocPct)} wall · {n2(s.carKwhPerSocPct)} car</span>
                <span className="cc-s">The plan uses the wall number, since that is what the meter and demand charge see.</span>
              </div>
              <div className="cc-stat">
                <span className="cc-k">Typical miss per session</span>
                <span className="cc-v mono">{n1(s.meanAbsDiffKwh)} kWh</span>
                <span className="cc-s">{s.pairs} sessions matched · {outliers.length} {outliers.length === 1 ? 'outlier' : 'outliers'}</span>
              </div>
            </section>

            <section className="cc-card" aria-label="Sources">
              <h2>Where the car rows come from</h2>
              <p className="mono">{s.bySource['rivian-car']} live from the car · {s.bySource.roamer} from the Roamer export{s.bySource.roamerDroppedAsDuplicate ? ` (${s.bySource.roamerDroppedAsDuplicate} dropped as duplicates)` : ''}</p>
              {s.bySource['rivian-car'] === 0 && <p className="cc-note">No live car sessions yet. The dashboard logs one each time a charge ends, so the first should appear after the next charge.</p>}
              {data!.wcOnly.length > 0 && <p className="cc-note">{data!.wcOnly.length} wall sessions ({Math.round(wcOnlyKwh)} kWh) have no car row to compare{lastPaired ? `; the last matched session is ${day(lastPaired)}` : ''}. Those are charges the car log did not cover.</p>}
              {data!.carOnly.length > 0 && <p className="cc-note">{data!.carOnly.length} car sessions have no wall row: {data!.carOnly.filter((c) => c.atHome !== true).length} were away from home.</p>}
            </section>

            {outliers.length > 0 && (
              <section className="cc-card" aria-label="Outliers">
                <h2>Outliers</h2>
                <p className="cc-note">More than {Math.round(OUTLIER * 100)} points off the usual ratio. Usually one log split or joined a session differently. They still count in the numbers above.</p>
                <ul className="cmp-list">
                  {outliers.map((p) => <li key={p.carStartedAt} className="mono">{day(p.carStartedAt)}: wall {n1(p.wcKwh)} kWh{p.wcRows > 1 ? ` (${p.wcRows} rows)` : ''}, car {n1(p.carKwh)} kWh</li>)}
                </ul>
              </section>
            )}

            <section className="cc-card" aria-label="Sessions">
              <h2>Sessions, newest first</h2>
              {pairs.length === 0 ? <p className="cc-note">No matched sessions in this range.</p> : (
                <div className="cmp-scroll">
                  <table className="cmp-table">
                    <thead><tr><th>Started</th><th>Wall kWh</th><th>Car kWh</th><th>Car − wall</th><th>Battery added</th><th>Wall kWh per %</th><th>Avg kW</th><th>Outside</th></tr></thead>
                    <tbody>{pairs.map((p) => <Row key={p.carStartedAt} p={p} median={s.medianRatio} />)}</tbody>
                  </table>
                </div>
              )}
              <p className="cc-note">Avg kW and outside temperature come from live car sessions only; Roamer rows leave them blank. Highlighted rows are outliers.</p>
            </section>
          </>
        )}
        <p className="cc-foot">The raw numbers are at /api/admin/charge-compare?days={days}.</p>
      </main>
    </div>
  );
}
