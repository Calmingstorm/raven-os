import { V86 } from './libv86.mjs';

const screen = document.getElementById('screen');
const canvas = screen.querySelector('canvas');
const status = document.getElementById('status');
const overlay = document.getElementById('overlay');
const input = document.getElementById('type-input');
const readout = document.getElementById('readout');
const textToggle = document.getElementById('text-toggle');

const emulator = new V86({
  wasm_path: 'v86.wasm',
  memory_size: 128 * 1024 * 1024,
  vga_memory_size: 8 * 1024 * 1024,
  screen: { container: screen, use_graphical_text: true },
  bios: { url: 'seabios.bin' },
  vga_bios: { url: 'vgabios.bin' },
  // The kernel is a Multiboot image, so v86 loads it directly: no bootloader or disk image.
  multiboot: { url: 'ravenos.kernel' },
  // RavenOS has no mouse driver; this also stops a click on the screen from grabbing the pointer.
  disable_mouse: true,
  autostart: true,
});
window.emulator = emulator;

const megabytes = bytes => (bytes / 1048576).toFixed(1);

function setStatus(text, state) {
  status.textContent = text;
  status.dataset.state = state;
}

emulator.add_listener('download-progress', event => {
  if (event.lengthComputable && event.total) {
    setStatus(`Loading ${megabytes(event.loaded)} of ${megabytes(event.total)} MB`, 'loading');
  }
});
emulator.add_listener('emulator-started', () => {
  overlay.hidden = true;
  setStatus('Running. Click the screen and type.', 'running');
  requestAnimationFrame(updateReadout);
});
emulator.add_listener('emulator-stopped', () => setStatus('Stopped', 'stopped'));

// Scale the screen by whole device pixels so the VGA font stays sharp. Below 1:1 (phones) it
// scales smoothly instead, and the text view below the screen does the reading.
function fitScreen() {
  const width = canvas.width || 720;
  const height = canvas.height || 400;
  const full = document.fullscreenElement === screen;
  const ratio = window.devicePixelRatio || 1;
  const availableWidth = full ? window.innerWidth : screen.parentElement.clientWidth - 4;
  const availableHeight = full ? window.innerHeight : Infinity;
  const fit = Math.min(availableWidth / width, availableHeight / height);
  const wholeDevicePixels = Math.floor(fit * ratio);
  const scale = wholeDevicePixels >= 1 ? wholeDevicePixels / ratio : fit;
  canvas.style.width = `${Math.floor(width * scale)}px`;
  canvas.style.height = `${Math.floor(height * scale)}px`;
  canvas.style.imageRendering = wholeDevicePixels >= 1 ? 'pixelated' : 'auto';
}
emulator.add_listener('screen-set-size', () => requestAnimationFrame(fitScreen));
new ResizeObserver(fitScreen).observe(screen.parentElement);
document.addEventListener('fullscreenchange', fitScreen);
window.addEventListener('resize', fitScreen);

// The screen as text: the 80x25 text buffer, with rows that wrapped at column 80 joined back
// into one line, so it reflows to any width and can be selected and copied.
function screenText() {
  // The screen adapter appears once the emulator has initialised, after this script first runs.
  if (!emulator.screen_adapter) return '';
  const rows = emulator.screen_adapter.get_text_screen();
  const lines = [];
  let continues = false;
  for (const row of rows) {
    const trimmed = row.replace(/\s+$/, '');
    if (continues) lines[lines.length - 1] += trimmed;
    else lines.push(trimmed);
    continues = row.length >= 80 && !/\s$/.test(row);
  }
  while (lines.length && lines[lines.length - 1] === '') lines.pop();
  return lines.join('\n');
}

let readoutQueued = false;
function updateReadout() {
  readoutQueued = false;
  if (readout.hidden) return;
  const atBottom = readout.scrollTop + readout.clientHeight >= readout.scrollHeight - 4;
  readout.textContent = screenText();
  if (atBottom) readout.scrollTop = readout.scrollHeight;
}
emulator.add_listener('screen-put-char', () => {
  if (!readoutQueued && !readout.hidden) {
    readoutQueued = true;
    requestAnimationFrame(updateReadout);
  }
});

function showReadout(show) {
  readout.hidden = !show;
  textToggle.setAttribute('aria-expanded', String(show));
  textToggle.textContent = show ? 'Hide text' : 'Show as text';
  if (show) {
    updateReadout();
    readout.scrollTop = readout.scrollHeight;
  }
}
// Phones can't read 80 columns at their width, so they get the text view from the start.
showReadout(window.matchMedia('(max-width: 760px)').matches);
textToggle.addEventListener('click', () => showReadout(readout.hidden));

// Keys reach the machine wherever focus is, except in text fields. Focus goes back to the
// screen after a button press, so Enter or Space can't press the same button again.
function focusScreen() {
  screen.focus({ preventScroll: true });
}

function send(text) {
  emulator.keyboard_send_text(text);
}

document.getElementById('chips').addEventListener('click', event => {
  const chip = event.target.closest('button[data-cmd]');
  if (!chip) return;
  send(chip.dataset.cmd + '\n');
  focusScreen();
});

document.getElementById('type-form').addEventListener('submit', event => {
  event.preventDefault();
  send(input.value + '\n');
  input.value = '';
});

document.getElementById('restart').addEventListener('click', () => {
  emulator.restart();
  focusScreen();
});

document.getElementById('fullscreen').addEventListener('click', () => {
  if (screen.requestFullscreen) screen.requestFullscreen();
  focusScreen();
});

screen.addEventListener('pointerdown', focusScreen);
screen.addEventListener('paste', event => {
  const text = event.clipboardData && event.clipboardData.getData('text');
  if (!text) return;
  event.preventDefault();
  send(text);
});
