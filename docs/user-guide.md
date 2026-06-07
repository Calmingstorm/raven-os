# RavenOS user guide

Boot the ISO in QEMU:

```bash
./tools/run-qemu.sh
```

Run validation:

```bash
./tools/check.sh
```

Useful shell commands:

- `help` - command overview
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
- `run hello`, `run clock`, `run writer`, `run netdemo` - supervised demo programs
- `net`, `sock`, `send 1 hello`, `ping` - loopback network/socket tools
- `tasks`, `spawn`, `kill <id>` - task table operations
- `metrics`, `proc`, `manifest`, `services`, `security` - diagnostics

RavenOS is not POSIX. The shell is an operator console over the wyrd log and kernel objects, not a Unix cosplay terminal. Good. The world has enough beige mistakes.
