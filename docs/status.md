# RavenOS status

RavenOS is now a bootable interactive research OS image, validated in QEMU.

## What works now

- BIOS/Multiboot boot via GRUB ISO
- 32-bit freestanding kernel
- VGA text console with scrolling
- Interrupt descriptor table
- PIC remap
- PIT timer IRQ0
- PS/2 keyboard IRQ1
- Interactive shell/command palette
- Wyrd event log
- Capability table scaffold
- Object namespace scaffold
- Multiboot boot info inspection
- Physical memory bitmap allocator
- Tiny kernel heap
- Cooperative task table scaffold
- CI build/smoke test

## What does not make it production-viable yet

It still lacks ring-3 userspace, disk persistence, filesystem, networking, and a real installer. It is not ready to replace a daily-driver OS. It is ready as a serious hobby OS foundation, with a running kernel and verifiable subsystems.

## Validation command

```bash
./tools/check.sh
```

Current validation covers:

- kernel compilation with freestanding flags
- Multiboot compliance
- ISO generation
- QEMU boot smoke test
- git diff whitespace checks

## Added in 0.3.0 work

- First-stage paging with identity map for the first 4 MiB
- RAM filesystem with read/write command proof
- Kernel-callable syscall ABI dispatcher scaffold
- Shell commands: paging, fs, cat, write, abi, sys

These close more of the viability gap, but the honest blockers remain: ring-3 userspace, durable disk filesystem, and hardware drivers beyond the QEMU-friendly basics.

## Added in 0.4.0 work

- Supervised userspace program table with demo ABI-backed programs
- Volatile 128-sector block storage device
- Storage read/write sector API
- Loopback network scaffold with packet counters
- Installer dry-run planner
- Shell commands: disk, net, ping, user, run, install

This is still not a daily-driver OS. The repo now contains working scaffolds for every major viability area: boot, interrupts, keyboard, shell, event log, memory, paging, heap, scheduler, filesystem, storage, userspace ABI, network, and installer. The remaining work is turning scaffolds into hardened subsystems: ring-3, durable disk FS, real NIC/disk drivers, and installable images.


## Added in 0.5.0 work

- Runtime environment table with `env` and `sethost` commands
- Structured diagnostic log with `log` command
- Package catalog with `pkgs` command
- Operator `health` report
- Safe simulated power-control state with `power`, `reboot`, and `shutdown`

## Added in 0.6.0 work

- Service supervisor table with `services` and `restart` commands
- Security policy scaffold with deny-by-default checks and `security` command
- Metrics text export via `metrics` command
- Procfs-style diagnostic pseudo-files via `proc` command

## Added in 0.7.0 work

- Multi-console/session table with `consoles` and `switchtty` commands
- Monotonic clock/uptime renderer with `clock` command
- Boot/component manifest with `manifest` command
- Volatile snapshot/checkpoint scaffold with `snapshot` command

## Current honest viability read

RavenOS now has boot, interrupts, keyboard, shell, memory allocation, paging, heap, RAM FS, storage scaffold, userspace ABI scaffold, scheduler table, network scaffold, installer plan, security policy, service supervision, health/metrics, and operator diagnostics. That is a credible hobby OS foundation. It is still not daily-driver viable until it has actual ring-3 process isolation, durable filesystem support, real storage/NIC drivers, an install target, and enough userspace to do useful work without pretending the kernel shell is a civilization.


## Added in 0.8.0 work

- Editable RAM filesystem commands with path arguments: `edit <path> <text>`, `cat <path>`, and `rm <path>`
- Tiny RavenFS block-image file table on the existing storage scaffold
- `save <path>`, `load <path>`, and `format` commands for the RavenFS path
- Installer `install-run` that writes boot/config/log placeholders into RavenFS
- Loopback socket table with `sock` and `send <socket-id> <payload>`
- Package install flags via `pkg-install <name>`
- Named supervised demo programs via `run hello`, `run clock`, `run writer`, and `run netdemo`
- Task lifecycle improvements including kill/state transitions
- Expanded health, metrics, procfs, and manifest reporting

This pushes RavenOS from static subsystem scaffolds toward a small usable operator environment: files can be edited, saved to a block-image filesystem, loaded back, packages can be marked installed, sockets can carry payloads, demo programs can be run by name, and the installer writes a coherent base layout. It is still blocked from daily-driver status by ring-3 isolation, durable disk drivers, preemptive scheduling, real networking, and real userspace binaries.


## Added in 0.9.0 work

- GDT and TSS setup with kernel/user code and data descriptors
- User-callable DPL3 `int 0x80` syscall gate wired into the existing ABI dispatcher
- Expanded syscall table with note-write and loopback-send calls
- ATA primary-master IDENTIFY probe and `ata` diagnostic command
- Round-robin timer IRQ task rotation accounting and switch metrics
- RVNEXE executable registry/loader scaffold with `/bin/*.rvn` manifests
- `exec`, `load <path>`, `run <name>`, and `rings` operator commands

This attacks several real daily-driver blockers directly: privilege architecture is now described by actual descriptors, syscalls can enter through an interrupt gate, storage has begun probing hardware instead of only pretending inside RAM, the scheduler tracks timer-driven task rotation, and executable loading has a first format/registry. The missing parts remain substantial: no proven ring-3 iret execution, no full context switch, no ATA sector read/write path, no network card driver, and no persistent installed boot yet.


## Added in 0.10.0 work

- ATA PIO sector read/write implementation for primary-master LBA28 disks
- `ata-read` and `ata-write` commands exercising LBA 20 in QEMU dev images
- Raw 64 MiB RavenOS dev disk image builder with a seeded ATA test sector
- QEMU disk run/smoke scripts and validation wired into `tools/check.sh`
- Process table with PID, entry point, page-directory metadata, and user-stack metadata
- `procps` and `ps-spawn` commands for process/address-space inspection

This closes more of the daily-driver gap than the previous pass: storage is no longer identify-only, and the kernel now has explicit process/address-space metadata instead of only task rows. Still missing: a safe proven iret into ring-3, real CR3 process switching, a persistent bootloader-installed disk image, and a NIC driver.


## Added in 0.11.0 work

- COM1 serial driver initialized during boot
- VGA output mirrored to serial for headless operation
- Shell input accepted from serial as well as PS/2 keyboard
- `serial` command for console diagnostics
- Automated QEMU serial command test that drives real shell commands and verifies output

This is an important daily-driver-adjacent step because the OS can now be operated and validated headlessly, not just watched through a VGA smoke test. It gives RavenOS a real automation surface for future installer, filesystem, process, and networking tests.
