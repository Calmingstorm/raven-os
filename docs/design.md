# RavenOS design notes

## Thesis

RavenOS is an inspectable personal operating system. Its central primitive is the **wyrd log**, a structured event timeline emitted by the kernel and eventually by userspace services.

Every meaningful system transition should be explainable:

- who acted
- what verb occurred
- what object was touched
- whether it succeeded
- what note/context matters

Unix taught machines to expose files. RavenOS teaches machines to expose causality.

## Kernel primitives

### Wyrd timeline

A fixed-size in-kernel event ring exists today. Future versions should persist this to disk and expose query APIs.

Current event schema:

```c
tick, actor, verb, object, result, note
```

Future schema should include:

- monotonic timestamp
- task id
- capability id
- object id
- severity
- correlation id
- structured key/value payload

### Capability table

RavenOS should prefer explicit capability grants over ambient authority. The current table is static and illustrative. The future table should be object-backed and revocable.

### Object namespace

The object namespace is not intended to be a Unix filesystem clone. It is a live inventory of inspectable kernel and userspace objects.

Example paths:

```text
/dev/vga0
/dev/keyboard0
/sys/wyrd
/sys/caps
/sys/clock
/bin/shell
```

## Interface model

The command palette is the first shell. It should grow into an inspector rather than a POSIX imitation.

Good commands:

```text
timeline where actor=driver.keyboard
caps holder=shell
objects type=device
explain /dev/keyboard0
```

Bad defaults:

```text
pretending ls/cat/grep are the native model before the OS has objects worth inspecting
```

## Viability path

A viable personal OS needs, at minimum:

- reliable boot
- memory management
- scheduling
- isolation
- storage
- input/output
- update path
- diagnostics

RavenOS is at the reliable boot/input/diagnostics scaffold stage. Next major engineering work is memory management.
