# Limitations and Scope

## Shell

The shell is intentionally not a full POSIX grammar. It supports pipelines, redirection, background execution, quotes, history, and core job-control built-ins, but not command substitution, glob expansion, shell variables, heredocs, logical `&&/||`, subshell syntax, or scripting language constructs.

## Loader

The user-space loader is designed to expose ELF/program-loading mechanics rather than replace the kernel's `execve` implementation.

- Dynamic executables with `PT_INTERP` are rejected.
- Default fixed-address mapping uses `MAP_FIXED_NOREPLACE` to avoid silently destroying the loader's own mappings.
- `--force-fixed` switches to `MAP_FIXED` for purpose-built educational binaries whose virtual addresses are known to be safe.
- General process startup requires stack construction, auxiliary vectors, TLS, relocations, and/or an interpreter; the optional direct entry jump is therefore intended for freestanding static binaries.
- The portable build inspects and maps ELF32/ELF64. Direct ELF32 execution requires a 32-bit process build.

## Tracer

The tracer is a small educational `ptrace` client. It does not implement breakpoints, DWARF source debugging, remote injection, stealth, persistence, or privilege escalation. Attaching to unrelated processes may be blocked by Linux ptrace policy.

## Networking

The TCP command server binds to `127.0.0.1` only. This intentionally prevents the demonstration from becoming a remotely exposed shell service. Commands are framed, output-capped, and executed through the project's own noninteractive pipeline engine.

## Allocator

The custom allocator demonstrates free lists, splitting, coalescing, mmap arenas, and synchronization. It does not currently return fully free arenas to the OS or implement advanced size classes, per-thread arenas, quarantine, hardened metadata, `realloc`, or `calloc`.
