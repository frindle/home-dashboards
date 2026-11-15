// The forecast charging switch (lib/forecastSwitch.ts), LAN-only like the rest of the app.
//   GET  -> { off, sinceMs, untilMs, active }
//   POST { choice: 'on' | 'off' | 1 | 3 | 7 }   (JSON or the /charging form; a number is days off)
// A change runs the controller at once, so the car gets the new night now when in a write slot.
import { NextRequest } from 'next/server';
import { forecastOff, readForecastSwitch, switchFromChoice, writeForecastSwitch } from '@/lib/forecastSwitch';
import { runRivianChargeControl } from '@/lib/rivianChargeControl';

export const dynamic = 'force-dynamic';

// Back to the page the form was on, only on this host (never an outside referer).
function sameOriginPath(req: NextRequest): string {
  try {
    const r = new URL(req.headers.get('referer') ?? '');
    if (r.host === req.nextUrl.host) return r.pathname + r.search;
  } catch { /* no or bad referer */ }
  return '/charging';
}

export async function GET() {
  const s = await readForecastSwitch();
  return Response.json({ ...s, active: !forecastOff(s, Date.now()) });
}

export async function POST(req: NextRequest) {
  const isForm = (req.headers.get('content-type') ?? '').includes('form');
  let b: Record<string, unknown> | null = null;
  if (isForm) {
    const f = await req.formData().catch(() => null);
    b = f ? Object.fromEntries(f.entries()) : null;
  } else {
    b = (await req.json().catch(() => null)) as Record<string, unknown> | null;
  }
  const s = switchFromChoice(b?.choice, Date.now());
  if (!s) return Response.json({ error: "choice must be 'on', 'off', 1, 3 or 7" }, { status: 400 });
  await writeForecastSwitch(s);
  void runRivianChargeControl({ force: true });
  if (isForm) return new Response(null, { status: 303, headers: { Location: sameOriginPath(req) } });
  return Response.json({ ok: true, ...s });
}
