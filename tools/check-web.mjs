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
console.log(`check-web: ${executablePath} ${browser.version()}`);

export const screenRows = page => page.evaluate(() => window.emulator.screen_adapter.get_text_screen());
export const cursor = page => page.evaluate(() => {
  const address = window.emulator.v86.cpu.devices.vga.cursor_address;
  return { row: Math.floor(address / 80), col: address % 80 };
});
const screenText = async page => (await screenRows(page)).join('\n');

// Wait until the guest has stopped printing: two identical screens 300 ms apart.
async function settle(page) {
  let previous = await screenText(page);
  for (let i = 0; i < 30; i++) {
    await page.waitForTimeout(300);
    const now = await screenText(page);
    if (now === previous) return now;
    previous = now;
  }
  assert.fail('the screen never stopped changing');
}

// Record clicks, text the page sends, and scancodes reaching the emulated keyboard, so a failed wait
// can say where input stopped.
const instrument = page => page.evaluate(() => {
  const trace = window.__trace = { clicks: [], sent: [], scancodes: 0 };
  document.addEventListener('click', event => trace.clicks.push(event.target.outerHTML.slice(0, 60)), true);
  const send = window.emulator.keyboard_send_text.bind(window.emulator);
  window.emulator.keyboard_send_text = (text, delay) => { trace.sent.push(text); return send(text, delay); };
  const bus = window.emulator.keyboard_adapter.bus;
  const busSend = bus.send.bind(bus);
  bus.send = (name, data) => { if (name === 'keyboard-code') trace.scancodes++; return busSend(name, data); };
});

// On a failed wait, report the state that decides whether keys can reach the guest.
const inputState = page => page.evaluate(() => {
  const ps2 = window.emulator.v86.cpu.devices.ps2;
  return {
    trace: window.__trace,
    focused: document.activeElement && (document.activeElement.id || document.activeElement.tagName),
    keyboardEnabled: window.emulator.keyboard_adapter.emu_enabled,
    ps2: ps2 && {
      stream: ps2.enable_keyboard_stream, haveKeyboard: ps2.have_keyboard, command: ps2.command_register,
      queued: ps2.kbd_buffer && ps2.kbd_buffer.length, byteReady: ps2.next_byte_is_ready,
    },
    status: document.getElementById('status').textContent,
  };
});

async function waitForScreen(page, text, timeout = 10000) {
  await page.waitForFunction(
    wanted => window.emulator.screen_adapter.get_text_screen().join('\n').includes(wanted), text, { timeout },
  ).catch(async () => {
    if (shots) await page.screenshot({ path: path.join(shots, 'check-web-failure.png'), fullPage: true });
    assert.fail(`screen never showed ${JSON.stringify(text)}; input state: ${JSON.stringify(await inputState(page))}; `
      + `page log: ${JSON.stringify(page.log)}; it shows:\n${await screenText(page)}`);
  });
}

async function open(width, { block } = {}) {
  const page = await browser.newPage({ viewport: { width, height: 900 }, deviceScaleFactor: 1 });
  const problems = [];
  page.log = [];
  page.on('pageerror', error => { problems.push(`page error: ${error.message}`); page.log.push(`pageerror: ${error.message}`); });
  page.on('console', message => page.log.push(`${message.type()}: ${message.text().slice(0, 200)}`));
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
    await instrument(page);
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

  // Nothing below the screen moves when focus enters or leaves it: CI's fonts once wrapped the
  // status line, the buttons shifted mid-click, and the click landed on the page instead.
  // The box is read only once layout has settled: a resize rescales the screen a frame or two later.
  const aboutBox = async () => {
    let previous = null;
    for (let i = 0; i < 20; i++) {
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      const box = await page.getByRole('button', { name: 'about', exact: true }).boundingBox();
      if (previous && JSON.stringify(box) === JSON.stringify(previous)) return box;
      previous = box;
    }
    assert.fail('the layout never settled');
  };
  for (const width of [1280, 600, 390]) {
    await page.setViewportSize({ width, height: 900 });
    await page.focus('#screen');
    const inside = await aboutBox();
    await page.focus('#restart');
    assert.deepEqual(await aboutBox(), inside, `at ${width} px the buttons move when focus leaves the screen`);
  }
  await page.setViewportSize({ width: 1280, height: 900 });
  await page.focus('#screen');

  await page.getByRole('button', { name: 'about', exact: true }).click();
  await waitForScreen(page, 'inspectable machine first');
  await page.fill('#type-input', 'timeline');
  await page.press('#type-input', 'Enter');
  await waitForScreen(page, 'wyrd timeline');
  await page.click('#screen');
  await page.keyboard.type('mem\n');
  await waitForScreen(page, 'physical memory manager');

  // A button replaces half-typed input, trailing spaces included, instead of being appended to it.
  await page.keyboard.type('abo        ');
  await page.getByRole('button', { name: 'tasks', exact: true }).click();
  await waitForScreen(page, 'preemptive-accounting');
  const afterButton = await screenText(page);
  assert.ok(!afterButton.includes('unknown command'),
    `typing "abo" and spaces, then pressing the tasks button, ran something else:\n${afterButton}`);
  assert.ok(afterButton.split('\n').some(row => row.trimEnd() === 'raven> tasks'), 'the tasks button ran exactly "tasks"');

  // With the cursor moved into the middle of half-typed input, a button still replaces all of it.
  await page.click('#screen');
  await page.keyboard.type('about');
  await page.keyboard.press('ArrowLeft');
  await page.keyboard.press('ArrowLeft');
  await page.getByRole('button', { name: 'services', exact: true }).click();
  await settle(page);
  const afterMidLine = (await screenRows(page)).map(row => row.trimEnd());
  assert.ok(afterMidLine.includes('raven> services') && !afterMidLine.some(row => row.includes('unknown command')),
    `a button with the cursor mid-line ran something else:\n${afterMidLine.join('\n')}`);

  // A full-length line (the shell takes 159 characters) arrives whole and runs.
  const longest = 'x'.repeat(159);
  await page.fill('#type-input', longest + 'overflow');
  assert.equal(await page.inputValue('#type-input'), longest, 'the line box stops at the shell limit');
  await page.press('#type-input', 'Enter');
  await page.waitForFunction(text => window.emulator.screen_adapter.get_text_screen().join('').includes(text),
    `unknown command: ${longest}`, { timeout: 10000 })
    .catch(async () => assert.fail(`a 159-character line did not arrive whole:\n${await screenText(page)}`));

  // Keys reach the guest only while the screen has focus; Shift+Esc hands them back; Tab stays inside.
  await page.click('#screen');
  await page.keyboard.press('Tab');
  assert.equal(await page.evaluate(() => document.activeElement.id), 'screen', 'Tab inside the screen goes to the guest');
  await page.keyboard.press('Shift+Escape');
  assert.notEqual(await page.evaluate(() => document.activeElement.id), 'screen', 'Shift+Esc leaves the screen');
  // The Shift of Shift+Esc is released in the guest too: commands afterwards aren't typed in capitals.
  await page.getByRole('button', { name: 'about', exact: true }).click();
  await page.waitForTimeout(800);
  const afterLeaving = await screenText(page);
  assert.ok(!afterLeaving.includes('ABOUT'), `Shift stayed down in the guest after Shift+Esc:\n${afterLeaving}`);
  await page.keyboard.type('mem\n');
  await waitForScreen(page, 'raven> mem');
  await settle(page);
  await page.keyboard.press('Shift+Escape');
  const before = await screenText(page);
  await page.focus('#restart');
  await page.keyboard.type('zz');
  await page.waitForTimeout(400);
  assert.equal(await screenText(page), before, 'keys typed outside the screen do not reach the guest');
  await page.keyboard.press('Tab');
  assert.notEqual(await page.evaluate(() => document.activeElement.id), 'restart', 'Tab moves through the page');

  // Graphics: gfx draws its test pattern at 640x400x32, and a key brings back the full 80-column text
  // screen (v86 alone would keep the 640-pixel canvas and clip it). Twice, so a second entry is covered.
  const canvasWidth = () => page.evaluate(() => document.querySelector('#screen canvas').width);
  for (let cycle = 1; cycle <= 2; cycle++) {
    await page.click('#screen');
    await page.keyboard.type('gfx\n');
    await page.waitForFunction(() => document.querySelector('#screen canvas').width === 640, null, { timeout: 10000 })
      .catch(async () => assert.fail(`gfx did not switch to 640x400 (cycle ${cycle}):\n${await screenText(page)}`));
    await page.waitForTimeout(300);
    const pattern = await page.evaluate(() => {
      const g = document.querySelector('#screen canvas').getContext('2d');
      const at = (x, y) => Array.from(g.getImageData(x, y, 1, 1).data.slice(0, 3)).map(v => v.toString(16).padStart(2, '0')).join('');
      return { bars: [0, 1, 2, 3, 4, 5, 6, 7].map(i => at(i * 80 + 40, 150)), dark: at(2, 320), light: at(637, 320) };
    });
    assert.deepEqual(pattern.bars, ['ffffff', 'ffff00', '00ffff', '00ff00', 'ff00ff', 'ff0000', '0000ff', '000000'],
      `the colour bars (cycle ${cycle})`);
    assert.deepEqual([pattern.dark, pattern.light], ['000000', 'fefefe'], `the grey ramp (cycle ${cycle})`);
    await page.keyboard.press('x');
    await page.waitForFunction(() => document.querySelector('#screen canvas').width === 720, null, { timeout: 10000 })
      .catch(async () => assert.fail(`the text screen came back ${await canvasWidth()} pixels wide, not 720 (cycle ${cycle})`));
    await settle(page);
    const rows = (await screenRows(page)).map(row => row.trimEnd()).filter(Boolean);
    assert.deepEqual(rows.slice(-2), ['gfx: back in text mode', 'raven>'], `gfx returns to a prompt (cycle ${cycle})`);
  }
  // After graphics the cursor sits at the prompt and scrollback still works.
  const promptRow = (await screenRows(page)).map(row => row.trimEnd()).lastIndexOf('raven>');
  assert.deepEqual(await cursor(page), { row: promptRow, col: 7 }, 'the cursor is back at the prompt after gfx');
  const live = await screenText(page);
  await page.keyboard.press('Shift+PageUp');
  await settle(page);
  assert.notEqual(await screenText(page), live, 'Shift+PgUp scrolls back after gfx');
  await page.keyboard.press('Shift+PageDown');
  await settle(page);
  assert.equal(await screenText(page), live, 'Shift+PgDn returns to the live screen');
  await page.keyboard.type('gfx info\n');
  await waitForScreen(page, 'entered: 2');

  await page.getByRole('button', { name: 'Show as text' }).click();
  assert.match(await page.textContent('#readout'), /entered: 2/, 'the text view shows the screen');
  if (shots) await page.screenshot({ path: path.join(shots, 'check-web-desktop.png'), fullPage: true });
  assert.deepEqual(desktop.problems, []);
  results.push(`desktop: booted in ${desktop.bootSeconds.toFixed(1)} s; chip, line box and keyboard reach the shell; `
    + 'buttons replace half-typed input; keys only while focused, Shift+Esc leaves, Tab stays inside; '
    + 'gfx pattern twice, text screen back at full width');

  // Phone: the text view is open from the start and follows what the machine prints.
  const phone = await open(390);
  assert.equal(await phone.page.isVisible('#readout'), true, 'the text view starts open on a phone');
  await phone.page.fill('#type-input', 'about');
  await phone.page.press('#type-input', 'Enter');
  await waitForScreen(phone.page, 'inspectable machine first');
  await phone.page.waitForFunction(() => document.getElementById('readout').textContent.includes('inspectable machine first'));
  const wrapped = await phone.page.evaluate(() => document.getElementById('readout').scrollWidth <= document.getElementById('readout').clientWidth);
  assert.ok(wrapped, 'the text view wraps to the phone width instead of scrolling sideways');
  // In graphics mode the text view says so instead of showing stale text, then comes back.
  await phone.page.fill('#type-input', 'gfx');
  await phone.page.press('#type-input', 'Enter');
  await phone.page.waitForFunction(() => /graphics mode/.test(document.getElementById('readout').textContent), null, { timeout: 10000 });
  await phone.page.getByRole('button', { name: 'about', exact: true }).click();
  await phone.page.waitForFunction(() => document.getElementById('readout').textContent.includes('gfx: back in text mode'), null, { timeout: 10000 });
  if (shots) await phone.page.screenshot({ path: path.join(shots, 'check-web-phone.png'), fullPage: true });
  assert.deepEqual(phone.problems, []);
  results.push(`phone: booted in ${phone.bootSeconds.toFixed(1)} s; the text view is open, wraps, follows the machine, and covers graphics mode`);

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
