import { V86 } from './libv86.mjs';

const screen = document.getElementById('screen');
const canvas = screen.querySelector('canvas');
const status = document.getElementById('status');
const overlay = document.getElementById('overlay');
const overlayText = document.getElementById('overlay-text');
const retry = document.getElementById('retry');
const input = document.getElementById('type-input');
const readout = document.getElementById('readout');
const textToggle = document.getElementById('text-toggle');

const PROMPT = 'raven>';
// Page load to the shell prompt. The prompt normally appears in under a second once the files are in.
const BOOT_TIMEOUT_MS = 30000;
// RavenFS's disk: blank, and kept only in this tab's memory.
const SCRATCH_DISK_BYTES = 1024 * 1024;

// The Doom data disk, named by its hash in build.json. Nothing of it downloads until Doom reads it, and then only
// the fixed-size chunks it reads; the image is a whole number of chunks, which v86's chunked reads need.
async function doomDisk() {
  try {
    const response = await fetch('build.json', { cache: 'no-cache' });
    const doom = response.ok ? (await response.json()).doom : null;
    return doom ? { url: doom.image, size: doom.image_bytes, async: true, fixed_chunk_size: doom.chunk_bytes } : undefined;
  } catch {
    return undefined;
  }
}

const emulator = new V86({
  wasm_path: 'v86.wasm',
  memory_size: 128 * 1024 * 1024,
  vga_memory_size: 8 * 1024 * 1024,
  screen: { container: screen, use_graphical_text: true },
  bios: { url: 'seabios.bin' },
  vga_bios: { url: 'vgabios.bin' },
  // The kernel is a Multiboot image, so v86 loads it directly: no bootloader or boot disk.
  multiboot: { url: 'ravenos.kernel' },
  // v86 attaches the second disk (the primary slave, where RavenOS looks for Doom) only beside a first one.
  hda: { buffer: new ArrayBuffer(SCRATCH_DISK_BYTES) },
  hdb: await doomDisk(),
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

// Boot: the overlay stays up until the shell prompt is actually on screen, not merely until the
// emulator starts. A failed download or a missing prompt replaces it with the reason and a Retry.
let booted = false;
let failed = false;
const bootTimer = setTimeout(
  () => fail(`RavenOS didn't reach its prompt within ${BOOT_TIMEOUT_MS / 1000} seconds.`), BOOT_TIMEOUT_MS);

function fail(message) {
  if (booted || failed) return;
  failed = true;
  clearTimeout(bootTimer);
  emulator.stop();
  overlay.hidden = false;
  overlay.dataset.state = 'error';
  overlayText.textContent = message;
  retry.hidden = false;
  setStatus(message, 'stopped');
}
retry.addEventListener('click', () => location.reload());

// A CPU fault halts RavenOS with two lines on its serial port. The screen just stops (in graphics mode, on the
// last frame), so say what happened.
let serialLine = '';
let previousSerialLine = '';
emulator.add_listener('serial0-output-byte', byte => {
  if (byte !== 10 && byte !== 13) {
    if (serialLine.length < 300) serialLine += String.fromCharCode(byte);
    return;
  }
  if (!serialLine) return;
  if (serialLine === 'interrupts disabled; halted' && previousSerialLine.startsWith('FATAL exception=')) {
    halted(previousSerialLine);
  }
  previousSerialLine = serialLine;
  serialLine = '';
});

function halted(fault) {
  overlay.hidden = false;
  overlay.dataset.state = 'error';
  overlayText.textContent = `RavenOS stopped on a CPU fault (${fault.slice(6)}). Restart to start it again.`;
  retry.textContent = 'Restart';
  retry.hidden = false;
  setStatus('Stopped on a CPU fault.', 'stopped');
}

emulator.add_listener('download-progress', event => {
  if (!failed && event.lengthComputable && event.total) {
    setStatus(`Loading ${megabytes(event.loaded)} of ${megabytes(event.total)} MB`, 'loading');
  }
});
emulator.add_listener('download-error', event => {
  fail(`Couldn't download ${event.file_name || 'a file the machine needs'}. Check your connection, then retry.`);
});
emulator.add_listener('emulator-started', () => {
  if (!failed) setStatus('Booting', 'loading');
});

function checkBooted() {
  if (booted || failed || !screenText().includes(PROMPT)) return;
  booted = true;
  clearTimeout(bootTimer);
  overlay.hidden = true;
  updateStatus();
  // Typing should just work on arrival, unless the visitor has already focused something else.
  if (document.activeElement === document.body) focusScreen();
}

// Scale the screen by whole device pixels so the VGA font stays sharp. Below 1:1 (phones) it
// scales smoothly instead, and the text view below the screen does the reading.
function fitScreen() {
  const width = canvas.width || 720;
  const height = canvas.height || 400;
  if (!graphical && canvas.width) textCanvasSize = [canvas.width, canvas.height];
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
// v86 reports [columns, rows, 0] for text mode and [width, height, bits per pixel] for graphics.
let graphical = false;
let textCanvasSize = null;
emulator.add_listener('screen-set-size', ([, , bpp]) => {
  const leavingGraphics = graphical && !bpp;
  graphical = bpp > 0;
  // v86 0.5.473 puts the visible canvas back to text size only when its hidden text canvas changes
  // size, which it doesn't when graphics mode ends at the same 80x25. The text then draws into the
  // 640-pixel graphics canvas and loses its right edge. Restore the size and redraw, and forget v86's
  // cached graphics size so the next graphics mode resizes the canvas again.
  if (leavingGraphics && textCanvasSize) {
    const [width, height] = textCanvasSize;
    const vga = emulator.v86.cpu.devices.vga;
    vga.screen_width = vga.screen_height = 0;
    emulator.screen_adapter.set_size_graphical(width, height, width, height);
    vga.complete_redraw();
  }
  requestAnimationFrame(() => {
    fitScreen();
    updateReadout();
  });
});
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

function updateReadout() {
  if (readout.hidden) return;
  if (graphical) {
    readout.textContent = 'The screen is in graphics mode. The text view comes back when RavenOS returns to text.';
    return;
  }
  const atBottom = readout.scrollTop + readout.clientHeight >= readout.scrollHeight - 4;
  readout.textContent = screenText();
  if (atBottom) readout.scrollTop = readout.scrollHeight;
}

let screenQueued = false;
emulator.add_listener('screen-put-char', () => {
  if (screenQueued) return;
  screenQueued = true;
  requestAnimationFrame(() => {
    screenQueued = false;
    checkBooted();
    updateReadout();
  });
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

// Keys go to RavenOS only while the screen has focus, so Tab and other keys work normally on the
// rest of the page. Inside the screen Tab belongs to the guest; Shift+Escape hands focus back.
function updateStatus() {
  if (!booted || failed) return;
  setStatus(document.activeElement === screen
    ? 'Running. Keys go to RavenOS; Shift+Esc gives them back.'
    : 'Running. Click the screen to type.', 'running');
}
function focusScreen() {
  screen.focus({ preventScroll: true });
}
emulator.add_listener('emulator-ready', () => emulator.keyboard_set_enabled(document.activeElement === screen));
screen.addEventListener('focus', () => {
  emulator.keyboard_set_enabled(true);
  updateStatus();
});
screen.addEventListener('blur', () => {
  // v86 releases every key it holds down in the guest when the window loses focus. Do the same when
  // the screen does, or a key held while leaving (the Shift of Shift+Esc) stays down in the guest:
  // its release arrives after the keyboard is disabled.
  window.dispatchEvent(new Event('blur'));
  emulator.keyboard_set_enabled(false);
  updateStatus();
});
window.addEventListener('keydown', event => {
  if (event.key === 'Escape' && event.shiftKey && document.activeElement === screen) {
    event.preventDefault();
    event.stopImmediatePropagation();
    input.focus();
  }
}, true);

// v86 drops simulated keys too while the keyboard is disabled, so enable it around the (synchronous)
// send and then restore the focus rule.
function typeIntoGuest(text) {
  emulator.keyboard_set_enabled(true);
  emulator.keyboard_send_text(text);
  emulator.keyboard_set_enabled(document.activeElement === screen);
}

// A command from a button or the line box replaces whatever is half-typed at the prompt rather than
// being appended to it: Ctrl+U clears the shell's whole input line, wherever its cursor is. These are
// Ctrl down, U down, U up, Ctrl up (scancode set 1), sent straight to the emulated keyboard.
const CTRL_U = [0x1d, 0x16, 0x96, 0x9d];
function runCommand(command) {
  emulator.keyboard_send_scancodes(CTRL_U);
  typeIntoGuest(command + '\n');
}

document.getElementById('chips').addEventListener('click', event => {
  const chip = event.target.closest('button[data-cmd]');
  if (!chip) return;
  runCommand(chip.dataset.cmd);
  focusScreen();
});

document.getElementById('type-form').addEventListener('submit', event => {
  event.preventDefault();
  runCommand(input.value);
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
  typeIntoGuest(text);
});
