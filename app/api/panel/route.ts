// GET /api/panel: the compact status JSON for the ESP32 round display (firmware/crowpanel-21). Key legend and the
// ok/st rules are documented in lib/panelFeed.ts. Cached 15 s. Read-only.
//
// No auth, on purpose: it matches the 12.3"/14" display routes (/api/dashboard, /api/energy), which are also open
// because the dashboard has no auth layer and is LAN-only. The body carries no tokens, addresses or vehicle location.
// If auth is added to those routes later, add it here too.
import { parseAlertConfig } from '@/lib/panelAlerts';
import { CACHE_MS, makePanelCache, parseDimConfig, parsePresenceConfig } from '@/lib/panelFeed';
import { makePresenceReader } from '@/lib/panelPresence';
import { panelSources } from '@/lib/panelSources';

export const dynamic = 'force-dynamic';

// PANEL_NIGHT_HOURS (optional) turns on night dimming: the body then carries "br", the backlight percent for now.
// Presence (optional; Home Assistant entities or POST /api/panel/presence): "br" follows whether anyone is at the desk,
// read on every request, not only when the 15 s cache rebuilds.
const presenceCfg = parsePresenceConfig(process.env);
const get = makePanelCache(panelSources, CACHE_MS, Date.now, parseDimConfig(process.env),
  { cfg: presenceCfg, read: makePresenceReader(presenceCfg) }, parseAlertConfig(process.env));

export async function GET() {
  const body = await get();
  return new Response(body, {
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' },
  });
}
