#!/usr/bin/env node
// Check a web build against its build.json manifest.
//   node tools/verify-web.mjs dist <dir> [required-file ...]
//     every file listed in build.json matches its sha256, each .gz copy (build.json.gz included) decompresses to
//     the same bytes, the required files are listed, and nothing but build.json and .gz copies is unlisted. The
//     Doom disk is the listed size, a whole number of the page's fetch chunks, and has no .gz copy.
//   node tools/verify-web.mjs site <url> <dir>
//     the site serves exactly <dir>/build.json, and every listed file matches its sha256. Each is requested twice:
//     uncompressed, and with gzip accepted (nginx's gzip_static then serves the .gz copy browsers get). The Doom
//     disk must also answer byte ranges the way v86 reads it: a 206 with the exact Content-Range and bytes,
//     uncompressed, and cacheable for good.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import https from 'node:https';
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

// GET with exact control of Accept-Encoding; returns the decoded body and the encoding the server chose.
function get(url, acceptEncoding) {
  const client = url.protocol === 'https:' ? https : http;
  return new Promise((resolve, reject) => {
    const request = client.get(url, { headers: { 'accept-encoding': acceptEncoding, 'cache-control': 'no-cache' } }, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => {
        const raw = Buffer.concat(chunks);
        const encoding = response.headers['content-encoding'] || 'identity';
        try {
          const body = encoding === 'gzip' ? zlib.gunzipSync(raw)
            : encoding === 'br' ? zlib.brotliDecompressSync(raw)
            : encoding === 'identity' ? raw : null;
          if (!body) throw new Error(`unexpected content-encoding ${encoding}`);
          resolve({ status: response.statusCode, encoding, body });
        } catch (error) {
          reject(new Error(`${url.pathname} (${acceptEncoding}): ${error.message}`));
        }
      });
    });
    request.on('error', reject);
    request.setTimeout(20000, () => request.destroy(new Error(`${url.pathname}: timed out`)));
  });
}

// One byte range, requested as a browser's XHR does (compression accepted). Returns the raw response.
function getRange(url, start, end) {
  const client = url.protocol === 'https:' ? https : http;
  return new Promise((resolve, reject) => {
    const headers = { range: `bytes=${start}-${end}`, 'accept-encoding': 'gzip, deflate, br', 'cache-control': 'no-cache' };
    const request = client.get(url, { headers }, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolve({ status: response.statusCode, headers: response.headers, body: Buffer.concat(chunks) }));
    });
    request.on('error', reject);
    request.setTimeout(20000, () => request.destroy(new Error(`${url.pathname}: timed out`)));
  });
}

if (mode === 'dist') {
  const manifest = fs.readFileSync(path.join(target, 'build.json'));
  const build = JSON.parse(manifest.toString('utf8'));
  for (const name of rest) {
    if (name !== 'build.json') assert.ok(build.files[name], `build.json does not list ${name}`);
  }
  const manifestGz = path.join(target, 'build.json.gz');
  if (fs.existsSync(manifestGz)) {
    assert.ok(zlib.gunzipSync(fs.readFileSync(manifestGz)).equals(manifest), 'build.json.gz does not match build.json');
  }
  for (const [name, hash] of Object.entries(build.files)) {
    const bytes = fs.readFileSync(path.join(target, name));
    assert.equal(sha256(bytes), hash, `${name} does not match build.json`);
    const gz = path.join(target, `${name}.gz`);
    if (fs.existsSync(gz)) assert.equal(sha256(zlib.gunzipSync(fs.readFileSync(gz))), hash, `${name}.gz does not match build.json`);
  }
  if (build.doom) {
    const { image, image_bytes: size, chunk_bytes: chunk } = build.doom;
    assert.ok(build.files[image], `build.json does not list the Doom disk ${image}`);
    assert.equal(fs.statSync(path.join(target, image)).size, size, `${image} is not ${size} bytes`);
    assert.ok(size > 0 && chunk > 0 && size % chunk === 0, `${image} is not a whole number of ${chunk}-byte chunks`);
    assert.ok(!fs.existsSync(path.join(target, `${image}.gz`)), `${image} must not have a .gz copy`);
  }
  const unlisted = walk(target).filter(name => name !== 'build.json' && name !== 'build.json.gz'
    && !build.files[name] && !(name.endsWith('.gz') && build.files[name.slice(0, -3)]));
  assert.deepEqual(unlisted, [], 'files in the build that build.json does not list');
  console.log(`verify-web: ${target} matches build.json (${Object.keys(build.files).length} files, release ${build.release})`);
} else if (mode === 'site') {
  const local = fs.readFileSync(path.join(rest[0], 'build.json'));
  const build = JSON.parse(local.toString('utf8'));
  const check = async (name, expected) => {
    for (const accept of ['identity', 'gzip']) {
      const { status, encoding, body } = await get(new URL(name, target), accept);
      assert.equal(status, 200, `${name} (${accept}): HTTP ${status}`);
      assert.ok(expected(body), `the site's ${name} does not match build.json when requested with ${accept} (served ${encoding})`);
    }
  };
  await check('build.json', body => body.equals(local));
  for (const [name, hash] of Object.entries(build.files)) await check(name, body => sha256(body) === hash);
  let ranges = '';
  if (build.doom) {
    // v86 aborts a disk read answered with the whole file (200), and caches whatever bytes a 206 brings.
    const { image, image_bytes: size, chunk_bytes: chunk } = build.doom;
    const bytes = fs.readFileSync(path.join(rest[0], image));
    const sector = Math.floor(size / 2 / 512) * 512;
    const middleChunk = sector - sector % chunk;
    const spans = [[0, 511], [sector, sector + 511], [size - 512, size - 1], [middleChunk, middleChunk + chunk - 1],
      [size - chunk, size - 1]];
    for (const [start, end] of spans) {
      const { status, headers, body } = await getRange(new URL(image, target), start, end);
      const what = `${image} bytes ${start}-${end}`;
      assert.equal(status, 206, `${what}: HTTP ${status}, not a range response`);
      assert.equal(headers['content-range'], `bytes ${start}-${end}/${size}`, `${what}: wrong Content-Range`);
      assert.ok([undefined, 'identity'].includes(headers['content-encoding']), `${what}: served ${headers['content-encoding']}-encoded`);
      assert.ok(body.equals(bytes.subarray(start, end + 1)), `${what}: wrong bytes`);
      assert.match(headers['cache-control'] || '', /\bimmutable\b/, `${what}: not cacheable for good`);
    }
    ranges = `; Doom disk byte ranges verified (${spans.length})`;
  }
  console.log(`verify-web: ${target} serves release ${build.release}; all ${Object.keys(build.files).length} files verified uncompressed and gzipped${ranges}`);
} else {
  console.error('usage: verify-web.mjs dist <dir> [required ...] | site <url> <dir>');
  process.exit(2);
}
