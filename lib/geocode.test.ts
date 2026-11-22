// Run: TZ=America/Los_Angeles node --experimental-strip-types --test lib/geocode.test.ts
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { clearGeocodeCache, placeEvent, queryForms, searchPlaces } from './geocode.ts';
import { _setSleepForTests, ONLINE_UA } from './onlineGeo.ts';

const fakeNominatim = (hits: unknown[] = [{ lat: '36.2', lon: '-115.3', display_name: 'A place' }]) => {
  const urls: string[] = [];
  return { urls, impl: async (url: string) => { urls.push(url); return { ok: true, status: 200, json: async () => hits }; } };
};
const env = { NOMINATIM_URL: 'http://nom.invalid:8080/', GEO_ONLINE: '0' };

test('placeEvent order: event geo, then override by UID, then Nominatim, else unplaced', async () => {
  clearGeocodeCache();
  const f = fakeNominatim();
  const base = { uid: 'u1', location: '1 Main St' };
  assert.deepEqual(await placeEvent({ ...base, geo: { lat: 1, lon: 2 } }, { u1: { lat: 9, lon: 9 } }, env, f.impl), { lat: 1, lon: 2, source: 'event' });
  assert.deepEqual(await placeEvent({ ...base, geo: null }, { u1: { lat: 9, lon: 8 } }, env, f.impl), { lat: 9, lon: 8, source: 'override' });
  assert.equal(f.urls.length, 0);
  assert.deepEqual(await placeEvent({ ...base, geo: null }, {}, env, f.impl), { lat: 36.2, lon: -115.3, source: 'nominatim' });
  assert.ok(f.urls[0].startsWith('http://nom.invalid:8080/search?'));
  clearGeocodeCache();
  assert.equal(await placeEvent({ ...base, geo: null }, {}, env, fakeNominatim([]).impl), null);
  assert.equal(await placeEvent({ ...base, geo: null }, {}, { GEO_ONLINE: '0' }, f.impl), null);        // no NOMINATIM_URL, online off
  assert.equal(await placeEvent({ uid: 'x', location: '', geo: null }, {}, env, f.impl), null);
});

test('searchPlaces: several hits, cached, errors give [] and no URL in the log', async () => {
  clearGeocodeCache();
  const f = fakeNominatim([{ lat: '1', lon: '2', display_name: 'One' }, { lat: 'x', lon: '2' }, { lat: '3', lon: '4', display_name: 'Two' }]);
  assert.deepEqual((await searchPlaces('a b', env, f.impl)).map((h) => h.label), ['One', 'Two']);
  await searchPlaces('A B', env, f.impl);
  assert.equal(f.urls.length, 1);
  const logged: string[] = [];
  const orig = console.error;
  console.error = (...a: unknown[]) => { logged.push(a.join(' ')); };
  try {
    assert.deepEqual(await searchPlaces('zzz', env, async (u) => { throw new Error(`down ${u}`); }), []);
  } finally { console.error = orig; }
  assert.ok(logged.length === 1 && !logged[0].includes('nom.invalid') && !logged[0].includes('zzz'));
});

test('online fallback: only when the LAN copy finds nothing; app User-Agent, US only, source online', async () => {
  clearGeocodeCache();
  _setSleepForTests(async () => {});
  try {
    const seen: { url: string; ua?: string }[] = [];
    const impl = async (url: string, init: { headers?: Record<string, string> }) => {
      seen.push({ url, ua: init.headers?.['User-Agent'] });
      const lan = url.startsWith('http://nom.lan');
      const hits = lan && url.includes('Henderson') ? [{ lat: '36.03', lon: '-114.98', display_name: 'Henderson' }]
        : !lan && url.includes('Bishop') ? [{ lat: '37.36', lon: '-118.39', display_name: 'Bishop, CA' }] : [];
      return { ok: true, status: 200, json: async () => hits };
    };
    const e = { NOMINATIM_URL: 'http://nom.lan:8080', NOMINATIM_ONLINE_URL: 'https://nom.online.invalid' };
    assert.deepEqual(await placeEvent({ uid: 'a', location: 'Henderson', geo: null }, {}, e, impl), { lat: 36.03, lon: -114.98, source: 'nominatim' });
    assert.equal(seen.length, 1);
    assert.deepEqual(await placeEvent({ uid: 'b', location: 'Main St, Bishop CA', geo: null }, {}, e, impl), { lat: 37.36, lon: -118.39, source: 'online' });
    assert.equal(seen.length, 3);
    assert.ok(seen[2].url.startsWith('https://nom.online.invalid/search?') && seen[2].url.includes('countrycodes=us'));
    assert.equal(seen[2].ua, ONLINE_UA);
    assert.equal(seen[1].ua, undefined);
    // A miss on both is cached (retried after 12 h, not every run).
    assert.deepEqual(await searchPlaces('Nowhere at all', e, impl), []);
    const n = seen.length;
    assert.deepEqual(await searchPlaces('Nowhere at all', e, impl), []);
    assert.equal(seen.length, n);
    // Both down: not cached, so the next run asks again.
    const orig = console.error;
    console.error = () => {};
    try {
      const down = async () => { throw new Error('down'); };
      assert.deepEqual(await searchPlaces('Down town', e, down), []);
    } finally { console.error = orig; }
    assert.deepEqual(await searchPlaces('Down town', e, impl), []);
    assert.equal(seen.length, n + 2);
  } finally { _setSleepForTests(null); }
});

test('queryForms: an Apple two-line location, then simpler forms, no repeats', () => {
  const apple = 'Some Place\n123 W Example Ave, Las Vegas, NV 89123, United States';
  assert.deepEqual(queryForms(apple), [
    { q: apple },
    { q: '123 W Example Ave, Las Vegas, NV 89123, United States' },
    { q: '123 W Example Ave, Las Vegas, NV 89123' },
    { q: '123 W Example Ave, Las Vegas, NV' },
    { street: '123 W Example Ave', city: 'Las Vegas', state: 'NV', country: 'us' },
    { q: '123 W Example Ave, NV 89123' },
    { q: 'Some Place, Las Vegas, NV' },
    { street: 'W Example Ave', city: 'Las Vegas', state: 'NV', country: 'us' },
  ]);
  // One line, name first, a suite part and a suite tail: the street starts at the house number, units dropped.
  const one = queryForms('Clinic, 9 Main St Ste 4, Suite 200, Henderson, nv 89052');
  assert.deepEqual(one[1], { q: '9 Main St, Henderson, NV 89052' });
  assert.ok(one.some((f) => f.q === 'Clinic, Henderson, NV'));
  assert.ok(one.some((f) => f.street === '9 Main St' && f.city === 'Henderson'));
  // Words that only start like a unit stay.
  assert.ok(queryForms('Unity Hall, 5 Stewart Ave Unit 3, Reno, NV 89501').some((f) => f.street === '5 Stewart Ave' && f.city === 'Reno'));
  assert.ok(queryForms('Unity Hall, 5 Stewart Ave Unit 3, Reno, NV 89501').some((f) => f.q === 'Unity Hall, Reno, NV'));
  // A city like Columbus does not lose its "us"; plain texts stay one form.
  assert.deepEqual(queryForms('Columbus'), [{ q: 'Columbus' }]);
  assert.deepEqual(queryForms('Main St, Bishop CA'), [{ q: 'Main St, Bishop CA' }]);
  assert.deepEqual(queryForms('Henderson, NV'), [{ q: 'Henderson, NV' }]);
  assert.deepEqual(queryForms('  '), []);
});

test('searchPlaces: a text that finds nothing is tried in its simpler forms; the first hit wins and is cached', async () => {
  clearGeocodeCache();
  _setSleepForTests(async () => {});
  try {
    const seen: string[] = [];
    const impl = async (url: string) => {
      seen.push(url);
      const u = new URL(url);
      const lan = u.host === 'nom.lan:8080';
      // The LAN copy only knows the street when asked in structured form; the public one knows the zip form.
      const hits = lan && u.searchParams.get('street') === '123 W Example Ave' ? [{ lat: '36.1', lon: '-115.2', display_name: 'S' }]
        : !lan && u.searchParams.get('q') === '7 Far Rd, Bishop, CA 93514' ? [{ lat: '37.3', lon: '-118.4', display_name: 'F' }] : [];
      return { ok: true, status: 200, json: async () => hits };
    };
    const e = { NOMINATIM_URL: 'http://nom.lan:8080', NOMINATIM_ONLINE_URL: 'https://nom.online.invalid' };
    const apple = 'Some Place\n123 W Example Ave, Las Vegas, NV 89123, United States';
    assert.deepEqual(await placeEvent({ uid: 'a', location: apple, geo: null }, {}, e, impl), { lat: 36.1, lon: -115.2, source: 'nominatim' });
    assert.equal(seen.length, 5);
    assert.ok(seen.every((u) => u.startsWith('http://nom.lan:8080/search?')));
    assert.equal(new URL(seen[4]).searchParams.get('country'), 'us');
    await placeEvent({ uid: 'a', location: apple, geo: null }, {}, e, impl);
    assert.equal(seen.length, 5);
    // Outside the LAN copy: every LAN form misses, then the public one gets at most 4 forms.
    seen.length = 0;
    const far = 'Trailhead\n7 Far Rd, Bishop, CA 93514, United States';
    assert.deepEqual(await placeEvent({ uid: 'b', location: far, geo: null }, {}, e, impl), { lat: 37.3, lon: -118.4, source: 'online' });
    const online = seen.filter((u) => u.startsWith('https://nom.online.invalid/'));
    assert.equal(seen.length - online.length, queryForms(far).length);
    assert.equal(online.length, 3);
    assert.ok(online.every((u) => u.includes('countrycodes=us')));
    seen.length = 0;
    const none = 'Nope\n1 Nowhere Ln, Nada, NV 89000, United States';
    assert.deepEqual(await searchPlaces(none, e, impl, 1), []);
    assert.equal(seen.filter((u) => u.startsWith('https://')).length, 4);
    // Some forms fail and the rest miss: still an answer, so the miss is cached.
    clearGeocodeCache();
    const orig = console.error;
    console.error = () => {};
    let n = 0;
    try {
      const flaky = async (url: string) => { n++; if (n % 2) throw new Error('down'); return impl(url); };
      assert.deepEqual(await searchPlaces(none, { NOMINATIM_URL: 'http://nom.lan:8080', GEO_ONLINE: '0' }, flaky, 1), []);
    } finally { console.error = orig; }
    const calls = n;
    await searchPlaces(none, { NOMINATIM_URL: 'http://nom.lan:8080', GEO_ONLINE: '0' }, impl, 1);
    assert.equal(n, calls);
  } finally { _setSleepForTests(null); }
});
