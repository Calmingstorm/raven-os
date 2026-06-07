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
