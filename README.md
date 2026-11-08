# RavenOS

RavenOS is a tiny x86 research operating system built around one principle: the machine should explain itself.

The kernel is **wyrd-log native**. Boot decisions, driver initialization, command execution, capability checks, and failures flow into a structured in-kernel timeline instead of disappearing into folklore and regret.

This is not a Linux clone and not a POSIX cosplay project. The long-term goal is a viable, inspectable personal OS with a capability-first security model, a live object namespace, and an operator-facing interface that treats accountability as a primitive.

## Current state

Version: `0.11.0`

Implemented:

- Multiboot-compatible 32-bit x86 boot path
- Freestanding C kernel
- VGA text console with scrolling
- IDT and PIC setup
- PIT timer on IRQ0
- PS/2 keyboard input on IRQ1
- Interactive command palette
- Wyrd event ring buffer
- Capability table scaffold
- Kernel object namespace scaffold
- Multiboot memory-map inspection
- Physical frame allocator
- First-stage identity paging
- Tiny kernel heap
- RAM filesystem plus procfs-style diagnostic pseudo-files
- Volatile sector storage scaffold
- Cooperative task table scaffold
- Userspace ABI dispatcher scaffold and demo programs
- Loopback network scaffold
- Installer dry-run planner
- Runtime environment table
- Structured diagnostic log
- Package/component manifest
- Security policy scaffold
- Service supervisor table
- Metrics/health reports
- Console/session table
- Snapshot/checkpoint scaffold
- Editable RAM filesystem commands with path arguments
- Tiny RavenFS block-image file table with save/load/format commands
- Installer run path that writes a base system into RavenFS
- Loopback socket table and payload send command
- Package install flags
- Named supervised demo programs
- Task kill/state transitions
- GDT/TSS setup with kernel and user segment descriptors
- DPL3 `int 0x80` syscall gate wired into the ABI dispatcher
- ATA primary-master identify probe
- Round-robin timer IRQ task rotation accounting
- RVNEXE executable registry/loader scaffold
- ATA PIO sector read/write path for QEMU IDE dev disks
- Raw dev disk image tooling and QEMU disk smoke validation
- Process table with address-space metadata
- COM1 serial console output and input
- Automated serial shell command validation against QEMU
- Serial operator command: `serial`
- Proc/process operator commands: `procps` and `ps-spawn`
- Safe simulated power controls
- Bootable ISO generation
- QEMU smoke-test tooling

Interactive commands include:

```text
help serial timeline log caps objects boot mem paging heap fs cat edit rm write
disk ata ata-read ata-write save load format net ping sock send env sethost pkgs pkg-install
health metrics proc services restart security clock consoles switchtty
manifest snapshot power reboot shutdown user exec load run abi rings procps ps-spawn sys install install-run
alloc tasks spawn kill stats clear about panic-test
```

## Build

Dependencies on a Debian/Ubuntu-like host:

```bash
sudo apt install build-essential gcc-multilib binutils grub-pc-bin xorriso mtools qemu-system-x86
```

Build the ISO:

```bash
./tools/build.sh
```

Run in QEMU:

```bash
./tools/run-qemu.sh
```

Run validation:

```bash
./tools/check.sh
```

Artifacts:

```text
build/ravenos.kernel
build/ravenos.iso
```

## In the browser

https://ravenos.calmingstorm.net runs the kernel in [v86](https://github.com/copy/v86), an x86 emulator compiled
to WebAssembly. v86 boots `build/ravenos.kernel` directly as a Multiboot image, without GRUB or a disk image.

```bash
./tools/build-web.sh          # web/dist: page, emulator, BIOS files, kernel, pre-gzipped copies
node tools/check-web.mjs      # boots web/dist in headless Chrome at desktop and phone widths
./tools/deploy-web.sh         # publishes a release to server-2 (needs root SSH to 192.168.1.41)
```

`tools/check-web.mjs` takes `--url <site>` to check a deployed copy, and `--screenshots <dir>` to save screenshots.
Hosting: `deploy/nginx-ravenos.conf` (server-2) and `deploy/apache-ravenos.calmingstorm.net.conf` (.13, TLS and
proxy).

## Licenses

- **v86** (BSD-2-Clause, `web/licenses/v86-LICENSE.txt`) is installed from npm at build time, pinned in
  `web/package.json`.
- **SeaBIOS** (`web/vendor/bios/*.bin`) is LGPL-3.0. Its corresponding source, the configuration and the build steps are
  in `web/vendor/bios/source`; the site serves them at `/source/bios/`.
- The first commit (`cdc80a9`) included build output by mistake, among it a GRUB rescue ISO (`build/ravenos.iso`); the
  next commit removed it. GRUB is GPL-3.0-or-later, and its source is at https://www.gnu.org/software/grub/.

## Design direction

Near-term viability milestones:

1. Memory map ingestion and physical memory manager
2. Paging and kernel heap
3. Cooperative task scheduler
4. Init task model
5. RAM-backed filesystem/object store
6. ATA or virtio disk support
7. Userspace ABI
8. Persistent wyrd log
9. Network driver path
10. Installer image

RavenOS should become something a person can boot and use for a focused set of tasks, not merely a screenshot with delusions. The gods have seen enough of those.


## Honesty clause

RavenOS is not daily-driver ready yet. See `docs/daily-driver-gap.md` for the exact blockers: ring-3 isolation, durable storage drivers, preemptive scheduling, real networking, install persistence, and real userspace binaries. Version 0.11.0 adds serial console I/O and automated interactive shell validation, but still lacks proven ring-3 execution and an installed boot target, but Odin is not putting lipstick on a missing disk driver.


## Current milestone: 0.12.0

RavenOS now has ATA-backed RavenFS persistence validated across QEMU reboot, serial-driven integration tests, and tiny RVNEXE script manifests that can be saved, loaded, and run. It remains a research OS, not a daily driver: ring-3 isolation, CR3 process switching, real networking, and installed-disk boot are still hard blockers.


Current version: 0.13.0

### USB boot image

Build an image suitable for writing directly to a USB stick:

```sh
./tools/build-usb-iso.sh
./tools/usb-iso-qemu-test.sh
```

The artifact is `build/ravenos-usb.iso`; checksum is written to `build/ravenos-usb.iso.sha256`. See `docs/usb-boot.md` before writing it to real hardware.
