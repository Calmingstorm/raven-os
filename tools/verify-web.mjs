#!/usr/bin/env node
// Check a web build against its build.json manifest.
//   node tools/verify-web.mjs dist <dir> [required-file ...]
//     every file listed in build.json matches its sha256, each .gz copy decompresses to the same bytes,
//     the required files are listed, and nothing but build.json and .gz copies is unlisted.
//   node tools/verify-web.mjs site <url> <dir>
//     the site serves exactly <dir>/build.json, and every listed file it serves matches its sha256.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const [mode, target, ...rest] = process.argv.slice(2);
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function walk(dir, base = dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap(entry => {
    const full = path.join(dir, entry.name);
    return entry.isDirectory() ? walk(full, base) : [path.relative(base, full)];
  });
}

if (mode === 'dist') {
  const build = JSON.parse(fs.readFileSync(path.join(target, 'build.json'), 'utf8'));
  for (const name of rest) {
    if (name !== 'build.json') assert.ok(build.files[name], `build.json does not list ${name}`);
  }
  for (const [name, hash] of Object.entries(build.files)) {
    const bytes = fs.readFileSync(path.join(target, name));
    assert.equal(sha256(bytes), hash, `${name} does not match build.json`);
    const gz = path.join(target, `${name}.gz`);
    if (fs.existsSync(gz)) assert.equal(sha256(zlib.gunzipSync(fs.readFileSync(gz))), hash, `${name}.gz does not match build.json`);
  }
  const unlisted = walk(target).filter(name => name !== 'build.json' && name !== 'build.json.gz'
    && !build.files[name] && !(name.endsWith('.gz') && build.files[name.slice(0, -3)]));
  assert.deepEqual(unlisted, [], 'files in the build that build.json does not list');
  console.log(`verify-web: ${target} matches build.json (${Object.keys(build.files).length} files, release ${build.release})`);
} else if (mode === 'site') {
  const local = fs.readFileSync(path.join(rest[0], 'build.json'), 'utf8');
  const get = async name => {
    const response = await fetch(new URL(name, target), { headers: { 'accept-encoding': 'identity' }, cache: 'no-store' });
    assert.equal(response.status, 200, `${name}: HTTP ${response.status}`);
    return Buffer.from(await response.arrayBuffer());
  };
  assert.equal((await get('build.json')).toString('utf8'), local, 'the site serves a different build.json');
  const build = JSON.parse(local);
  for (const [name, hash] of Object.entries(build.files)) {
    assert.equal(sha256(await get(name)), hash, `the site's ${name} does not match build.json`);
  }
  console.log(`verify-web: ${target} serves release ${build.release}, all ${Object.keys(build.files).length} files verified`);
} else {
  console.error('usage: verify-web.mjs dist <dir> [required ...] | site <url> <dir>');
  process.exit(2);
}
