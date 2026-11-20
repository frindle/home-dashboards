import { test } from 'node:test';
import assert from 'node:assert/strict';
import { awayFromStates, effectiveBrightness, makePanelCache, parseDimConfig, parsePresenceConfig, withBr, type PanelBody, type Sources } from './panelFeed.ts';
import { clearPresence, fetchHaPresence, handlePresence, makePresenceReader, readPresence, recordPresence, tokenMatches } from './panelPresence.ts';

const NOW = Date.parse('2026-11-12T20:00:00Z');   // 12:00 in Los Angeles, day
const NIGHT = Date.parse('2026-11-13T09:00:00Z'); // 01:00 in Los Angeles
const ok = (x: object = {}) => ({ ok: 1 as const, st: 0 as const, ...x });
const src: Sources = { bl: async () => ok({ m: [], al: 0 }), nw: async () => ok(), sd: async () => ok(), ev: async () => ok() };
const cfg = parsePresenceConfig({ PANEL_PRESENCE_TOKEN: 'tok' })!;
const dim = parseDimConfig({ PANEL_NIGHT_HOURS: '22-7', PANEL_TZ: 'America/Los_Angeles' })!;

test('presence config: off without a token; defaults, clamping, stale floor', () => {
  assert.equal(parsePresenceConfig({}), null);
  assert.equal(parsePresenceConfig({ PANEL_PRESENCE_TOKEN: '  ' }), null);
  assert.deepEqual(cfg, { token: 'tok', ha: undefined, awayBr: 1, staleMs: 180_000 });
  const c = parsePresenceConfig({ PANEL_PRESENCE_TOKEN: 't', PANEL_AWAY_BRIGHTNESS: '0', PANEL_PRESENCE_STALE_S: '5' })!;
  assert.deepEqual([c.awayBr, c.staleMs], [1, 180_000]);   // 0 -> 1 (firmware treats 0 as 1); stale under 10 s ignored
  const d = parsePresenceConfig({ PANEL_PRESENCE_TOKEN: 't', PANEL_AWAY_BRIGHTNESS: '4', PANEL_PRESENCE_STALE_S: '600' })!;
  assert.deepEqual([d.awayBr, d.staleMs], [4, 600_000]);
});

test('brightness: locked wins while fresh, unlocked/stale/none/off keep the base', () => {
  const fresh = { away: true, atMs: NOW - 60_000 };
  assert.equal(effectiveBrightness(60, cfg, fresh, NOW), 1);
  assert.equal(effectiveBrightness(undefined, cfg, fresh, NOW), 1);          // no dimming configured, still applies
  assert.equal(effectiveBrightness(5, cfg, fresh, NOW), 1);                  // night + locked -> locked level
  assert.equal(effectiveBrightness(60, cfg, { away: false, atMs: NOW }, NOW), 60);
  assert.equal(effectiveBrightness(5, cfg, { away: false, atMs: NOW }, NOW), 5);   // night + unlocked -> night
  assert.equal(effectiveBrightness(60, cfg, { away: true, atMs: NOW - 181_000 }, NOW), 60);   // stale
  assert.equal(effectiveBrightness(60, cfg, { away: true, atMs: NOW - 180_000 }, NOW), 1);    // edge still fresh
  assert.equal(effectiveBrightness(60, cfg, { away: true, atMs: NOW + 3_600_000 }, NOW), 60); // clock far in the future
  assert.equal(effectiveBrightness(60, cfg, null, NOW), 60);
  assert.equal(effectiveBrightness(60, null, fresh, NOW), 60);
  assert.equal(effectiveBrightness(undefined, null, fresh, NOW), undefined);
});

test('withBr keeps key order and removes br when undefined', () => {
  const b = { v: 1, t: 1, lvl: 'g', why: [], bl: {}, nw: {}, sd: {}, ev: {} } as unknown as PanelBody;
  assert.deepEqual(Object.keys(withBr(b, 3)), ['v', 't', 'lvl', 'why', 'br', 'bl', 'nw', 'sd', 'ev']);
  assert.equal(withBr(b, undefined), b);
  assert.ok(!('br' in withBr(withBr(b, 3), undefined)));
});

test('feed: bytes identical to today with presence off, even if a report exists', async () => {
  recordPresence(true, NOW);
  const plain = makePanelCache(src, 15_000, () => NOW, dim);
  const off = makePanelCache(src, 15_000, () => NOW, dim, { cfg: null, read: readPresence });
  const none = makePanelCache(src, 15_000, () => NOW);
  const offNoDim = makePanelCache(src, 15_000, () => NOW, null, { cfg: null, read: readPresence });
  assert.equal(await off(), await plain());
  assert.equal(await offNoDim(), await none());
  assert.ok(!('br' in JSON.parse(await none())));
  clearPresence();
});

test('feed: lock shows on the next request, inside the 15 s cache, and clears on unlock', async () => {
  clearPresence();
  let clock = NOW;
  const get = makePanelCache(src, 15_000, () => clock, dim, { cfg, read: readPresence });
  assert.equal(JSON.parse(await get()).br, 60);
  clock += 2_000;
  recordPresence(true, clock);
  assert.equal(JSON.parse(await get()).br, 1);
  clock += 2_000;
  recordPresence(false, clock);
  assert.equal(JSON.parse(await get()).br, 60);
  clock += 2_000;
  recordPresence(true, clock);
  clock += 200_000;                                    // Mac went quiet: report stale, panel recovers
  assert.equal(JSON.parse(await get()).br, 60);
  clearPresence();
});

test('feed: night + locked uses the locked level, night + unlocked the night level', async () => {
  clearPresence();
  const get = makePanelCache(src, 15_000, () => NIGHT, dim, { cfg, read: readPresence });
  assert.equal(JSON.parse(await get()).br, 5);
  recordPresence(true, NIGHT);
  assert.equal(JSON.parse(await get()).br, 1);
  recordPresence(false, NIGHT);
  assert.equal(JSON.parse(await get()).br, 5);
  clearPresence();
});

test('feed: locked adds br even when day/night dimming is not configured', async () => {
  clearPresence();
  const get = makePanelCache(src, 15_000, () => NOW, null, { cfg, read: readPresence });
  assert.ok(!('br' in JSON.parse(await get())));
  recordPresence(true, NOW);
  assert.equal(JSON.parse(await get()).br, 1);
  clearPresence();
});

const post = (body: string, auth?: string, extra: Record<string, string> = {}) =>
  new Request('http://x/api/panel/presence', { method: 'POST', body, headers: { ...(auth ? { authorization: auth } : {}), ...extra } });

test('endpoint: 404 when off, 401 on missing/wrong token, nothing recorded', async () => {
  clearPresence();
  assert.equal((await handlePresence(post('{"locked":true}', 'Bearer tok'), null)).status, 404);
  assert.equal((await handlePresence(post('{"locked":true}'), cfg)).status, 401);
  assert.equal((await handlePresence(post('{"locked":true}', 'Bearer nope'), cfg)).status, 401);
  assert.equal((await handlePresence(post('{"locked":true}', 'Bearer tokk'), cfg)).status, 401);
  assert.equal((await handlePresence(post('{"locked":true}', 'tok'), cfg)).status, 401);
  assert.equal(readPresence(), null);
});

test('endpoint: records a valid post with the server time; rejects junk bodies', async () => {
  clearPresence();
  const r = await handlePresence(post('{"locked":true}', 'Bearer tok'), cfg, () => NOW);
  assert.equal(r.status, 200);
  assert.deepEqual(readPresence(), { away: true, atMs: NOW });
  assert.equal((await handlePresence(post('{"locked":false}', 'Bearer tok'), cfg, () => NOW + 1)).status, 200);
  assert.deepEqual(readPresence(), { away: false, atMs: NOW + 1 });
  for (const bad of ['', 'nope', '{}', '{"locked":"yes"}', '{"locked":1}', 'null']) {
    assert.equal((await handlePresence(post(bad, 'Bearer tok'), cfg)).status, 400, bad);
  }
  assert.equal((await handlePresence(post('x'.repeat(300), 'Bearer tok'), cfg)).status, 413);
  assert.deepEqual(readPresence(), { away: false, atMs: NOW + 1 });   // junk changed nothing
  clearPresence();
});

test('token compare handles different lengths and empty values', () => {
  assert.ok(tokenMatches('abc', 'abc'));
  assert.ok(!tokenMatches('abc', 'abcd'));
  assert.ok(!tokenMatches('', 'abc'));
});

test('any/all: present if ANY usable entity is present; away only when ALL usable are away', () => {
  const p = ['on', 'home'];
  assert.equal(awayFromStates(['off'], p), true);
  assert.equal(awayFromStates(['on'], p), false);
  assert.equal(awayFromStates(['off', 'on'], p), false);          // Mac asleep but ecobee room occupied
  assert.equal(awayFromStates(['off', 'not_home'], p), true);
  assert.equal(awayFromStates(['off', 'unavailable'], p), true);   // unusable ignored
  assert.equal(awayFromStates(['unavailable', 'on'], p), false);
  assert.equal(awayFromStates(['unavailable', 'unknown', null, ''], p), null);   // nothing usable -> fall back
  assert.equal(awayFromStates([], p), null);
  assert.equal(awayFromStates([' ON ', 'off'], p), false);        // case/space tolerant
  assert.equal(awayFromStates(['locked'], ['unlocked']), true);   // custom present states
});

test('HA config: needs entities plus HA_URL and HA_TOKEN; list, defaults, overrides', () => {
  const env = { HA_URL: 'http://ha.test:8123/', HA_TOKEN: 't', PANEL_PRESENCE_ENTITY: ' binary_sensor.a , binary_sensor.b ' };
  const c = parsePresenceConfig(env)!;
  assert.deepEqual(c.ha, { url: 'http://ha.test:8123', haToken: 't', entities: ['binary_sensor.a', 'binary_sensor.b'], presentStates: ['on', 'home'] });
  assert.equal(c.token, undefined);
  assert.deepEqual(parsePresenceConfig({ ...env, PANEL_PRESENCE_PRESENT_STATES: 'Unlocked, home' })!.ha!.presentStates, ['unlocked', 'home']);
  assert.equal(parsePresenceConfig({ ...env, HA_TOKEN: '' }), null);
  assert.equal(parsePresenceConfig({ ...env, HA_URL: undefined }), null);
  assert.equal(parsePresenceConfig({ HA_URL: 'u', HA_TOKEN: 't' }), null);
});

const haCfg = parsePresenceConfig({ HA_URL: 'http://ha.test', HA_TOKEN: 'secret', PANEL_PRESENCE_ENTITY: 'binary_sensor.mac,binary_sensor.room' })!;
const fakeHa = (states: Record<string, string | number>, calls: string[] = []) => (async (url: string | URL | Request, init?: RequestInit) => {
  const u = String(url);
  calls.push(u);
  assert.equal((init?.headers as Record<string, string>).Authorization, 'Bearer secret');
  const id = decodeURIComponent(u.split('/api/states/')[1]);
  const v = states[id];
  if (v === 500) return new Response('x', { status: 500 });
  if (v === undefined) throw new Error('down');
  return new Response(JSON.stringify({ entity_id: id, state: v }), { status: 200 });
}) as unknown as typeof fetch;

test('HA read: mocked responses, failures ignored, nothing usable returns null', async () => {
  const ha = haCfg.ha!;
  assert.deepEqual(await fetchHaPresence(ha, NOW, fakeHa({ 'binary_sensor.mac': 'off', 'binary_sensor.room': 'off' })), { away: true, atMs: NOW });
  assert.deepEqual(await fetchHaPresence(ha, NOW, fakeHa({ 'binary_sensor.mac': 'off', 'binary_sensor.room': 'on' })), { away: false, atMs: NOW });
  assert.deepEqual(await fetchHaPresence(ha, NOW, fakeHa({ 'binary_sensor.mac': 'off' })), { away: true, atMs: NOW });   // room read failed
  assert.deepEqual(await fetchHaPresence(ha, NOW, fakeHa({ 'binary_sensor.mac': 'unavailable', 'binary_sensor.room': 500 })), null);
  assert.equal(await fetchHaPresence(ha, NOW, fakeHa({})), null);
});

test('HA reader: cached 2 s, retried after 10 s on failure, falls back to the POSTed report', async () => {
  clearPresence();
  let clock = NOW;
  const calls: string[] = [];
  const states: Record<string, string | number> = { 'binary_sensor.mac': 'off', 'binary_sensor.room': 'off' };
  const read = makePresenceReader(haCfg, () => clock, fakeHa(states, calls));
  assert.equal((await read())?.away, true);
  assert.equal(calls.length, 2);
  await read(); await read();
  assert.equal(calls.length, 2);                      // cached
  clock += 2_500; states['binary_sensor.room'] = 'on';
  assert.equal((await read())?.away, false);          // picked up after the short cache
  assert.equal(calls.length, 4);
  // Home Assistant goes away: nothing usable -> POSTed report (none yet -> null)
  delete states['binary_sensor.mac']; delete states['binary_sensor.room'];
  clock += 2_500;
  assert.equal(await read(), null);
  const n = calls.length;
  clock += 3_000;
  await read();
  assert.equal(calls.length, n);                      // failure cached 10 s
  recordPresence(true, clock);
  assert.deepEqual(await read(), { away: true, atMs: clock });
  clearPresence();
});

test('feed: HA presence drives br end to end (night + away, any-present keeps night level)', async () => {
  clearPresence();
  const states: Record<string, string | number> = { 'binary_sensor.mac': 'off', 'binary_sensor.room': 'off' };
  const get = makePanelCache(src, 15_000, () => NIGHT, dim, { cfg: haCfg, read: makePresenceReader(haCfg, () => NIGHT, fakeHa(states)) });
  assert.equal(JSON.parse(await get()).br, 1);
  states['binary_sensor.room'] = 'on';
  const get2 = makePanelCache(src, 15_000, () => NIGHT, dim, { cfg: haCfg, read: makePresenceReader(haCfg, () => NIGHT, fakeHa(states)) });
  assert.equal(JSON.parse(await get2()).br, 5);
});
