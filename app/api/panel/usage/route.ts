// POST /api/panel/usage: the Claude usage numbers for the panel's Claude page. Bearer token from PANEL_USAGE_TOKEN;
// 404 when that is unset. JSON contract in lib/panelUsage.ts.
import { handleUsage, parseUsageConfig } from '@/lib/panelUsage';

export const dynamic = 'force-dynamic';

export async function POST(req: Request) {
  return handleUsage(req, parseUsageConfig(process.env));
}
