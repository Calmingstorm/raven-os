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
