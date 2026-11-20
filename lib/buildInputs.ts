// Which repo files can change the running image. update.sh skips the Docker
// build when a deploy only brought files that never reach the image (docs,
// tests, firmware, CI config). The "update available" check uses this list too,
// so a skipped docs-only commit does not make it claim an update.
//
// Keep in step with is_build_input in update.sh.
export function isImageInput(path: string): boolean {
  if (['Dockerfile', 'docker-compose.yml', '.dockerignore', 'package.json', 'package-lock.json'].includes(path)) return true;
  // Runtime directories: even a *.md or a test file in them ships.
  if (/^(server|protos|scripts)\//.test(path)) return true;
  if (/\.md$/.test(path)) return false;
  if (/^(docs|firmware|\.github|tests)\//.test(path)) return false;
  if (['update.sh', '.env.example', '.gitignore', '.gitattributes'].includes(path)) return false;
  if (/\.test\.(ts|tsx|js|mjs)$/.test(path)) return false;
  return true;
}

/** True when every file in a GitHub compare listing is outside the image. */
export function onlyNonImageFiles(files: { filename?: string }[] | undefined): boolean {
  // GitHub lists at most 300 files per compare; a missing or full list is "unknown": say outdated.
  if (!Array.isArray(files) || files.length === 0 || files.length >= 300) return false;
  return files.every(f => typeof f.filename === 'string' && !isImageInput(f.filename));
}
