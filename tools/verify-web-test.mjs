#!/usr/bin/env node
// Regression tests for tools/verify-web.mjs: damaged gzip copies must fail both the build check and the site
// check. Works on a scratch copy of web/dist (run tools/build-web.sh first) served by a small gzip_static-style
// server: a request that accepts gzip gets the .gz copy when there is one, as nginx does.
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const verifier = path.join(root, 'tools/verify-web.mjs');
const dist = path.join(root, 'web/dist');
assert.ok(fs.existsSync(path.join(dist, 'build.json')), 'no web/dist/build.json: run tools/build-web.sh first');

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'verify-web-test-'));
const copy = path.join(scratch, 'dist');
fs.cpSync(dist, copy, { recursive: true });

const server = http.createServer((req, res) => {
  let name = decodeURIComponent(new URL(req.url, 'http://local').pathname);
  if (name.endsWith('/')) name += 'index.html';
  const file = path.join(copy, path.normalize(name));
  if (!file.startsWith(copy) || !fs.existsSync(file)) { res.writeHead(404).end(); return; }
  const gzip = /\bgzip\b/.test(req.headers['accept-encoding'] || '') && fs.existsSync(`${file}.gz`);
  res.writeHead(200, gzip ? { 'content-encoding': 'gzip' } : {});
  res.end(fs.readFileSync(gzip ? `${file}.gz` : file));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const site = `http://127.0.0.1:${server.address().port}/`;

// Asynchronous on purpose: the site under test is served by this same process.
const passes = (...args) => new Promise(resolve => {
  execFile('node', [verifier, ...args], { timeout: 60000 }, error => resolve(!error));
});
const results = [];
try {
  assert.ok(await passes('dist', copy), 'the pristine copy passes the build check');
  assert.ok(await passes('site', site, copy), 'the pristine copy passes the site check');

  // A stale build.json.gz carrying a different manifest.
  const manifest = path.join(copy, 'build.json.gz');
  const goodManifest = fs.readFileSync(manifest);
  const other = JSON.parse(fs.readFileSync(path.join(copy, 'build.json'), 'utf8'));
  other.release = '20000101T000000Z-0000000';
  fs.writeFileSync(manifest, zlib.gzipSync(JSON.stringify(other)));
  assert.ok(!(await passes('dist', copy)), 'a build.json.gz with a different manifest fails the build check');
  assert.ok(!(await passes('site', site, copy)), 'a served build.json.gz with a different manifest fails the site check');
  fs.writeFileSync(manifest, goodManifest);
  results.push('a different manifest in build.json.gz fails both checks');

  // A modified .gz copy of a file that the browser smoke test never requests.
  const readme = path.join(copy, 'source/bios/README.md.gz');
  const goodReadme = fs.readFileSync(readme);
  fs.writeFileSync(readme, zlib.gzipSync('not the SeaBIOS source notes\n'));
  assert.ok(!(await passes('site', site, copy)), 'a modified served README.md.gz fails the site check');
  assert.ok(!(await passes('dist', copy)), 'a modified README.md.gz fails the build check');
  fs.writeFileSync(readme, goodReadme);
  results.push('a modified source/bios/README.md.gz fails both checks');

  assert.ok(await passes('dist', copy) && await passes('site', site, copy), 'restored copy passes again');
} finally {
  server.close();
  fs.rmSync(scratch, { recursive: true, force: true });
}
console.log(`verify-web-test ok\n  ${results.join('\n  ')}`);
