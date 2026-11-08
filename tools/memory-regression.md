# Round-1 memory regression tests

Run from any directory:

* `bash tools/memory-regression-test.sh`: Linux host harness, production
  `bootinfo.c` and `pmm.c`, GCC undefined-behavior sanitizer, fixed low-address
  mappings for the Multiboot 32-bit physical-pointer ABI. No root required.
* `bash tools/memory-qemu-test.sh`: dedicated minimal freestanding guest,
  production memory files, bootstrap and linker script. GRUB ISO and all
  objects live in a unique temporary directory. QEMU runs at 32, 128 and
  256 MiB, with serial assertions and debug-exit. No disk or storage driver
  is attached. No repository build/iso outputs are touched.

Coverage includes mmap-only flags, basic-memory fallback, holes, overlapping
usable/reserved entries in either order, partial-page reservations, duplicate
usable entries, exhaustion and invalid/double frees, kernel/low-memory
reservations, boot modules/tables/strings, cached metadata lifetime, 128 MiB
cap, extended records, truncated/zero/undersized/oversized records, physical
pointer arithmetic overflow and 64-bit address/length overflow. Real GRUB
maps are also exercised after clearing the optional basic-memory flag.

Policy and limits:

* Mmap flag present means authoritative. Malformed, empty, over-budget or
  overflowing maps disable allocation; they never invoke a basic fallback.
* At most 256 mmap records, 64 modules, 512 total cached regions, and 4096
  bytes per boot string. Exceeding limits fails closed, never truncates.
* All intersecting reserved pages win over usable entries. Only full pages
  contained in an individual usable entry are eligible; adjacent fragmented
  usable entries are conservatively not coalesced to recover partial pages.
* `pmm_total_frames()` is the capped physical frame-index span, **not** a
  usable-frame count. `pmm_free_frames()` is the actual allocatable count.
* `bootinfo_mem_kib()` is the full usable-page union below 128 MiB, after
  firmware/boot metadata reservations, but before PMM low-memory/kernel
  reservations. It works without optional basic-memory fields.
* The parser assumes bootloader-provided pointers are readable physical RAM,
  as Multiboot requires. Arithmetic and structure validation cannot establish
  physical accessibility of arbitrary malicious pointers before a trusted
  map exists. No paging/heap changes or allocations above 128 MiB are tested
  or implemented here. Optional boot metadata not consumed by Raven is not
  retained, except the information structure itself.
