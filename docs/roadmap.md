# RavenOS roadmap

RavenOS is being built in layers. The target is a small but genuinely runnable personal OS, not a skin over Linux and not a permanent boot-screen toy.

## Milestone 0: bootable inspectable kernel

Status: in progress, usable in QEMU.

- [x] Multiboot x86 boot
- [x] Freestanding C kernel
- [x] VGA console
- [x] IDT/PIC
- [x] PIT timer
- [x] PS/2 keyboard
- [x] Interactive command palette
- [x] Wyrd event timeline
- [x] Capability table scaffold
- [x] Object namespace scaffold
- [x] CI build and smoke test

## Milestone 1: memory and allocator

- [ ] Parse Multiboot memory map
- [ ] Physical frame allocator
- [ ] Paging
- [ ] Kernel heap
- [ ] Guard pages for kernel stacks
- [ ] Memory stats command

## Milestone 2: tasks and isolation

- [ ] Kernel task abstraction
- [ ] Cooperative scheduler
- [ ] Preemptive scheduler
- [ ] Ring 3 transition
- [ ] Syscall path
- [ ] Per-task capabilities

## Milestone 3: storage and persistence

- [ ] Initrd format
- [ ] RAM object store
- [ ] Persistent wyrd log format
- [ ] ATA PIO driver
- [ ] Simple filesystem

## Milestone 4: usable system image

- [ ] Userspace init
- [ ] Text editor or note pad
- [ ] Config store
- [ ] Installer/update workflow
- [ ] Crash report export

## Milestone 5: networking

- [ ] NIC target selection, likely e1000 first
- [ ] Ethernet frame support
- [ ] ARP
- [ ] IPv4
- [ ] UDP
- [ ] Tiny HTTP diagnostics endpoint
