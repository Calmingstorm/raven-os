# RavenOS daily-driver gap analysis

RavenOS is intentionally not branded as daily-driver ready yet. The current tree is a bootable, interactive research OS with real kernel subsystems and a coherent operating model, but a person cannot use it as their main machine until these are done:

## Hard blockers

1. **Ring-3 isolation**
   - Current userspace is supervised/in-kernel demo execution.
   - Required: GDT/TSS, user stacks, syscall trap path, fault isolation, process address spaces.

2. **Durable storage**
   - RavenFS currently sits on a volatile block image.
   - Required: ATA/virtio-blk driver, persistent partition layout, fsck/recovery path.

3. **Preemptive scheduler**
   - Current tasks are cooperative/table driven.
   - Required: context switch, sleeping/wake queues, process lifecycle, fair scheduling.

4. **Real networking**
   - Current network stack is loopback/socket scaffold.
   - Required: NIC driver, ARP/IP/ICMP/UDP/TCP or a deliberately smaller first network model.

5. **Userspace applications**
   - Current apps are built-in supervised demos.
   - Required: executable format, loader, editor, file tools, network client, package manager that installs actual binaries.

6. **Install target**
   - Current installer writes a base system into volatile RavenFS.
   - Required: disk install, bootloader integration, upgrade/rollback story.

## What 0.9.0 moved forward

- GDT/TSS descriptor setup exists.
- DPL3 int 0x80 gate is installed and routed into syscall dispatch.
- ATA IDENTIFY probe exists.
- Timer IRQ rotates task accounting.
- RVNEXE executable registry/loader scaffold exists.

## What 0.8.0 moved forward

- Editable RAM filesystem commands: `edit`, `cat <path>`, `rm`.
- RavenFS block-image file table with `save`, `load`, `format`.
- Installer `install-run` writes a base system into RavenFS.
- Socket table and payload send path on loopback.
- Package install flags.
- Named userspace demo programs.
- Task kill/state transitions.
- Better health/proc/metrics coverage.

## Daily-driver definition for this project

A minimal viable daily-driver RavenOS means:

- Boots from an installed disk image without the ISO.
- Persists files across reboot.
- Runs isolated user programs that can crash without killing the kernel.
- Has an editor, shell, file commands, diagnostics, and network transfer.
- Can update itself or install packages from local media/network.
- Produces a durable wyrd log explaining system state and failures.

Anything less is a strong hobby kernel, not a daily driver. Odin refuses to lie about that. The corpse of every abandoned hobby OS is made of optimism and missing disk drivers.
