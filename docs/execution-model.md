# Linux Execution Model

This note explains the target execution model. The integrated CLI currently implements ELF inspection and memory-layout reporting. Shell and loader source is preserved from the earlier coursework and is not yet integrated or verified by the top-level build.

## From a shell command to a process

The shell parses user input and decides whether a command should run in the foreground or background, whether its standard input/output should be redirected, and whether it participates in a pipeline.

For external commands, the shell creates a process with `fork`. The child configures file descriptors and then calls `execvp`, replacing its current process image with the requested executable.

## File descriptors and pipelines

Unix represents open files, pipes, and standard streams using small integer file descriptors. A pipeline connects two processes by replacing one child's standard output with the pipe's write end and the next child's standard input with the read end.

This repository exercises those operations directly with `pipe`, `close`, `dup`/`dup2`, and `execvp`.

## System calls

At a lower layer, operations such as reading, writing, opening files, and terminating a process ultimately enter the kernel through system calls. The NASM component demonstrates the 32-bit x86 Linux interface using `int 0x80`, syscall numbers in `EAX`, and arguments in registers.

## Executable loading

`exec` depends on the OS understanding the executable file format. The shared ELF parser now validates the ELF header and program headers, identifies loadable segments and describes their memory permissions. The next loader milestone will implement verified mappings, BSS initialization and entry-point transfer. The preserved original loader attempts these operations but is incomplete and requires absent startup support.

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
