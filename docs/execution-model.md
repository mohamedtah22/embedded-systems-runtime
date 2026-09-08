# Linux Execution Model — How the Projects Connect

This note summarizes the systems concepts exercised across the repository.

## From a shell command to a process

The shell parses user input and decides whether a command should run in the foreground or background, whether its standard input/output should be redirected, and whether it participates in a pipeline.

For external commands, the shell creates a process with `fork`. The child configures file descriptors and then calls `execvp`, replacing its current process image with the requested executable.

## File descriptors and pipelines

Unix represents open files, pipes, and standard streams using small integer file descriptors. A pipeline connects two processes by replacing one child's standard output with the pipe's write end and the next child's standard input with the read end.

This repository exercises those operations directly with `pipe`, `close`, `dup`/`dup2`, and `execvp`.

## System calls

At a lower layer, operations such as reading, writing, opening files, and terminating a process ultimately enter the kernel through system calls. The NASM component demonstrates the 32-bit x86 Linux interface using `int 0x80`, syscall numbers in `EAX`, and arguments in registers.

## Executable loading

`exec` depends on the OS understanding the executable file format. The ELF loader project examines that next layer directly: it reads the ELF header and program headers, identifies loadable segments, derives memory permissions, maps segments into virtual memory, and transfers control to the executable's entry point.

## Why this matters for low-level software

These projects connect APIs that application software often treats as black boxes:

- process creation
- virtual address spaces
- executable formats
- system-call ABIs
- file-descriptor tables
- memory protection
- assembly-level interfaces

That model is directly useful when debugging systems software, embedded Linux applications, runtimes, firmware-adjacent tools, loaders, drivers, and performance-sensitive code.
