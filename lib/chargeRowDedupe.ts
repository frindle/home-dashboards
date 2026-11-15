// A wall-connector session is one row per side and start time. Two overlapping
// dashboard polls once closed the same session twice (2026-11-14, before the
// poll was serialized), so readers drop a repeat of an earlier side + start.
export function dedupeChargeRows<T extends { side: string; startedAt: string }>(rows: T[]): T[] {
  const seen = new Set<string>();
  return rows.filter((r) => {
    const key = `${r.side}|${r.startedAt}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}
