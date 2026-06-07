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
