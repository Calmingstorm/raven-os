#!/usr/bin/env python3
"""Graphics regression in QEMU: the gfx test pattern, the console's return, and an unsupported display.

QEMU's std-vga shares video memory between the linear framebuffer and the VGA planes, so this is where the
saved text font must actually be written back: the text screen after gfx has to render pixel-identically to
the one before it. A Cirrus card has no Bochs VBE interface, so there gfx must refuse and keep the console.
Build first. Evidence (screendumps, serial) stays under /tmp/raven-gfx-* on failure.
"""
import argparse
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
KEYS = {" ": "spc", "\n": "ret", "-": "minus"}


def check(condition, message):
    if not condition:
        raise AssertionError(message)


class Machine:
    def __init__(self, qemu, kernel, vga, directory):
        self.directory = directory
        self.sequence = 0
        self.log = (directory / f"qemu-{vga}.log").open("wb")
        self.serial_path = directory / f"serial-{vga}.log"
        command = [qemu, "-accel", "tcg", "-m", "128M", "-vga", vga, "-display", "none", "-monitor", "none",
                   "-nic", "none", "-serial", "file:" + str(self.serial_path), "-kernel", str(kernel),
                   "-no-reboot", "-qmp", "unix:" + str(directory / f"qmp-{vga}") + ",server=on,wait=off"]
        self.process = subprocess.Popen(command, stdout=self.log, stderr=self.log)
        self.sock = socket.socket(socket.AF_UNIX)
        deadline = time.monotonic() + 10
        while True:
            try:
                self.sock.connect(str(directory / f"qmp-{vga}"))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                check(self.process.poll() is None, "QEMU exited before QMP was ready")
                check(time.monotonic() < deadline, "QMP socket never appeared")
                time.sleep(0.02)
        self.sock.settimeout(10)
        self.qmp = self.sock.makefile("rwb", buffering=0)
        check("QMP" in json.loads(self.qmp.readline()), "no QMP greeting")
        self.call("qmp_capabilities")
        self.wait_for(lambda: b"raven> " in self.serial(), "the prompt never appeared", 20)

    def call(self, name, arguments=None):
        self.sequence += 1
        request = {"execute": name, "id": self.sequence}
        if arguments is not None:
            request["arguments"] = arguments
        self.qmp.write(json.dumps(request).encode() + b"\n")
        while True:
            response = json.loads(self.qmp.readline())
            if response.get("id") == self.sequence:
                check("error" not in response, f"QMP {name}: {response}")
                return response.get("return")

    def serial(self):
        return self.serial_path.read_bytes() if self.serial_path.exists() else b""

    def wait_for(self, condition, message, seconds=10):
        deadline = time.monotonic() + seconds
        while not condition():
            check(self.process.poll() is None, "QEMU exited")
            check(time.monotonic() < deadline, message)
            time.sleep(0.1)

    def key(self, qcode):
        for down in (True, False):
            self.call("input-send-event", {"events": [
                {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": qcode}}}]})
            time.sleep(0.02)

    def type(self, text):
        for char in text:
            self.key(KEYS.get(char, char))

    def screen(self, name):
        """A rendered screendump as (width, height, rows of RGB tuples)."""
        path = self.directory / f"{name}.ppm"
        self.call("screendump", {"filename": str(path)})
        data = path.read_bytes()
        parts = data.split(b"\n", 3)
        check(parts[0] == b"P6" and parts[2] == b"255", f"unexpected screendump header in {path}")
        width, height = map(int, parts[1].split())
        pixels = parts[3]
        rows = [[tuple(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) for x in range(width)] for y in range(height)]
        return width, height, rows

    def text(self):
        """The 80x25 text buffer from guest memory (characters only)."""
        path = self.directory / "vram.bin"
        self.call("human-monitor-command", {"command-line": f'pmemsave 0xb8000 4000 "{path}"'})
        cells = struct.unpack("<2000H", path.read_bytes())
        return ["".join(chr(c & 255) for c in cells[y * 80:(y + 1) * 80]).rstrip() for y in range(25)]

    def wait_for_text(self, wanted, seconds=10):
        self.wait_for(lambda: any(wanted in row for row in self.text()), f"screen never showed {wanted!r}", seconds)

    def wait_for_size(self, size, name, seconds=10):
        deadline = time.monotonic() + seconds
        while True:
            shot = self.screen(name)
            if shot[:2] == size:
                return shot
            check(time.monotonic() < deadline, f"screen stayed {shot[:2]}, never {size}")
            time.sleep(0.2)

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.sock.close()
        self.log.close()


def std_vga(qemu, kernel, directory):
    m = Machine(qemu, kernel, "std", directory)
    try:
        m.type("clear\n")
        m.wait_for_text("Type help or help controls")
        time.sleep(0.5)
        width, height, before = m.screen("text-before")
        check((width, height) == (720, 400), f"text mode renders at {width}x{height}, expected 720x400")
        top = 12 * 16  # the rows of the home screen that gfx must leave untouched
        for cycle in range(1, 4):
            m.type("gfx\n")
            _, _, pattern = m.wait_for_size((640, 400), f"gfx-{cycle}")
            time.sleep(0.3)
            _, _, pattern = m.screen(f"gfx-{cycle}")
            got = [pattern[150][i * 80 + 40] for i in range(8)]
            check(got == BARS, f"cycle {cycle}: colour bars {got}")
            check(pattern[320][2] == (0, 0, 0) and pattern[320][637] == (254, 254, 254),
                  f"cycle {cycle}: grey ramp ends {pattern[320][2]} {pattern[320][637]}")
            m.key("x")
            _, _, after = m.wait_for_size((720, 400), f"text-after-{cycle}")
            m.wait_for_text("gfx: back in text mode")
            time.sleep(0.3)
            _, _, after = m.screen(f"text-after-{cycle}")
            differing = sum(1 for y in range(top) for x in range(720) if after[y][x] != before[y][x])
            check(differing == 0, f"cycle {cycle}: {differing} pixels of the untouched rows changed: font not restored")
        m.type("gfx info\n")
        m.wait_for_text("entered: 3")
        m.type("mem\n")
        m.wait_for_text("physical memory manager")
        return "std-vga: 3 cycles, exact bars and ramp, text rows pixel-identical afterwards, prompt usable"
    finally:
        m.close()


def cirrus(qemu, kernel, directory):
    m = Machine(qemu, kernel, "cirrus", directory)
    try:
        m.type("gfx\n")
        m.wait_for_text("staying in text mode")
        refusal = next(row for row in m.text() if "staying in text mode" in row)
        m.type("mem\n")
        m.wait_for_text("physical memory manager")
        return f"cirrus: refused with \"{refusal.strip()}\", console still works"
    finally:
        m.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--kernel", default=str(ROOT / "build" / "ravenos.kernel"))
    args = parser.parse_args()
    directory = Path(tempfile.mkdtemp(prefix="raven-gfx-"))
    try:
        results = [std_vga(args.qemu, args.kernel, directory), cirrus(args.qemu, args.kernel, directory)]
    except Exception:
        print(f"gfx-qemu-test: FAILED; evidence in {directory}")
        raise
    shutil.rmtree(directory)
    print("gfx-qemu-test ok\n  " + "\n  ".join(results))


if __name__ == "__main__":
    main()
