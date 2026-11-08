# Round 2 paging tests

`./tools/round2-paging-host-test.sh` builds a disposable host executable with
UBSan and production `paging.c`/`pmm.c`. Only privileged CR0/CR3 operations are
replaced under `ROUND2_PAGING_HOST_TEST`. No host virtual mappings are installed.

It checks every identity-map PTE below the 128 MiB PMM cap, reserved holes,
page-rounded partial reservations, VGA attributes, absent-map fail-closed
allocation, independent physical frames for a 16 MiB virtual heap, staged-PTE
rollback on OOM, successful smaller retry at the same VA, rejected second heap
arena, and invalid heap inputs. Successful per-frame allocation/free operations
must not create timeline events; repeated OOM diagnostics are bounded.

MMIO checks include zero/oversized/overflowing requests, the final physical page
ending exactly at 4 GiB, byte-offset preservation, supervisor RW/PCD/PWT/PAT
attributes, page-rounded containment reuse, conflicting partial overlaps,
ordinary RAM below and above the direct-map cap, page-rounded RAM alias checks,
VGA aliases, full 64 MiB window exhaustion and 64-descriptor exhaustion. Failed
operations must preserve mapping entries, counters, VA capacity and PMM frames.

`./tools/round2-paging-qemu-test.sh` builds disposable production-linked guests.
`node tools/round2-paging-v86-test.mjs` runs those guests in a fresh headless
browser with v86. See each runner's options for asset and output paths. These
guests test actual hardware page tables, writable high identity/heap addresses,
and fatal exceptions. The host suite does not claim to validate CPU exception
delivery, actual device access, MTRRs or hardware PCI BAR resource ownership.

Static table geometry is 32 RAM tables (128 MiB), four heap tables (16 MiB at
`0xC0000000`), and 16 MMIO tables (64 MiB at `0xE0000000`), plus one directory.
This costs 212 KiB of kernel-reserved table memory before alignment/descriptors.
The heap supports one successful arena; MMIO mappings persist until reboot.
