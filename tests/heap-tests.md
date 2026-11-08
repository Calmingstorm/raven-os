# Round 2 heap coverage

Run `./tools/heap-test.sh`. This compiles the real `kernel/heap.c` into a native
ASan/UBSan executable and separately compiles it with the production i386 ABI
and warnings. All build products live in a disposable `/tmp/raven-heap-host.*`
directory, not the shared kernel build directory.

The native paging stub gives the heap an mmap arena bracketed by inaccessible
guard pages. Each simulated low-memory boot also protects the entire unused
suffix. This checks heap access bounds, not PMM or page-table correctness.

Coverage includes:

- Every power-of-two fallback from 16 MiB to 4 KiB, complete failure, explicit
  degraded/emergency diagnostics, and repeated init preserving live data.
- Byte-by-byte write/verification of 6 MiB with simultaneous writable 2 MiB
  headroom, plus aggregate rendering and absence of allocation event spam.
- 16-byte payload alignment, minimum split/no-split boundaries, zero sizes,
  SIZE_MAX/UINT32_MAX rejection, calloc multiplication overflow and dirty-memory
  zeroing, maximum legal allocation and exact OOM.
- Adjacent coalescing in both directions, invalid/out-of-arena/interior/header
  pointers and double frees, legacy whole-arena reset.
- realloc NULL/zero, shrink, movement, in-place growth, exact neighbor absorption,
  oversized and fragmentation-induced failure preserving old data/metadata.
- Three complete exhaustion/recovery cycles and 20,000 deterministic randomized
  realloc/free operations, checking all live payload contents, block bounds,
  guards, exact arena accounting and no adjacent uncoalesced free blocks.

The independently owned `round2` integrated guest tests link the real paging,
PMM and heap and run under both QEMU and v86. They should verify the full 6 MiB
write plus 2 MiB headroom, forced OOM and failed realloc preservation, full
coalescing recovery, and PMM invariants across unsuccessful arena attempts.
Native paging stubs alone do not prove physical backing or rollback.

The allocator is single-context, not IRQ/reentrant safe. `kfree_all` invalidates
all existing pointers. As with ordinary allocators, a stale pointer whose address
has since been reallocated cannot be distinguished from the new live pointer.
