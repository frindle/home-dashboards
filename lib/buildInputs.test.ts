import test from 'node:test';
import assert from 'node:assert/strict';
import { isImageInput, onlyNonImageFiles } from './buildInputs.ts';

test('code, config and dependency files are image inputs', () => {
  for (const p of ['lib/apiLog.ts', 'app/page.tsx', 'components/a.tsx', 'package.json', 'package-lock.json',
    'Dockerfile', 'docker-compose.yml', 'next.config.ts', 'icon.png', 'instrumentation.ts']) {
    assert.equal(isImageInput(p), true, p);
  }
});

test('docs, tests, firmware and CI are not', () => {
  for (const p of ['CHARGE-LOG-CAPTURE.md', 'docs/valhalla-nominatim.md', 'lib/caldavWriteback.test.ts',
    'firmware/crowpanel-21/panel_helpers.h', '.github/workflows/ci.yml', 'update.sh', '.env.example', 'tests/update_sh_test.sh']) {
    assert.equal(isImageInput(p), false, p);
  }
});

test('files in runtime directories ship even when they look like docs or tests', () => {
  for (const p of ['server/start.js', 'protos/vehicle_data.proto', 'scripts/kiosk-setup.sh', 'scripts/README.md']) {
    assert.equal(isImageInput(p), true, p);
  }
});

test('a compare listing counts as "no update" only when every file is outside the image', () => {
  assert.equal(onlyNonImageFiles([{ filename: 'docs/a.md' }, { filename: 'lib/a.test.ts' }]), true);
  assert.equal(onlyNonImageFiles([{ filename: 'docs/a.md' }, { filename: 'lib/a.ts' }]), false);
});

test('an empty, missing or full (300) listing is not trusted', () => {
  assert.equal(onlyNonImageFiles(undefined), false);
  assert.equal(onlyNonImageFiles([]), false);
  assert.equal(onlyNonImageFiles(Array.from({ length: 300 }, () => ({ filename: 'README.md' }))), false);
  assert.equal(onlyNonImageFiles([{}]), false);
});
