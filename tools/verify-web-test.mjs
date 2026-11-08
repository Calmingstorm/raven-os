#!/usr/bin/env node
// Regression tests for tools/verify-web.mjs: damaged gzip copies must fail both the build check and the site
// check, and a server that mishandles the Doom disk's byte ranges must fail the site check. Works on a scratch
// copy of web/dist (run tools/build-web.sh first) served by a small nginx-like server: a request that accepts gzip
// gets the .gz copy when there is one, a single byte range gets a 206, and .img files are cacheable for good.
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import crypto from 'node:crypto';
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

// How the server answers byte ranges: 'honour' as nginx does, 'ignore' (the whole file, 200), 'gzip' (a 206
// compressed anyway), or 'stale' (a 206 from different bytes).
let ranges = 'honour';
const server = http.createServer((req, res) => {
  let name = decodeURIComponent(new URL(req.url, 'http://local').pathname);
  if (name.endsWith('/')) name += 'index.html';
  const file = path.join(copy, path.normalize(name));
  if (!file.startsWith(copy) || !fs.existsSync(file)) { res.writeHead(404).end(); return; }
  const headers = file.endsWith('.img') ? { 'cache-control': 'public, max-age=31536000, immutable' } : {};
  const range = /^bytes=(\d+)-(\d+)$/.exec(req.headers.range || '');
  if (range && ranges !== 'ignore') {
    const bytes = fs.readFileSync(file);
    const start = Number(range[1]);
    const end = Math.min(Number(range[2]), bytes.length - 1);
    let part = bytes.subarray(start, end + 1);
    if (ranges === 'stale') part = Buffer.from(part.map(byte => byte ^ 0xff));
    headers['content-range'] = `bytes ${start}-${end}/${bytes.length}`;
    if (ranges === 'gzip') { headers['content-encoding'] = 'gzip'; part = zlib.gzipSync(part); }
    res.writeHead(206, headers).end(part);
    return;
  }
  const gzip = /\bgzip\b/.test(req.headers['accept-encoding'] || '') && fs.existsSync(`${file}.gz`);
  if (gzip) headers['content-encoding'] = 'gzip';
  res.writeHead(200, headers);
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

  // The Doom disk read the way v86 reads it.
  for (const [mode, failure] of [['ignore', 'the whole file for a range'], ['gzip', 'a compressed range'],
    ['stale', 'a range with the wrong bytes']]) {
    ranges = mode;
    assert.ok(!(await passes('site', site, copy)), `a server sending ${failure} fails the site check`);
    results.push(`a server sending ${failure} fails the site check`);
  }
  ranges = 'honour';

  // A Doom disk that isn't a whole number of fetch chunks.
  const buildJson = path.join(copy, 'build.json');
  const goodBuild = fs.readFileSync(buildJson);
  const build = JSON.parse(goodBuild.toString('utf8'));
  const image = path.join(copy, build.doom.image);
  const goodSize = fs.statSync(image).size;
  const goodGz = fs.readFileSync(`${buildJson}.gz`);
  const tail = Buffer.alloc(512);
  const fd = fs.openSync(image, 'r');
  fs.readSync(fd, tail, 0, 512, goodSize - 512);
  fs.closeSync(fd);
  fs.truncateSync(image, goodSize - 512);
  build.files[build.doom.image] = crypto.createHash('sha256').update(fs.readFileSync(image)).digest('hex');
  build.doom.image_bytes = goodSize - 512;
  fs.writeFileSync(buildJson, JSON.stringify(build, null, 1) + '\n');
  fs.rmSync(`${buildJson}.gz`);
  assert.ok(!(await passes('dist', copy)), 'a Doom disk that ends mid-chunk fails the build check');
  fs.appendFileSync(image, tail);
  fs.writeFileSync(buildJson, goodBuild);
  fs.writeFileSync(`${buildJson}.gz`, goodGz);
  results.push('a Doom disk that ends mid-chunk fails the build check');

  assert.ok(await passes('dist', copy) && await passes('site', site, copy), 'restored copy passes again');
} finally {
  server.close();
  fs.rmSync(scratch, { recursive: true, force: true });
}
console.log(`verify-web-test ok\n  ${results.join('\n  ')}`);
