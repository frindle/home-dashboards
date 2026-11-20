// POST /api/panel/lab: what is waiting on a person and the model queue size, for the panel's Lab page. Bearer token
// from PANEL_LAB_TOKEN; 404 when that is unset. JSON contract in lib/panelLab.ts.
import { handleLab, parseLabConfig } from '@/lib/panelLab';

export const dynamic = 'force-dynamic';

export async function POST(req: Request) {
  return handleLab(req, parseLabConfig(process.env));
}
