# RavenOS user guide

Boot the ISO in QEMU:

```bash
./tools/run-qemu.sh
```

Run with a raw IDE dev disk:

```bash
./tools/run-qemu-disk.sh
```

Run validation:

```bash
./tools/check.sh
```

Run the interactive serial shell validation directly:

```bash
./tools/serial-command-test.sh
```

Useful shell commands:

- `help` - command overview
- `serial` - serial console status
- `timeline` - recent wyrd events
- `health` - operator health report
- `fs` - list RAM files
- `edit /home/demo hello` - write a RAM file
- `cat /home/demo` - read a RAM or RavenFS file
- `save /home/demo` - persist RAM file into RavenFS block image
- `rm /home/demo` - remove RAM file
- `load /home/demo` - load RavenFS file back into RAM
- `disk` - show RavenFS block image
- `format` - reset RavenFS
- `install` - show install plan
- `install-run` - write base system to RavenFS
- `pkgs` - package catalog
- `pkg-install games` - mark a package installed
- `exec`, `load /bin/hello.rvn`, `run hello`, `run clock`, `run writer`, `run netdemo` - executable registry and supervised demo programs
- `rings` - inspect GDT/TSS/ring architecture
- `ata` - inspect ATA identify probe
- `ata-write`, `ata-read` - write/read LBA 20 on the QEMU dev disk
- `procps`, `ps-spawn` - inspect and create process metadata
- `net`, `sock`, `send 1 hello`, `ping` - loopback network/socket tools
- `tasks`, `spawn`, `kill <id>` - task table operations
- `metrics`, `proc`, `manifest`, `services`, `security` - diagnostics

RavenOS is not POSIX. The shell is an operator console over the wyrd log and kernel objects, not a Unix cosplay terminal. Good. The world has enough beige mistakes.
