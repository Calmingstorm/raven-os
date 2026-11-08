# Isolated production-linked paging guests

Run `bash tools/round2-paging-qemu-test.sh`. Set `ROUND2_KEEP=1` to retain its
private `/tmp/raven-round2-paging-qemu.*` directory, ELF hashes and serial logs.
Every runner builds into `mktemp`, never the shared `build/` tree. There is no
writable disk, NIC, full kernel main, production service or desktop session.

Optional v86 uses existing assets only:

```
node tools/round2-paging-v86-test.mjs --assets /path/to/assets --keep
```

Assets contain `node_modules/v86/build/{libv86.mjs,v86.wasm}` plus
`bios/{seabios.bin,vgabios.bin}`. Flat or `build/` layouts also work. Supply
`--puppeteer /path/to/puppeteer-core` if it is not resolvable from the assets,
or `--playwright /path/to/playwright-core`. Automatic runtime selection prefers
existing `web/node_modules/playwright-core`, falling back to puppeteer-core.
Default asset lookup tries `web/dist`, `web`, then `build/console-v86`.
Supply `--chrome /path/to/chrome` if needed. Each guest runs in a new headless
page inside a disposable browser profile, not an existing graphical session.
`--keep` retains kernels, serial logs and `report.json` under a private
`/tmp/raven-round2-paging-v86-*` directory. No downloads or installs occur.

The builder links real `boot/boot.s`, `kernel/isr.s`, `kernel/gdt.c`,
`kernel/serial.c`, `kernel/bootinfo.c`, `kernel/pmm.c`, `kernel/paging.c`,
`kernel/idt.c`, and the production linker script with the dedicated guest main.
Stubs cover VGA output, timeline observation, inactive IRQs and syscall dispatch.
The guest reads CR3 and real PDEs/PTEs, writes high identity and heap addresses,
and uses actual CPU exception delivery, not a mocked exception-handler call.

| Mode | Coverage |
| --- | --- |
| 0 | Every PTE through 128MiB, unusable and unaligned holes, VGA, high RAM writes, heap pages, MMIO offset/cache attributes/containment/conflict/overflow/reuse |
| 1 | Supervisor write to reserved hole 0x00800000, fatal PF with CR2/EIP/raw error 2 and decoded cause |
| 2 | QEMU Pentium III PSE reserved-bit fault at 0xD0000000, RSVD/write bits and decoded cause |
| 3 | Real UD2 before paging, fatal #UD and no exception storm |
| 4 | Drain physical frames, fail heap transactionally, return frames, retry; successful PMM allocations/frees quiet |
| 5 | Maximum 16MiB heap, unique physical backing, first/last word writes per page, one-success rule |
| 6 | Entire 64MiB MMIO window, every PTE address/cache bits, hard end, failed over-capacity request and retained reuse |

v86 runs modes 0,1,3,4,5,6. Reserved-bit CPU behavior is checked only on QEMU,
not assumed from v86 emulation. QEMU TCG's Pentium III reports raw error 0xA
for the reserved PSE bit21 case, so the validator requires RSVD/write/supervisor
and verifies the decoded P label against the actual raw P bit. It does not
invent a protection bit that the emulator did not deliver.

Fatal guests must print exactly one fatal record and the halt marker, then
remain stopped. v86 also samples actual HLT, IF, EIP, CR2, instruction counter
and reboot state twice to prove stable CPU halt rather than quiet output alone.
The QEMU 10-second timeout is expected for these guests. Normal
QEMU guests exit 33 through isa-debug-exit. v86 emits literal `HALT` from its
Multiboot debug-exit port; the runner accepts that sentinel only for normal
guests after the complete PASS and `ROUND2 EXIT port=0x00F4 value=0x10` markers,
not as proof of success by itself.

The raw heap paging API requires page-aligned byte lengths and does not promise
zero fill. The guest tests that contract, not heap-layer allocation semantics.
MMIO tests inspect hardware mappings, but do not claim actual PCI device access,
device ordering, BAR ownership, PAT/MTRR configuration or physical hardware
compatibility. The synthetic map is entirely backed by emulator 128MiB RAM.
