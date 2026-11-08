#!/usr/bin/env node
// Boot web/dist in headless Chrome the way a visitor does and check that the machine answers.
// Usage: node tools/check-web.mjs [--url <site>] [--screenshots <dir>]
// Without --url it serves web/dist itself (run tools/build-web.sh first).
// Needs Chrome or Chromium: CHROME_PATH, /usr/bin/google-chrome or /usr/bin/chromium.
//
// Helpers for guest-screen assertions: screenRows(page) is the 25 text rows, and cursor(page) is the
// VGA hardware cursor ({row, col}, from the emulated CRTC's cursor location register).
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
  '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.txt': 'text/plain', '.json': 'application/json',
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

export const screenRows = page => page.evaluate(() => window.emulator.screen_adapter.get_text_screen());
export const cursor = page => page.evaluate(() => {
  const address = window.emulator.v86.cpu.devices.vga.cursor_address;
  return { row: Math.floor(address / 80), col: address % 80 };
});
const screenText = async page => (await screenRows(page)).join('\n');

async function waitForScreen(page, text, timeout = 10000) {
  await page.waitForFunction(
    wanted => window.emulator.screen_adapter.get_text_screen().join('\n').includes(wanted), text, { timeout },
  ).catch(async () => assert.fail(`screen never showed ${JSON.stringify(text)}; it shows:\n${await screenText(page)}`));
}

async function open(width, { block } = {}) {
  const page = await browser.newPage({ viewport: { width, height: 900 }, deviceScaleFactor: 1 });
  const problems = [];
  page.on('pageerror', error => problems.push(`page error: ${error.message}`));
  if (block) {
    await page.route(`**/${block}`, route => route.fulfill({ status: 404, body: 'gone' }));
  } else {
    page.on('requestfailed', request => problems.push(`request failed: ${request.url()}`));
    page.on('response', response => { if (response.status() >= 400) problems.push(`HTTP ${response.status()}: ${response.url()}`); });
  }
  const started = Date.now();
  await page.goto(url);
  if (!block) {
    await page.waitForFunction(() => document.getElementById('status').dataset.state === 'running', null, { timeout: 30000 });
    await waitForScreen(page, 'raven>');
  }
  return { page, problems, bootSeconds: (Date.now() - started) / 1000 };
}

const results = [];
try {
  // Desktop: the real screen at whole-pixel scale, every way of typing, the text view on demand.
  const desktop = await open(1280);
  const { page } = desktop;
  assert.equal(await page.evaluate(() => document.querySelector('#screen canvas').style.width), '720px',
    'at 1280 px the 720x400 text screen is drawn at exactly 1:1');
  assert.equal(await page.isVisible('#overlay'), false, 'the overlay is gone once the prompt is on screen');
  assert.equal(await page.isVisible('#readout'), false, 'the text view starts hidden on desktop');
  assert.equal(await page.evaluate(() => document.activeElement.id), 'screen', 'the screen has focus on arrival');
  const at = await cursor(page);
  assert.ok(Number.isInteger(at.row) && Number.isInteger(at.col), 'the cursor helper reads the VGA cursor');

  await page.getByRole('button', { name: 'about', exact: true }).click();
  await waitForScreen(page, 'inspectable machine first');
  await page.fill('#type-input', 'timeline');
  await page.press('#type-input', 'Enter');
  await waitForScreen(page, 'wyrd timeline');
  await page.click('#screen');
  await page.keyboard.type('mem\n');
  await waitForScreen(page, 'physical memory manager');

  // A button replaces half-typed input instead of being appended to it.
  await page.keyboard.type('abo');
  await page.getByRole('button', { name: 'tasks', exact: true }).click();
  await page.waitForTimeout(800);
  const afterButton = await screenText(page);
  assert.ok(!afterButton.includes('abotasks') && !afterButton.includes('unknown command'),
    `typing "abo" then pressing the tasks button ran something else:\n${afterButton}`);
  assert.ok(afterButton.split('\n').some(row => row.trimEnd() === 'raven> tasks'), 'the tasks button ran exactly "tasks"');

  // Keys reach the guest only while the screen has focus; Shift+Esc hands them back; Tab stays inside.
  await page.click('#screen');
  await page.keyboard.press('Tab');
  assert.equal(await page.evaluate(() => document.activeElement.id), 'screen', 'Tab inside the screen goes to the guest');
  await page.keyboard.press('Shift+Escape');
  assert.notEqual(await page.evaluate(() => document.activeElement.id), 'screen', 'Shift+Esc leaves the screen');
  const before = await screenText(page);
  await page.focus('#restart');
  await page.keyboard.type('zz');
  await page.waitForTimeout(400);
  assert.equal(await screenText(page), before, 'keys typed outside the screen do not reach the guest');
  await page.keyboard.press('Tab');
  assert.notEqual(await page.evaluate(() => document.activeElement.id), 'restart', 'Tab moves through the page');

  await page.getByRole('button', { name: 'Show as text' }).click();
  assert.match(await page.textContent('#readout'), /physical memory manager/, 'the text view shows the screen');
  if (shots) await page.screenshot({ path: path.join(shots, 'check-web-desktop.png'), fullPage: true });
  assert.deepEqual(desktop.problems, []);
  results.push(`desktop: booted in ${desktop.bootSeconds.toFixed(1)} s; chip, line box and keyboard reach the shell; `
    + 'buttons replace half-typed input; keys only while focused, Shift+Esc leaves, Tab stays inside');

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

  // A failed download says what failed and offers a retry instead of hanging on "Starting".
  const broken = await open(1280, { block: 'ravenos.kernel' });
  await broken.page.waitForFunction(() => document.getElementById('overlay').dataset.state === 'error', null, { timeout: 15000 })
    .catch(async () => assert.fail(`no error shown for a missing kernel; overlay says: ${await broken.page.textContent('#overlay')}`));
  assert.match(await broken.page.textContent('#overlay-text'), /ravenos\.kernel/, 'the error names the file');
  assert.equal(await broken.page.isVisible('#retry'), true, 'a Retry button is offered');
  assert.equal(await broken.page.evaluate(() => document.getElementById('status').dataset.state), 'stopped');
  assert.deepEqual(broken.problems, []);
  results.push('failed download: the overlay names ravenos.kernel and offers Retry');
} finally {
  await browser.close();
  if (server) server.close();
}
console.log(`check-web ok (${url})\n  ${results.join('\n  ')}`);
