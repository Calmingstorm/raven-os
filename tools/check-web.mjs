#!/usr/bin/env node
// Boot web/dist in headless Chrome the way a visitor does and check that the machine answers.
// Usage: node tools/check-web.mjs [--url <site>] [--screenshots <dir>]
// Without --url it serves web/dist itself (run tools/build-web.sh first).
// Needs Chrome or Chromium: CHROME_PATH, /usr/bin/google-chrome or /usr/bin/chromium.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createRequire } from 'node:module';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const { chromium } = createRequire(path.join(root, 'web/package.json'))('playwright-core');
const args = process.argv.slice(2);
const option = name => (args.includes(name) ? args[args.indexOf(name) + 1] : undefined);
const shots = option('--screenshots');

const types = {
  '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css',
  '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.txt': 'text/plain',
};
let server;
let url = option('--url');
if (!url) {
  const dist = path.join(root, 'web/dist');
  assert.ok(fs.existsSync(path.join(dist, 'index.html')), 'no web/dist/index.html: run tools/build-web.sh first');
  server = http.createServer((req, res) => {
    let name = decodeURIComponent(new URL(req.url, 'http://local').pathname);
    if (name.endsWith('/')) name += 'index.html';
    const file = path.join(dist, path.normalize(name));
    if (!file.startsWith(dist) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
      res.writeHead(404).end();
      return;
    }
    res.writeHead(200, { 'content-type': types[path.extname(file)] || 'application/octet-stream' });
    res.end(fs.readFileSync(file));
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  url = `http://127.0.0.1:${server.address().port}/`;
}

const executablePath = [process.env.CHROME_PATH, '/usr/bin/google-chrome', '/usr/bin/chromium']
  .filter(Boolean).find(candidate => fs.existsSync(candidate));
assert.ok(executablePath, 'the browser check needs Chrome or Chromium');
const browser = await chromium.launch({ executablePath, headless: true, args: ['--no-sandbox'] });

async function open(width) {
  const page = await browser.newPage({ viewport: { width, height: 900 }, deviceScaleFactor: 1 });
  const problems = [];
  page.on('pageerror', error => problems.push(`page error: ${error.message}`));
  page.on('requestfailed', request => problems.push(`request failed: ${request.url()}`));
  page.on('response', response => { if (response.status() >= 400) problems.push(`HTTP ${response.status()}: ${response.url()}`); });
  const started = Date.now();
  await page.goto(url);
  await page.waitForFunction(() => document.getElementById('status').dataset.state === 'running', null, { timeout: 30000 });
  await waitForScreen(page, 'raven>');
  return { page, problems, bootSeconds: (Date.now() - started) / 1000 };
}

const screenText = page => page.evaluate(() => window.emulator.screen_adapter.get_text_screen().join('\n'));

async function waitForScreen(page, text, timeout = 10000) {
  await page.waitForFunction(
    wanted => window.emulator.screen_adapter.get_text_screen().join('\n').includes(wanted), text, { timeout },
  ).catch(async () => assert.fail(`screen never showed ${JSON.stringify(text)}; it shows:\n${await screenText(page)}`));
}

const results = [];
try {
  // Desktop: the real screen at whole-pixel scale, every way of typing, the text view on demand.
  const desktop = await open(1280);
  const { page } = desktop;
  assert.equal(await page.evaluate(() => document.querySelector('#screen canvas').style.width), '720px',
    'at 1280 px the 720x400 text screen is drawn at exactly 1:1');
  assert.equal(await page.isVisible('#readout'), false, 'the text view starts hidden on desktop');
  await page.getByRole('button', { name: 'about', exact: true }).click();
  await waitForScreen(page, 'inspectable machine first');
  await page.fill('#type-input', 'timeline');
  await page.press('#type-input', 'Enter');
  await waitForScreen(page, 'wyrd timeline');
  await page.click('#screen');
  await page.keyboard.type('mem\n');
  await waitForScreen(page, 'physical memory manager');
  await page.getByRole('button', { name: 'Show as text' }).click();
  assert.match(await page.textContent('#readout'), /physical memory manager/, 'the text view shows the screen');
  if (shots) await page.screenshot({ path: path.join(shots, 'check-web-desktop.png'), fullPage: true });
  assert.deepEqual(desktop.problems, []);
  results.push(`desktop: booted in ${desktop.bootSeconds.toFixed(1)} s; chip, line box and keyboard all reach the shell`);

  // Phone: the text view is open from the start and follows what the machine prints.
  const phone = await open(390);
  assert.equal(await phone.page.isVisible('#readout'), true, 'the text view starts open on a phone');
  await phone.page.fill('#type-input', 'about');
  await phone.page.press('#type-input', 'Enter');
  await waitForScreen(phone.page, 'inspectable machine first');
  await phone.page.waitForFunction(() => document.getElementById('readout').textContent.includes('inspectable machine first'));
  const wrapped = await phone.page.evaluate(() => document.getElementById('readout').scrollWidth <= document.getElementById('readout').clientWidth);
  assert.ok(wrapped, 'the text view wraps to the phone width instead of scrolling sideways');
  if (shots) await phone.page.screenshot({ path: path.join(shots, 'check-web-phone.png'), fullPage: true });
  assert.deepEqual(phone.problems, []);
  results.push(`phone: booted in ${phone.bootSeconds.toFixed(1)} s; the text view is open, wraps, and follows the machine`);
} finally {
  await browser.close();
  if (server) server.close();
}
console.log(`check-web ok (${url})\n  ${results.join('\n  ')}`);
