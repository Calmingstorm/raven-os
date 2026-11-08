#!/usr/bin/env python3
"""Doom in QEMU: the real engine on the Freedoom data disk, from the shell and back.

The data disk is the primary IDE slave (a snapshot, so QEMU never writes the file) beside a small blank master,
the layout the browser page uses. The keys go through QEMU's PS/2 keyboard and RavenOS's driver, as a visitor's
do. Scenarios:
  play       Doom's startup log reaches the console, graphics start on the first frame, the menu and a new game
             answer keys, F10 then y quits, and the text console comes back pixel for pixel. A second launch is
             refused (one engine start per boot) and neither disk changes.
  no-disk    without the data disk, doom refuses and the shell carries on.
  cirrus     on a display without Bochs VBE the session ends with an error, before graphics, and the shell
             carries on.
Build first (tools/build.sh, tools/build-wad.sh). Evidence (screendumps, serial) stays under /tmp/raven-doom-*
on failure.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True  # importing the gfx test's helpers must not leave __pycache__ in tools/
spec = importlib.util.spec_from_file_location("gfx_qemu_test", ROOT / "tools/gfx-qemu-test.py")
gfx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gfx)
check, Machine = gfx.check, gfx.Machine
DISK = ROOT / "build/doom/freedoom1.img"
SCRATCH_BYTES = 1024 * 1024


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def drives(directory, with_disk=True):
    scratch = directory / "scratch.img"
    scratch.write_bytes(bytes(SCRATCH_BYTES))
    extra = ["-drive", f"file={scratch},format=raw,if=ide,index=0"]
    if with_disk:
        extra += ["-drive", f"file={DISK},format=raw,if=ide,index=1,snapshot=on"]
    return scratch, extra


def colours(shot):
    return len({pixel for row in shot[2] for pixel in row})


def play(qemu, kernel, directory):
    scratch, extra = drives(directory)
    scratch_before, disk_before = sha256(scratch), sha256(DISK)
    m = Machine(qemu, kernel, "std", directory, name="play", extra=extra)
    try:
        m.type("clear\n")
        m.wait_for_text("Type help or help controls")
        time.sleep(0.5)
        cleared_before = m.screen("cleared-before")

        started = time.monotonic()
        m.type("doom\n")
        title = m.wait_for_size((640, 400), "first-frame", 60)
        startup = time.monotonic() - started
        time.sleep(2)
        title = m.screen("title")
        check(colours(title) >= 64, f"the title frame has only {colours(title)} colours")

        m.key("esc")                      # the menu opens over the demo
        time.sleep(1)
        menu = m.screen("menu")
        check(colours(menu) >= 64, "the menu frame isn't Doom's")
        for _ in range(3):                # New Game, episode 1, default skill
            m.key("ret")
            time.sleep(0.8)
        time.sleep(2)
        m.event("up", True)               # walk
        time.sleep(1)
        m.event("up", False)
        time.sleep(1.5)
        m.screen("e1m1")

        m.key("f10")                      # Doom's quit prompt only opens if F10 reaches the engine
        time.sleep(1)
        m.screen("quit-prompt")
        m.key("y")
        m.wait_for_size((720, 400), "text-after", 30)
        m.wait_for_text("Doom session ended: exit (status=0).")
        console = "".join(m.text())
        check("I_InitGraphics: Auto-scaling factor: 2" in console, "Doom's startup log isn't in the console")

        m.type("doom\n")
        m.wait_for_text("Doom already initialized")
        check(m.screen("refused")[:2] == (720, 400), "a refused second launch left text mode")

        m.type("clear\n")
        m.wait_for_text("Type help or help controls")
        time.sleep(0.5)
        cleared_after = m.screen("cleared-after")
        differing = sum(1 for y in range(400) for x in range(720) if cleared_after[2][y][x] != cleared_before[2][y][x])
        check(differing == 0, f"{differing} pixels of the cleared console differ after Doom: text mode not restored")
    finally:
        m.close()
    check(sha256(scratch) == scratch_before, "the blank master changed")
    check(sha256(DISK) == disk_before, "the data disk file changed")
    return (f"play: graphics {startup:.1f} s after doom, title and menu frames, new game, F10 and y quit (exit 0), "
            "startup log kept, second launch refused, console pixel-identical, both disks unchanged")


def no_disk(qemu, kernel, directory):
    _, extra = drives(directory, with_disk=False)
    m = Machine(qemu, kernel, "std", directory, name="no-disk", extra=extra)
    try:
        m.type("doom\n")
        m.wait_for_text("Doom unavailable")
        refusal = next(row for row in m.text() if "Doom unavailable" in row)
        check(m.screen("no-disk")[:2] == (720, 400), "doom left text mode without a data disk")
        m.type("mem\n")
        m.wait_for_text("physical memory manager")
        return f"no-disk: \"{refusal.strip()}\", shell still works"
    finally:
        m.close()


def cirrus(qemu, kernel, directory):
    _, extra = drives(directory)
    m = Machine(qemu, kernel, "cirrus", directory, name="cirrus-doom", extra=extra)
    try:
        # Doom must give the same reason the gfx command refuses with, on screen, without leaving text mode.
        m.type("gfx\n")
        m.wait_for_text("staying in text mode")
        row = next(row for row in m.text() if "staying in text mode" in row)
        why = row.split("gfx: ", 1)[1].split("; staying in text mode")[0]
        m.type("doom\n")
        m.wait_for_text(f"Doom session ended: session error: {why}", 60)
        check(m.screen("cirrus-after")[:2] == (720, 400), "a refused Doom left text mode")
        m.type("mem\n")
        m.wait_for_text("physical memory manager")
        return f"cirrus: the session ended before graphics with \"{why}\" on screen; shell still works"
    finally:
        m.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--qemu", default=shutil.which("qemu-system-i386") or "qemu-system-i386")
    parser.add_argument("--kernel", default=str(ROOT / "build/ravenos.kernel"))
    parser.add_argument("--keep", action="store_true", help="keep the evidence directory even on success")
    args = parser.parse_args()
    check(DISK.exists(), f"no {DISK}: run tools/build-wad.sh first")
    directory = Path(tempfile.mkdtemp(prefix="raven-doom-"))
    results = []
    try:
        for scenario in (play, no_disk, cirrus):
            results.append(scenario(args.qemu, Path(args.kernel), directory))
    except Exception:
        print(f"doom-qemu-test: evidence kept in {directory}")
        raise
    if args.keep:
        print(f"doom-qemu-test: evidence in {directory}")
    else:
        shutil.rmtree(directory)
    print("doom-qemu-test ok\n  " + "\n  ".join(results))


if __name__ == "__main__":
    main()
