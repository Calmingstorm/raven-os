import { V86 } from './libv86.mjs';

const screen = document.getElementById('screen');
const status = document.getElementById('status');
const overlay = document.getElementById('overlay');
const input = document.getElementById('type-input');

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
});
emulator.add_listener('emulator-stopped', () => setStatus('Stopped', 'stopped'));

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
