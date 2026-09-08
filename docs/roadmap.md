# Runtime integration roadmap

Each milestone must build from the top-level Makefile, document its real limitations,
and pass its acceptance tests before the next subsystem is integrated. Existing
course sources stay available until a replacement covers their behavior. Do not
maintain parallel copies of a shell engine or ELF parser.

| Milestone | Shared integration | Acceptance gate |
| --- | --- | --- |
| 1. ELF foundation (implemented) | `elf/elf_parser.h` consumed by inspector and memory view | ELF32/64, malformed/truncated input, sections, symbols, translation and readelf comparison |
| 2. Static ELF32 loader | Consume parser metadata; add loader-specific policy | Known static i386 fixture executes; BSS bytes verified zero; invalid entry/interpreter/overlap/address collisions rejected; failed mappings cleaned up |
| 3. Shell engine | One parser/executor for CLI and later TCP | N-stage pipelines, `<`, `>`, `>>`, `&`, failed fork/exec/pipe/dup2/open, descriptor cleanup; PTY tests for Ctrl-C/Ctrl-Z, jobs/fg/bg; history `!!`/`!n`; no zombies |
| 4. Tracer | Launch programs from the CLI; inspect their execution state | Registers and RIP/EIP; stepping, continuation, signals, exit; attach/detach where ptrace policy permits; deterministic tracee fixtures |
| 5. Allocator | Reusable aligned allocator with explicit ownership | Free-list splitting/coalescing, metadata integrity, randomized allocation tests, mutex-protected concurrent workload, fragmentation statistics and benchmark |
| 6. TCP execution | Reuse shell executor and output-pipe handling | Loopback by default; bounded length framing; partial reads/writes, multiple clients, stderr/stdout capture, disconnects, child cleanup and error tests |

## Loader design contract

- The inspector accepts more ELF types than the loader will. Loading starts with
  ELF32/EM_386/ET_EXEC static binaries and an explicitly documented startup ABI.
- A 64-bit process cannot jump into an arbitrary i386 executable using a C call.
  Build and test the execution path in a supported 32-bit environment.
- Resolve absent course startup support with a documented stack/entry trampoline.
  Define argc, argv, environment, auxiliary vector and stack-alignment limitations.
- Only PT_LOAD describes loadable memory. Check page congruence and integer
  overflow, and reserve the complete address range before replacing owned pages.
  MAP_FIXED must never silently replace the loader's own or unrelated mappings.
- Use private mappings; separate file bytes from zero-fill and final permissions.
  Handle shared pages, zero-size segments, partial final pages and mapping failures.
- Validate the entry lies in an executable LOAD segment. Reject dynamic interpreter
  and relocation requirements outside the supported contract.
- Supply a reproducible executable fixture that verifies BSS and exits with a known
  status, with mapping diagnostics and cleanup checks on failure paths.

## Shell and concurrency contract

Use a reusable parser AST and execution context, with job state owned by the shell.
Create each pipeline in its own process group. Coordinate terminal ownership,
SIGCHLD reaping, stopped jobs and continuation without signal-handler-unsafe work.
Run PTY-based tests rather than assuming piped stdin exercises terminal semantics.

Introduce threads where useful (allocator stress or a bounded server work queue),
with mutexes/condition variables protecting concrete shared state. Avoid forking a
multithreaded process into non-async-signal-safe child code. Networking architecture
must choose a process model or a safe spawn boundary deliberately.

## Assembly migration

Preserve the NASM encoder behavior. Introduce CDECL wrappers for raw i386 open,
read, write, close and exit with preserved callee-saved registers. Explain EAX as
syscall number/result and EBX/ECX/EDX/ESI/EDI/EBP as argument registers, including
negative errno returns. Add ABI and partial-I/O/error-path tests before replacing
the historical source.

## Networking scope

Use only local development listeners by default. Commands run with the developer's
permissions; this is not a sandbox or an authenticated service. No stealth,
persistence or access-control bypass. Set explicit command/output limits and test
backpressure, clients that vanish mid-frame and commands that outlive a client.
