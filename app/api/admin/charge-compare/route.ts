import { NextRequest, NextResponse } from 'next/server';
import { readChargeHistory } from '@/lib/chargeHistory';
import { readConfig } from '@/lib/config';
import { vehicleIdResolver } from '@/lib/chargeSessionInflux';
import { readCarChargeHistory } from '@/lib/rivianChargeRunner';
import { compareSources } from '@/lib/chargeSourceCompare';

export const dynamic = 'force-dynamic';

// Read-only: car-reported Rivian sessions vs Wall Connector rows for the
// Rivian, paired by time overlap. ?days=90 (default, max 730).
export async function GET(req: NextRequest) {
  const raw = Number(new URL(req.url).searchParams.get('days') ?? '90');
  const days = Math.max(1, Math.min(730, Number.isFinite(raw) ? Math.floor(raw) : 90));
  const cutoff = Date.now() - days * 86_400_000;
  const idFor = vehicleIdResolver(readConfig());
  const inRange = (iso: string) => Date.parse(iso) >= cutoff;

  const wc = (await readChargeHistory()).filter((r) => idFor(r) === 'rivian' && inRange(r.endedAt));
  const car = (await readCarChargeHistory()).filter((r) => inRange(r.endedAt));
  return NextResponse.json({ days, ...compareSources(car, wc) });
}
