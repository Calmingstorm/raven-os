#!/usr/bin/env node
// Check a web build against its build.json manifest.
//   node tools/verify-web.mjs dist <dir> [required-file ...]
//     every file listed in build.json matches its sha256, each .gz copy (build.json.gz included) decompresses to
//     the same bytes, the required files are listed, and nothing but build.json and .gz copies is unlisted.
//   node tools/verify-web.mjs site <url> <dir>
//     the site serves exactly <dir>/build.json, and every listed file matches its sha256. Each is requested twice:
//     uncompressed, and with gzip accepted (nginx's gzip_static then serves the .gz copy browsers get).
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
  console.log(`verify-web: ${target} serves release ${build.release}; all ${Object.keys(build.files).length} files verified uncompressed and gzipped`);
} else {
  console.error('usage: verify-web.mjs dist <dir> [required ...] | site <url> <dir>');
  process.exit(2);
}
