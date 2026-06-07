# RavenOS

RavenOS is a tiny x86 research operating system built around one principle: the machine should explain itself.

The kernel is **wyrd-log native**. Boot decisions, driver initialization, command execution, capability checks, and failures flow into a structured in-kernel timeline instead of disappearing into folklore and regret.

This is not a Linux clone and not a POSIX cosplay project. The long-term goal is a viable, inspectable personal OS with a capability-first security model, a live object namespace, and an operator-facing interface that treats accountability as a primitive.

## Current state

Version: `0.4.0`

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
- Bootable ISO generation
- QEMU smoke-test tooling

Interactive commands:

```text
help
timeline
caps
objects
stats
clear
about
panic-test
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
