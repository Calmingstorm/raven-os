# Round 2: memory and owned timeline snapshots

This change supplies the memory half only. PCI/BGA, graphics mode, Doom, libc,
WAD storage, sound, process isolation and deployment are separate work.

## Address spaces and boot order

The IDT and exception gates are installed immediately after the GDT, before
paging or heap initialization. Fatal exceptions write directly to COM1 without
allocation, a timeline append, or graphics access, then halt with interrupts
disabled. Page faults include CR2, EIP, the raw error code and its decoded
present/protection, read/write, supervisor/user, reserved-bit and instruction
fetch flags. A broken kernel stack or a fault inside the exception handler can
still double/triple fault. This is not a separate emergency stack or crash dump.
Error-code stubs also account for optional newer #CP/#VC/#SX exceptions; those
CPU features are not stimulated by the QEMU/v86 target suites.

The RAM identity map uses the captured validated Multiboot regions through the
existing 128 MiB PMM cap. Full usable pages are mapped, reserved overlaps win,
and holes are not optimistically filled. Required kernel image, BSS, stack,
page tables and VGA text memory remain mapped. Boot metadata has already been
copied before paging; it need not remain dereferenceable. Frame-allocation
successes no longer emit one event per page and erase the boot timeline.

The heap arena occupies `0xC0000000..0xC1000000`; MMIO has a separate
`0xE0000000..0xE4000000` window. Statically reserved paging structures have
bounded cost and are included in the kernel reservation. Kernel RAM mappings
remain supervisor read/write, not W^X or per-process isolation.

## MMIO driver contract

`paging_map_mmio(uint32_t phys_addr, size_t size)` returns a `volatile void *`
or NULL. The caller validates that a memory BAR is supported, lies within the
32-bit physical address space, and has sufficient aperture for the byte span.
Never truncate a 64-bit BAR or cast the physical address to a usable pointer.

- Byte-granular ranges are rounded outward to 4 KiB pages. The returned pointer
  preserves the original physical page offset.
- Mappings are supervisor-only, present, writable, PCD=1, PWT=1, PAT=0. No PAT,
  MTRR or write-combining configuration changes are made. The supported default
  cache configuration is retained. Use volatile accesses of the device's width;
  volatile is not a general device-ordering or synchronization primitive.
- Ordinary RAM and existing direct-map overlaps are rejected to avoid a cached
  alias. Validation is conservative, including reported RAM beyond the PMM cap.
- Zero length, ranges extending beyond 4 GiB, conflicts, uninitialized paging,
  or exhausted window/descriptor resources fail without partial mappings or
  consumed resources. The last physical page below 4 GiB is supported.
- Mappings live until reboot. Requests wholly covered by an existing
  page-rounded mapping reuse it. Other partial physical overlaps fail. There
  is no unmap API in this round. There are 64 mapping descriptors and 64 MiB of
  virtual window; repeated graphics entry must reuse the existing mapping.
- Main-loop/kernel context only, never IRQ or exception context. Use the
  returned virtual address exclusively.

## Heap contract

The initial target is a 16 MiB contiguous **virtual** arena backed by independent
physical frames. `paging_alloc_heap` acquires frames transactionally and returns
them all on failure. `heap_init` can retry smaller arenas down to one page under
low memory; its initialization event and `heap` report expose the actual size.
A normal 128 MiB machine receives 16 MiB. This is a fixed-capacity arena, not
demand paging, heap growth or a general-purpose VM allocator.

Payloads are 16-byte aligned. In-band bounded metadata supports splitting and
coalescing adjacent free blocks. `heap` reports aggregates rather than a line
per allocation. Allocation is main-loop only, not interrupt-safe.

- `kmalloc(0)` and a zero-sized `kcalloc` return NULL.
- `kcalloc` checks multiplication overflow and zeroes the requested bytes.
- `kfree(NULL)` does nothing. Invalid/interior/already-free pointers do not
  corrupt allocator metadata. This does not make a stale pointer safe if its
  address has subsequently been reused by another allocation.
- `krealloc(NULL,n)` is allocation. `krealloc(p,0)` frees and returns NULL.
  Failure leaves the original allocation and contents intact. Success preserves
  the lesser of the old requested size and new requested size.
- Size rounding, metadata additions and multiplication are overflow checked.
- Legacy `kfree_all` invalidates every outstanding pointer and resets the arena.
  It is not a reclamation API suitable for independently owned live objects.

## Wyrd snapshots

All five strings are stored inside each event, independent of the heap or the
caller. Capacities include the terminating NUL: actor 64, verb 32, object 160,
result 32, note 192 bytes. The object holds the full supported 159-byte shell
command. Longer fields keep a NUL-terminated prefix and set a per-field bit in
`truncated`. Rendering appends an explicit marker such as
` [truncated:actor,object]` rather than silently presenting the prefix as complete.
NULL inputs become empty fields.

The 256-event ring uses a bounded write index and retained count, so wraparound
does not depend on an ever-growing insertion counter. Logical event ticks still
wrap modulo 32 bits. `wyrd_get` copies out an oldest-retained indexed snapshot;
it does not expose a pointer that a future append can mutate. These are
main-loop APIs, not a concurrent/IRQ-safe audit transport. The log remains
volatile and bounded, not a durable audit journal.

## Validation

`tools/check.sh` includes native sanitizer tests for heap and owned timeline
semantics and disposable QEMU integration/fault guests. `tools/round2-v86-test.sh`
runs the disposable memory/fault guests in headless Chrome against the pinned
v86 version, using temporary browser state and no writable disks. The web CI
job runs it alongside the existing web application checks. The console v86
regression suite additionally checks actual shell commands retain their distinct
timeline objects after the input buffer is reused.

`tools/round2-paging-host-test.sh` and `tools/round2-paging-qemu-test.sh` cover
exhaustive identity-map membership, fragmented backing frames, physical OOM
rollback, descriptor/window exhaustion and malformed boot metadata. The optional
`tools/round2-paging-v86-test.mjs` runs the matching emulator-supported scenarios
in web CI. Reserved-bit hardware fault injection is asserted in QEMU only;
v86's implementation is not used as evidence for that CPU feature. Both v86
runners accept the existing web Playwright dependency. Native/QEMU tooling
requires Node.js as well as the compiler, GRUB, Python and QEMU dependencies.

Tests fill and verify every byte of a 6 MiB allocation with additional headroom,
exercise fragmentation/coalescing, calloc/realloc/overflow/OOM recovery,
inspect real guest page tables for excluded holes and MMIO cache flags, and
inject fatal page faults only in disposable kernels. Serial assertions verify
memory results and fatal diagnostics, not framebuffer graphics. Graphics
acceptance belongs to the separate graphics change.
