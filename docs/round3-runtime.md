# Round 3 runtime integration contract

This PR supplies the freestanding runtime, not Doom itself. No engine sources,
playable game assets, linked Doom binary, deployment, or license decision are
part of it. The engine integration is pinned to doomgeneric
`e257a09527062bfb986b6e5e0780839b6cbd5b71` and remains a separate review.

## Headers and ownership

* `kernel/raven_wad.h`: lazy read-only WAD mount, bounded handles and positional
  reads. Logical size is 28,795,076 bytes; transport size is 28,835,840 bytes.
* `kernel/raven_ata.h`: quiet primary-channel PIO reads with an explicit drive.
  There is deliberately no slave write API. Existing `ata_read_sector` and
  `ata_write_sector`, RavenFS and shell write commands remain primary-master.
* `kernel/raven_clock.h`: millisecond units from the existing 100 Hz PIT,
  unsigned modulo arithmetic, HLT-based main-loop sleep. This is 10 ms
  resolution, not a promise of millisecond precision. A positive sleep with
  IF clear is a fatal programmer error; zero is a true no-op.
* `kernel/raven_doom_runtime.h`: boundary, adapter declarations and the
  session-owned allocation domain. The port supplies
  `raven_doom_port_main(int, char **)` and `raven_doom_port_cleanup(void)`.
* `libc/include/`: freestanding engine-only includes. Kernel compile flags and
  ordinary kernel allocation/output behavior are not replaced.

### Boundary lifecycle

`raven_doom_run` rejects null/invalid entry arguments, IF-clear calls, and nested
sessions before invoking any port code. It installs the live i386 setjmp frame
before entry and distinguishes normal return, explicit exit (including exact
zero or negative status), recoverable abort, unavailable and busy outcomes.
The shell is synchronously paused while the entry runs. CPU exceptions still
use the existing allocation-free serial fatal handler and halt, never longjmp.

Cleanup runs exactly once after entry return/exit/abort. The port must make it
safe after partial initialization, restore graphics, clear its input state,
and must not exit/abort/longjmp. Session memory and file streams remain live
during this callback. The runtime then closes libc streams, unmounts WAD,
reclaims only its linked list of session allocations, drains bounded stale
keyboard/serial input, and returns shell ownership. It never calls
`kfree_all`. Kernel allocations made separately with `kmalloc` survive.
`I_AtExit` callbacks belong to the port's `I_Quit`/`I_Error`, not this cleanup.

The real engine can initialize **once per boot**. The `doom` command refuses a
second initialization after any entry attempt, even if entry aborts. Missing
port, invalid WAD, insufficient 8 MiB heap preflight, or boundary refusal do not
consume that attempt. Repeated mock entries are supported and tested. Longjmp
and freeing allocations are not a reset of Doom's initialized global state.

Until a port is linked, `doom` reports that the engine is unavailable. These
are weak *references*, not success stubs. The port must explicitly disable
save/load and host-only paths and make ordinary `I_Quit` nonreturning.

### Allocation semantics

All libc malloc-family calls use the session domain and preserve 16-byte
payload alignment. Zero allocation returns NULL; free(NULL) is harmless;
realloc(p,0) frees. Overflow/OOM leaves existing allocations untouched.
Foreign/interior/stale pointers are rejected without dereferencing them;
as with ordinary C allocators, pointers cannot be distinguished once an
address is reused. The domain is main-loop only, including during cleanup.

## Disks and WAD

WAD mount never runs during ordinary boot or unknown-path opens. It explicitly
identifies primary slave, validates transport capacity, `IWAD`, directory
bounds and every lump's extent using checked arithmetic. Runtime mount does
not hash/read the whole 28.8 MB file: immutable URL, exact WAD/image SHA-256,
sector padding and source/license notices are the packaging half's gates.

The cache contains eight 512-byte sectors and handles are fixed at eight.
`read_at` separates error from EOF and reports partial transfer on I/O failure.
It accepts unaligned byte ranges, crosses sectors and hides the final 40,764
padding bytes. Unmount invalidates handles and the cache. Closing handles ends
their lifetime; do not retain their pointers after a slot is reopened.

The image is 56,320 sectors, exactly 110 chunks of 256 KiB. The browser must use
`fixed_chunk_size: 256 * 1024`. Rounding to only a 512-byte sector is not enough:
pinned v86 can request an entire final chunk beyond a shorter image's EOF and
fail on a short `206`. Capacity checks and fixtures require the chunk-aligned
transport size; file reads still expose only the logical WAD.

ATA polling has a finite iteration limit, checks BSY before error/DRQ, rejects
absent/ATAPI devices and out-of-range LBA28 accesses, and selects the intended
drive before considering status left by the other device. The limit bounds
polls, not elapsed wall time. Timeouts/errors are honest failures, with no
fallback to another disk. Master writes report flush/completion failure.
The quiet explicit-drive API does not flood Wyrd per sector.

RavenFS uses its existing RAM backend when the master cannot hold its exposed
128 sectors after base LBA 32 (160 sectors total). Thus a 64-sector browser
scratch hda is never formatted or overrun. Normal-sized RavenFS keeps its
existing mount/format behavior, **only on master**. v86 requires hda to exist
before attaching hdb; the web half must provide it deliberately.

## ABI and libc integration gates

The runtime assembly implements i386 SysV callee-saved EBX/ESI/EDI/EBP plus
resume ESP/EIP. longjmp value zero becomes one. No signal-mask, x87, SSE or IRQ
recovery API is implied. Engine/libc objects must use freestanding compiler
headers, not host glibc, and must not assume initialized floating-point state.
The libc block is separate from the kernel block in `tools/build.sh`.

There is no libm or fake `atof`. Pinned upstream's dormant float-configuration
machinery must be removed/disabled by the port rather than pulling in x87 or
unresolved soft-float helpers. The final selected engine objects must receive
their own actual undefined-symbol and floating-instruction audit before the
Doom PR is accepted. Runtime-only linking is not proof of engine linkability.

Diagnostics through libc stdout/stderr always go to serial, so they cannot
touch VGA registers or framebuffer memory while the port owns graphics.
General POSIX files/processes, audio, networking, savegames, persistent config,
floating formats and `%n` are outside this bounded libc surface. Unsupported
operations fail rather than pretending to succeed.

## Verification and limitations

`tools/check.sh` includes native sanitizer suites for libc, runtime and ATA/WAD
faults, exact i386 setjmp ABI tests, clock rollover/IRQ tests, and production-
linked QEMU fixture guests. CI's web job also runs the production runtime in
isolated headless v86 with a lazy ranged slave disk. Fixtures are synthetic
IWAD-shaped files, **not Freedoom and not playable**.

Guest cases cover normal return, exact exit status, abort, cleanup and unrelated
kernel allocation survival; actual PIT sleep; WAD unaligned/final-sector reads;
missing/corrupt/short disks; and small-master refusal to persist. QEMU checks
the complete master image's host SHA-256 before/after in addition to the
guest's full-device checksum. Native fault injection covers bounded ATA waits,
I/O/flush failures, malformed directories, cache invalidation and partial reads.
QEMU `ide-hd` rejects `readonly=on` with `Block node is read-only`; the QEMU
fixture therefore uses `snapshot=on` for the slave and verifies its raw backing
image's SHA-256 too. This protects the host image through a disposable overlay,
not hardware write protection. Port-level tests independently verify that no
slave WRITE/FLUSH command exists or is emitted.
Native command tests cover missing/partial port symbols, low-memory refusal,
preflight retry, exact argv and one-engine-attempt guard.

These tests do not establish rendered Doom gameplay, graphics restoration by
the future port, movement/fire release or upstream initialization robustness.
Those remain the engine half's acceptance gates. Public repository visibility
is not licensing approval; no linked Doom build is published by this work.
