// POST /api/panel/presence { "locked": boolean }: the desk Mac says whether its screen is locked, so the panel can dim.
// Bearer token from PANEL_PRESENCE_TOKEN; 404 when that is unset. See lib/panelPresence.ts.
import { handlePresence, presenceConfig } from '@/lib/panelPresence';

export const dynamic = 'force-dynamic';

export async function POST(req: Request) {
  return handlePresence(req, presenceConfig(process.env));
}
