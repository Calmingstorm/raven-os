#!/usr/bin/env python3
"""Diskless console regression: PS/2 keys, colored VRAM, serial and CRTC.

Build first. TCG executes the guest; qtest only reads real VGA cursor registers
while paused. Failure evidence remains under /tmp/raven-console-*.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
PROMPT = "raven> "


def check(condition, message):
    if not condition:
        raise AssertionError(message)


class Console:
    def __init__(self, args, directory):
        self.args, self.directory = args, directory
        self.process = None
        self.sockets, self.files = [], []
        self.sequence = 0

    def start(self):
        error = (self.directory / "qemu.log").open("wb")
        self.files.append(error)
        # No drives, storage backends or host devices, including on failure.
        command = [self.args.qemu, "-accel", "tcg", "-m", "128M",
                   "-display", "none", "-monitor", "none", "-nic", "none",
                   "-serial", "file:" + str(self.directory / "serial.log"),
                   "-kernel", str(self.args.kernel), "-no-reboot",
                   "-qmp", "unix:" + str(self.directory / "qmp") + ",server=on,wait=off",
                   "-qtest", "unix:" + str(self.directory / "qtest") + ",server=on,wait=off",
                   "-qtest-log", "/dev/null"]
        (self.directory / "invocation.json").write_text(json.dumps(command, indent=2) + "\n")
        self.process = subprocess.Popen(command, stdout=error, stderr=error)
        self.qmp = self.connect("qmp")
        check("QMP" in json.loads(self.qmp.readline()), "missing QMP greeting")
        self.call("qmp_capabilities")
        check(not any("inserted" in block for block in self.call("query-block")),
              "unexpected attached block media")
        self.io = self.connect("qtest")
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if PROMPT.encode() in self.serial():
                self.expect("")
                return
            check(self.process.poll() is None, "QEMU exited during boot")
            time.sleep(.05)
        raise AssertionError("guest did not reach prompt within 20s")

    def connect(self, name):
        sock = socket.socket(socket.AF_UNIX)
        self.sockets.append(sock)
        deadline = time.monotonic() + 10
        while True:
            try:
                sock.connect(str(self.directory / name))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                check(self.process.poll() is None, "QEMU exited before socket setup")
                if time.monotonic() >= deadline:
                    raise TimeoutError("QEMU socket not ready: " + name)
                time.sleep(.02)
        sock.settimeout(5)
        stream = sock.makefile("rwb", buffering=0)
        self.files.append(stream)
        return stream

    def call(self, name, arguments=None):
        self.sequence += 1
        request = {"execute": name, "id": self.sequence}
        if arguments is not None:
            request["arguments"] = arguments
        self.qmp.write(json.dumps(request).encode() + b"\n")
        while True:
            data = self.qmp.readline()
            check(data, "QMP connection closed")
            response = json.loads(data)
            if response.get("id") != self.sequence:
                continue
            check("error" not in response, "QMP error: " + repr(response))
            return response.get("return")

    def io_command(self, command):
        self.io.write(command.encode() + b"\n")
        while True:
            line = self.io.readline().decode().strip()
            if line.startswith("IRQ "):
                continue
            check(line.startswith("OK"), "qtest I/O failed: " + line)
            return int(line.split()[1], 0) if len(line.split()) > 1 else None

    def screen(self):
        self.call("stop")
        try:
            check(self.call("query-status")["status"] == "paused", "guest not paused")
            destination = self.directory / "last-vram.bin"
            result = self.call("human-monitor-command", {
                "command-line": 'pmemsave 0xb8000 4000 "' + str(destination) + '"'})
            check(not result, "pmemsave failed: " + str(result))
            raw = destination.read_bytes()
            check(len(raw) == 4000, "short VGA memory capture")
            cells = tuple(struct.unpack("<2000H", raw))
            rows = tuple(cells[y*80:(y+1)*80] for y in range(25))
            text = tuple("".join(chr(c & 255) for c in row) for row in rows)
            # Restore selected register, without racing the guest's paired outb.
            old_index = self.io_command("inb 0x3d4")
            registers = []
            try:
                for index in (0x0A, 0x0E, 0x0F):
                    self.io_command("outb 0x3d4 " + hex(index))
                    registers.append(self.io_command("inb 0x3d5"))
            finally:
                self.io_command("outb 0x3d4 " + hex(old_index))
            shape, high, low = registers
            hidden, position = bool(shape & 0x20), high*256 + low
            (self.directory / "last-screen.txt").write_text(
                "\n".join(text) + f"\ncursor hidden={hidden} position={position}\n")
            return rows, text, hidden, position
        finally:
            self.call("cont")

    def serial(self):
        path = self.directory / "serial.log"
        return path.read_bytes() if path.exists() else b""

    def event(self, qcode, down):
        self.call("input-send-event", {"events": [{"type": "key", "data": {
            "down": down, "key": {"type": "qcode", "data": qcode}}}]})

    def key(self, qcode, modifiers=()):
        for modifier in modifiers:
            self.event(modifier, True)
        self.event(qcode, True)
        time.sleep(self.args.key_delay/2)
        self.event(qcode, False)
        for modifier in reversed(modifiers):
            self.event(modifier, False)
        time.sleep(self.args.key_delay/2)

    def type(self, text):
        codes = {" ": "spc", "/": "slash", "-": "minus", ".": "dot"}
        for character in text:
            if character.isascii() and character.isalnum():
                self.key(character.lower(), ("shift",) if character.isupper() else ())
            else:
                check(character in codes, "unsupported test character: " + repr(character))
                self.key(codes[character])

    def expect(self, expected, cursor=None):
        cursor = len(expected) if cursor is None else cursor
        deadline = time.monotonic() + 3
        last = None
        while time.monotonic() < deadline:
            last = self.screen()
            rows, text, hidden, position = last
            prompts = [y for y, row in enumerate(text) if row.startswith(PROMPT)]
            if prompts:
                y = prompts[-1]
                tail = "".join(text[y:])[len(PROMPT):].rstrip()
                if tail == expected and not hidden and position == y*80 + len(PROMPT) + cursor:
                    check(all((c >> 8) == 0x0A for c in rows[y][:len(PROMPT)]),
                          "prompt is not light green on black")
                    for index in range(len(expected)):
                        offset = len(PROMPT) + index
                        check((rows[y+offset//80][offset%80] >> 8) == 0x0F,
                              "input cell is not white on black")
                    return last
            time.sleep(.02)
        raise AssertionError(f"input/cursor mismatch: expected {expected!r} at {cursor}; "
                             f"cursor={last[3]} hidden={last[2]}; see last-screen.txt")

    def erase(self):
        self.key("u", ("ctrl",))
        self.expect("")

    def command(self, command):
        self.type(command)
        self.expect(command)
        offset = len(self.serial())
        self.key("ret")
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if self.serial()[offset:].endswith(PROMPT.encode()):
                return self.expect("")
            time.sleep(.01)
        raise AssertionError("command did not return to prompt: " + command)

    def close(self):
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        for stream in reversed(self.files):
            stream.close()
        for sock in self.sockets:
            sock.close()


def editing(c):
    c.command("clear")
    c.type("abcde"); c.key("left"); c.key("left"); c.expect("abcde", 3)
    c.key("delete"); c.expect("abce", 3)
    c.key("backspace"); c.expect("abe", 2)
    c.key("right"); c.type("x"); c.expect("abex")
    c.key("home"); c.type("z"); c.expect("zabex", 1)
    c.key("end"); c.key("backspace"); c.expect("zabe")
    c.key("home"); c.key("left"); c.key("backspace"); c.expect("zabe", 0)
    c.key("end"); c.key("right"); c.key("delete"); c.expect("zabe")
    c.key("left"); c.key("left"); c.erase()
    # Release alone and the browser escape chord must not edit guest input.
    c.event("x", False); c.key("esc", ("shift",)); c.expect("")
    for _ in range(15):
        c.command("warmup")
    c.type("x"*159); c.expect("x"*159)
    c.type("y"); c.expect("x"*159)  # 160th character refused.
    c.key("home"); c.key("delete"); c.expect("x"*158, 0)
    c.key("end"); c.key("backspace"); c.expect("x"*157)
    c.key("home"); c.key("right"); c.erase()
    offset = len(c.serial())
    c.command("about")
    check(c.serial()[offset:].count(b"tiny 32-bit research OS") == 1,
          "cleared wrapped input did not execute exactly one replacement command")
    print("PASS editing, boundaries, wrapped159, CRTC cursor, Ctrl+U, release/Shift+Esc", flush=True)


def history(c):
    for index in range(35):
        c.command(f"h{index:02d}")  # Unknown commands only, no side effects.
    c.type("draft"); c.key("left"); c.key("left"); c.expect("draft", 3)
    for index in range(34, 2, -1):
        c.key("up"); c.expect(f"h{index:02d}")
    c.key("up"); c.expect("h03")  # h00..h02 evicted, exactly 32 retained.
    for index in range(4, 35):
        c.key("down"); c.expect(f"h{index:02d}")
    c.key("down"); c.expect("draft", 3)
    c.key("down"); c.expect("draft", 3)
    c.type("x"); c.expect("draxft", 4); c.erase()
    print("PASS 32 history entries, eviction, draft and draft cursor restoration", flush=True)


def completion(c):
    c.type("abo"); c.key("tab"); c.expect("about"); c.erase()
    c.type("pro"); c.key("tab"); c.expect("proc")  # Longest common prefix.
    c.erase()
    c.type("pa"); c.key("tab"); c.expect("pa")
    text = "\n".join(c.screen()[1])
    check("paging" in text and "panic-test" in text, "ambiguous candidates absent")
    c.erase()
    c.type("help mem"); c.key("tab"); c.expect("help mem"); c.erase()
    c.type("about"); c.key("home"); c.key("tab"); c.expect("about", 0); c.erase()
    c.type("zznomatch"); c.key("tab"); c.expect("zznomatch"); c.erase()
    print("PASS unique/common/ambiguous completion, no argument or mid-token completion", flush=True)


def help_pages(c):
    titles = {"": "RavenOS commands", "system": "System and inspection",
              "memory": "Memory", "files": "RAM files", "disk": "Disk (",
              "net": "Prototype loopback network", "runtime": "Prototype runtime",
              "controls": "Console controls", "x"*100: "Unknown help topic."}
    for topic, title in titles.items():
        c.command("clear")
        offset = len(c.serial())
        result = c.command("help" + (" " + topic if topic else ""))
        serial = c.serial()[offset:].decode("ascii")
        check(title in serial, "help title missing from serial: " + topic)
        # Input echo can exceed a row; output and final prompt must fit bounds.
        output = serial.split("\n", 1)[1].replace("\r", "")
        lines = output.splitlines()
        check(len(lines) <= 23, f"help {topic}: output + prompt exceeds 23 lines")
        check(all(len(line) <= 79 for line in lines), f"help {topic}: line exceeds 79 columns")
        visible = result[1]
        prompt_row = max(y for y, row in enumerate(visible) if row.startswith(PROMPT))
        expected_rows = lines[:-1]
        check(lines[-1] == PROMPT, "help missing final prompt")
        check(prompt_row >= len(expected_rows), "help scrolled off its own output")
        rendered = [row.rstrip() for row in visible[prompt_row-len(expected_rows):prompt_row]]
        check(rendered == expected_rows, f"help {topic}: VGA output differs from serial")
        check(all(row[79] == " " for row in visible[prompt_row-len(expected_rows):prompt_row]),
              "help uses forbidden column 80")
    offset = len(c.serial())
    result = c.command("clear")
    check(sum(row.startswith(PROMPT) for row in result[1]) == 1, "clear duplicate VGA prompts")
    check(c.serial()[offset:].count(PROMPT.encode()) == 1, "clear duplicate serial prompts")
    check(result[1][0].startswith("RavenOS "), "clear did not home the screen")
    print("PASS help main/all topics/unknown <=79 columns, <=23 rows; clear once", flush=True)


def scrollback(c):
    c.command("clear")
    for _ in range(15):
        c.command("warmup")
    live = c.expect("")[0]
    check(c.screen()[3] == 24*80 + len(PROMPT), "fixture not at bottom row")
    old_rows = []
    for index in range(150):
        new_live = c.command(f"s{index:03d}")[0]
        check(live[2:24] == new_live[:22], "unknown command did not scroll two rows")
        old_rows.extend(live[:2])
        live = new_live
    retained = tuple(old_rows[-256:])
    check(len(old_rows) > 256, "test did not force scrollback eviction")
    check(any((cell >> 8) == 0x0A for row in retained for cell in row),
          "scrollback fixture lacks colored prompts")
    position = 0
    for _ in range(12):
        c.key("pgup", ("shift",)); position = min(256, position+24)
        rows, _, hidden, _ = c.screen()
        check(rows == (retained+live)[256-position:256-position+25],
              f"PgUp colored VRAM mismatch at {position} lines")
        check(hidden, "hardware cursor remains enabled in scrollback")
    for _ in range(12):
        c.key("pgdn", ("shift",)); position = max(0, position-24)
        rows, _, hidden, _ = c.screen()
        check(rows == (retained+live)[256-position:256-position+25],
              f"PgDn colored VRAM mismatch at {position} lines")
        check(hidden == bool(position), "CRTC cursor visibility does not follow view")
    c.expect("")
    c.key("pgup", ("shift",)); c.type("x"); c.expect("x"); c.erase()
    c.key("pgup", ("shift",)); c.key("left"); c.expect("")
    print("PASS 256 colored retained rows, eviction, PgUp/PgDn, CRTC hide/show, typing/edit live", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, default=ROOT / "build/ravenos.kernel")
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    parser.add_argument("--key-delay", type=float, default=.025)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    args.kernel = args.kernel.resolve()
    if not args.kernel.is_file():
        parser.error("kernel missing; build first: " + str(args.kernel))
    if not shutil.which(args.qemu):
        parser.error("QEMU executable not found: " + args.qemu)
    if args.key_delay < .01 or args.timeout <= 0:
        parser.error("key-delay must be >= .01 and timeout must be positive")
    directory = Path(tempfile.mkdtemp(prefix="raven-console-"))
    c = Console(args, directory)
    success = False
    def timeout_handler(signum, frame):
        if signum == signal.SIGALRM:
            raise TimeoutError("console integration exceeded overall timeout")
        raise InterruptedError("console integration interrupted by signal " + str(signum))
    signal.signal(signal.SIGALRM, timeout_handler)
    signal.signal(signal.SIGTERM, timeout_handler)
    signal.signal(signal.SIGINT, timeout_handler)
    signal.alarm(args.timeout)
    try:
        c.start()
        editing(c); history(c); completion(c); help_pages(c); scrollback(c)
        success = True
        print("PASS console QEMU integration (physical keys, VRAM, serial, CRTC; no drives)")
        return 0
    except Exception as error:
        print("FAIL console QEMU integration: " + str(error), file=sys.stderr)
        return 1
    finally:
        signal.alarm(0)
        c.close()
        if success and not args.keep:
            shutil.rmtree(directory)
        else:
            print("Console evidence: " + str(directory), file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
