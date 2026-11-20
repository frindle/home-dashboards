import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { clearLab, handleLab, labSection, parseLabConfig, readLab, validateLab, MAX_BODY } from './panelLab.ts';
import { buildPanel, type Section, type Sources } from './panelFeed.ts';

const NOW = 1_800_000_000_000;
const S = NOW / 1000;
const CFG = parseLabConfig({ PANEL_LAB_TOKEN: 'lab-token-value' })!;
const post = (body: unknown, token: string | null = 'lab-token-value') =>
  new Request('http://x/api/panel/lab', {
    method: 'POST', body: typeof body === 'string' ? body : JSON.stringify(body),
    headers: token ? { authorization: `Bearer ${token}` } : {},
  });
const GOOD = { v: 1, items: [{ text: 'Approve model swap', since: S - 7200 }, { text: 'Review run', age_s: 90000, warn: true }], queue: { total: 7, running: 2 } };

beforeEach(() => clearLab());

test('off without a token: 404, nothing stored', async () => {
  assert.equal(parseLabConfig({}), null);
  assert.equal((await handleLab(post(GOOD), null, () => NOW)).status, 404);
  assert.equal(readLab(), null);
});

test('auth: missing and wrong tokens are 401', async () => {
  for (const t of [null, 'nope', 'lab-token-value-extra']) assert.equal((await handleLab(post(GOOD, t), CFG, () => NOW)).status, 401);
  assert.equal(readLab(), null);
});

test('good report is stored with the server receive time', async () => {
  assert.equal((await handleLab(post(GOOD), CFG, () => NOW)).status, 200);
  const r = readLab()!;
  assert.equal(r.atMs, NOW);
  assert.equal(r.items.length, 2);
  assert.equal(r.items[1].warn, true);
  assert.deepEqual(r.queue, { total: 7, running: 2 });
});

test('strict schema', () => {
  const bad = [
    {}, { v: 2, items: [] }, { v: 1 }, { v: 1, items: [], extra: 1 },
    { v: 1, items: new Array(7).fill({ text: 'x' }) },
    { v: 1, items: [{ text: '' }] }, { v: 1, items: [{ text: 'x'.repeat(29) }] }, { v: 1, items: [{ text: '<b>' }] },
    { v: 1, items: [{ text: 'a', since: S, age_s: 1 }] }, { v: 1, items: [{ text: 'a', warn: 'yes' }] },
    { v: 1, items: [{ text: 'a', since: 5 }] }, { v: 1, items: [{ text: 'a', age_s: -1 }] }, { v: 1, items: [{ text: 'a', x: 1 }] },
    { v: 1, items: [], queue: { total: 1, running: 2 } }, { v: 1, items: [], queue: { total: 1.5, running: 0 } },
    { v: 1, items: [], queue: { total: 1 } }, { v: 1, items: [], queue: 'x' },
  ];
  for (const b of bad) assert.equal(typeof validateLab(b, NOW), 'string', JSON.stringify(b));
  assert.equal(typeof validateLab({ v: 1, items: [] }, NOW), 'object');
});

test('size cap: 413', async () => {
  const big = JSON.stringify({ v: 1, items: [], pad: 'x'.repeat(MAX_BODY) });
  assert.equal((await handleLab(post(big), CFG, () => NOW)).status, 413);
  assert.equal((await handleLab(post('{nope'), CFG, () => NOW)).status, 400);
});

test('section: ages in seconds, stale flag, short keys', () => {
  const rep = validateLab(GOOD, NOW) as ReturnType<typeof validateLab> & object;
  const sec = labSection(rep as never, 1200_000, NOW + 60_000) as Record<string, unknown>;
  assert.equal(sec.ok, 1); assert.equal(sec.st, 0); assert.equal(sec.age, 60);
  assert.deepEqual(sec.i, [{ t: 'Approve model swap', a: 7260 }, { t: 'Review run', a: 90060, w: 1 }]);
  assert.equal(sec.q, 7); assert.equal(sec.r, 2);
  assert.equal(labSection(rep as never, 1200_000, NOW + 1300_000).st, 1);
  assert.deepEqual(labSection(null, 1, NOW), { ok: 0, st: 0 });
});

test('feed: no lb key without the source, lb present with it, size stays small', async () => {
  const ok: Section = { ok: 1, st: 0 };
  const base: Sources = { bl: async () => ok, nw: async () => ok, sd: async () => ok, ev: async () => ok };
  assert.equal('lb' in (await buildPanel(base, { nowMs: NOW })), false);
  const rep = validateLab({ v: 1, items: new Array(6).fill({ text: 'Approve model swap now', age_s: 99999 }), queue: { total: 9, running: 3 } }, NOW) as never;
  const body = await buildPanel({ ...base, lb: async () => labSection(rep, 1e6, NOW) }, { nowMs: NOW });
  assert.equal((body.lb as Section).ok, 1);
  assert.ok(JSON.stringify(body).length < 1500);
});
