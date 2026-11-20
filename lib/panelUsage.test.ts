import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { clearUsage, handleUsage, parseUsageConfig, readUsage, usageSection, validateUsage, MAX_BODY } from './panelUsage.ts';
import { buildPanel, type Section, type Sources } from './panelFeed.ts';

const NOW = 1_800_000_000_000;   // ms
const S = NOW / 1000;
const CFG = parseUsageConfig({ PANEL_USAGE_TOKEN: 'secret-token-value' })!;
const post = (body: unknown, token: string | null = 'secret-token-value', extra: Record<string, string> = {}) =>
  new Request('http://x/api/panel/usage', {
    method: 'POST', body: typeof body === 'string' ? body : JSON.stringify(body),
    headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...extra },
  });
const GOOD = { v: 1, five_hour: { used_pct: 37.5, resets_at: S + 7200 }, weekly: { used_pct: 12, resets_at: S + 300000 } };

beforeEach(() => clearUsage());

test('off without a token: 404, nothing stored', async () => {
  assert.equal(parseUsageConfig({}), null);
  assert.equal(parseUsageConfig({ PANEL_USAGE_TOKEN: '  ' }), null);
  const r = await handleUsage(post(GOOD), null, () => NOW);
  assert.equal(r.status, 404);
  assert.equal(readUsage(), null);
});

test('auth: missing, wrong, wrong-length and non-bearer tokens are 401 and store nothing', async () => {
  for (const t of [null, 'nope', 'secret-token-value-extra', 's']) {
    assert.equal((await handleUsage(post(GOOD, t), CFG, () => NOW)).status, 401, String(t));
  }
  const r = await handleUsage(new Request('http://x', { method: 'POST', body: JSON.stringify(GOOD), headers: { authorization: 'Basic secret-token-value' } }), CFG, () => NOW);
  assert.equal(r.status, 401);
  assert.equal(readUsage(), null);
});

test('a good report is stored with the server receive time', async () => {
  const r = await handleUsage(post(GOOD), CFG, () => NOW);
  assert.equal(r.status, 200);
  const rep = readUsage()!;
  assert.equal(rep.atMs, NOW); assert.equal(rep.h5!.used, 37.5); assert.equal(rep.wk!.resetsAt, S + 300000);
});

test('strict schema', async () => {
  const bad: unknown[] = [
    '{not json', [], { v: 2, weekly: GOOD.weekly }, { v: 1 }, { ...GOOD, extra: 1 },
    { v: 1, five_hour: { used_pct: -1 } }, { v: 1, five_hour: { used_pct: '5' } }, { v: 1, five_hour: { used_pct: 5000 } },
    { v: 1, five_hour: { used_pct: 5, resets_at: 'tomorrow' } }, { v: 1, five_hour: { used_pct: 5, resets_at: 12 } },
    { v: 1, five_hour: { used_pct: 5, other: 1 } }, { v: 1, weekly: null },
    { v: 1, models: [{ name: 'Opus' }] }, { v: 1, models: [{ name: '<b>', used_pct: 1 }] },
    { v: 1, models: Array.from({ length: 5 }, () => ({ name: 'A', used_pct: 1 })) },
  ];
  for (const b of bad) assert.equal((await handleUsage(post(b), CFG, () => NOW)).status, 400, JSON.stringify(b));
  assert.equal(readUsage(), null);
  assert.equal((await handleUsage(post({ v: 1, five_hour: { used_pct: 5, resets_at: new Date((S + 60) * 1000).toISOString() } }), CFG, () => NOW)).status, 200);
  assert.equal((await handleUsage(post({ v: 1, models: [{ name: 'Opus 4', used_pct: 1 }] }), CFG, () => NOW)).status, 200);
  assert.equal(typeof validateUsage({ v: 1, weekly: { used_pct: 1 } }, 1), 'object');
});

test('size cap: by header and by actual body', async () => {
  const big = JSON.stringify({ ...GOOD, models: [{ name: 'x'.repeat(MAX_BODY), used_pct: 1 }] });
  assert.equal((await handleUsage(post(big), CFG, () => NOW)).status, 413);
  assert.equal((await handleUsage(post(GOOD, 'secret-token-value', { 'content-length': '999999' }), CFG, () => NOW)).status, 413);
  assert.equal(readUsage(), null);
});

test('section: percentages, resets, pace and time to the limit', () => {
  const rep = validateUsage({ v: 1, five_hour: { used_pct: 50, resets_at: S + 9000 }, weekly: { used_pct: 12.4, resets_at: S + 300000 }, models: [{ name: 'Opus', used_pct: 30, resets_at: S + 300000 }] }, NOW);
  assert.equal(typeof rep, 'object');
  const sec = usageSection(rep as never, 1200_000, NOW) as Section & Record<string, any>;
  assert.equal(sec.ok, 1); assert.equal(sec.st, 0); assert.equal(sec.age, 0);
  assert.deepEqual(sec.h5, { p: 50, r: 9000 }); assert.deepEqual(sec.wk, { p: 12, r: 300000 });
  assert.deepEqual(sec.m, [{ n: 'Opus', p: 30, r: 300000 }]);
  assert.equal(sec.bp, 20);                    // 50% in 2.5 h
  assert.equal(sec.lm, undefined);          // would hit 100% exactly at the reset: not a prediction
});

test('section: the limit is only predicted when it comes before the reset', () => {
  const rep = validateUsage({ v: 1, five_hour: { used_pct: 90, resets_at: S + 9000 } }, NOW) as never;
  const sec = usageSection(rep, 1200_000, NOW) as Section & Record<string, any>;
  assert.equal(sec.bp, 36); assert.equal(sec.lm, 1000);
  const low = usageSection(validateUsage({ v: 1, five_hour: { used_pct: 5, resets_at: S + 9000 } }, NOW) as never, 1200_000, NOW) as Record<string, any>;
  assert.equal(low.lm, undefined);
  const early = usageSection(validateUsage({ v: 1, five_hour: { used_pct: 5, resets_at: S + 17800 } }, NOW) as never, 1200_000, NOW) as Record<string, any>;
  assert.equal(early.bp, undefined);           // under 10 minutes into the window: no pace yet
  const over = usageSection(validateUsage({ v: 1, five_hour: { used_pct: 140, resets_at: S + 9000 } }, NOW) as never, 1200_000, NOW) as Record<string, any>;
  assert.equal(over.h5.p, 100); assert.equal(over.lm, 0);
});

test('stale window: old numbers are flagged, a rolled-over window is dropped, nothing left is "no data"', () => {
  const rep = validateUsage({ v: 1, five_hour: { used_pct: 50, resets_at: S + 600 }, weekly: { used_pct: 10, resets_at: S + 900000 } }, NOW) as never;
  assert.equal((usageSection(rep, 1200_000, NOW + 1200_000) as Section).st, 0);
  assert.equal((usageSection(rep, 1200_000, NOW + 1200_001) as Section).st, 1);
  const later = usageSection(rep, 1200_000, NOW + 700_000) as Record<string, any>;   // the 5-hour window reset meanwhile
  assert.equal(later.h5, undefined); assert.equal(later.wk.p, 10);
  assert.deepEqual(usageSection(rep, 1200_000, NOW + 1000_000_000), { ok: 0, st: 0 });
  assert.deepEqual(usageSection(null, 1200_000, NOW), { ok: 0, st: 0 });
  assert.equal(parseUsageConfig({ PANEL_USAGE_TOKEN: 't', PANEL_USAGE_STALE_S: '600' })!.staleMs, 600_000);
  assert.equal(parseUsageConfig({ PANEL_USAGE_TOKEN: 't', PANEL_USAGE_STALE_S: '5' })!.staleMs, 1200_000);
});

test('feed with usage on: "us" is in the body, a near-limit report raises a red alert and the ring, a stale one does not', async () => {
  const base: Sources = {
    bl: async () => ({ ok: 1, st: 0, m: [], al: 0 }), nw: async () => ({ ok: 1, st: 0, src: 'wan', l: [{ n: 'Cox', d: 1, u: 1 }] }),
    sd: async () => ({ ok: 1, st: 0, arr: 'STARTED', d: [] }), ev: async () => ({ ok: 1, st: 0 }),
  };
  const rep = validateUsage({ v: 1, five_hour: { used_pct: 97, resets_at: S + 3000 } }, NOW) as never;
  const withUs = (at: number): Sources => ({ ...base, us: async () => usageSection(rep, 1200_000, at) });
  const fresh = await buildPanel(withUs(NOW + 1000), { nowMs: NOW + 1000 });
  assert.equal(fresh.lvl, 'r'); assert.equal(fresh.al![0].k, 'usage:5h'); assert.equal(fresh.al![0].tp, 'us');
  assert.ok(fresh.us && (fresh.us as Record<string, unknown>).h5);
  const stale = await buildPanel(withUs(NOW + 1300_000), { nowMs: NOW + 1300_000 });
  assert.equal(stale.lvl, 'g'); assert.ok(!stale.al); assert.equal(stale.us!.st, 1);
  const none = await buildPanel({ ...base, us: async () => usageSection(null, 1, NOW) }, { nowMs: NOW });
  assert.deepEqual(none.us, { ok: 0, st: 0 });
});
