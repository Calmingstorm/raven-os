# i386 setjmp/longjmp ABI regression tests

Run `bash tools/setjmp-test.sh` on a Linux host supporting i386 ELF execution.
Requires GCC and binutils, but no 32-bit libc or host setjmp implementation.
The production `libc/setjmp.s` is assembled and linked directly into every
test executable; Linux `int 0x80` performs only process exit.

- The assembly test checks the six exact saved fields against known values,
  including the caller ESP after return and the exact resume EIP label.
- On both initial and resumed returns it independently checks live EBX, ESI,
  EDI, EBP and ESP. A deeper frame overwrites every callee-saved register before
  longjmp, so a no-op restore cannot pass.
- It verifies initial return zero, `longjmp(..., 0)` becoming one, and exact
  positive, negative and `INT_MIN` values. The caller argument stack slot is
  checked only on initial return: it is legal for subsequent calls on the
  resumed stack to have overwritten that unused slot.
- A separate C test asserts every header offset and the 24-byte buffer size,
  tests nested live jump frames and compiler `returns_twice` semantics, and
  runs at `-O0`, `-O2` and `-O3`. Values of modified automatic locals that must
  survive longjmp are volatile; setjmp appears in valid controlling expressions.

This is the agreed i386 SysV integer-state ABI, not an x86_64, signal-mask,
FPU-state or exception-unwinding API. The runtime must remain in main-loop
context with interrupts enabled on both sides of the boundary. The assembly
does not modify IF, restore arbitrary flags, or attempt to repair bad callers.
