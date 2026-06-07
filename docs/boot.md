# Booting RavenOS

## QEMU

```bash
./tools/run-qemu.sh
```

The booted kernel opens an interactive command palette. Type `help`.

Useful commands:

```text
timeline
caps
objects
stats
panic-test
```

## Real hardware warning

RavenOS currently targets QEMU/i386-class BIOS boot via Multiboot. It is not ready for real hardware installs yet. The viable path is:

1. Multiboot/QEMU correctness
2. Memory manager
3. Disk driver
4. Filesystem
5. Installer
6. Hardware compatibility matrix

Trying to install this on a real machine today is a fine way to create a very small shrine to hubris.
