# Low-Level Linux Systems Programming Suite

A portfolio-oriented collection of low-level systems programming exercises written in **C** and **x86 assembly**, developed during systems-programming coursework at Ben-Gurion University.

The repository follows the Linux execution path from a shell command, through processes and file descriptors, down to raw system calls, ELF object structure, virtual memory, and program loading.

## Highlights

- Unix process creation and execution with `fork`, `execvp`, and `waitpid`
- Pipelines and I/O redirection with file descriptors, `pipe`, `dup`, and `dup2`
- Process tracking, foreground/background execution, signals, and command history
- Raw 32-bit Linux system calls from NASM through `int 0x80`
- x86 registers, stack arguments, and low-level OS interfaces
- ELF headers, section headers, symbols, and file/virtual-address relationships
- ELF32 program-header parsing and `PT_LOAD` handling
- Virtual-memory concepts, page alignment, `mmap`, and R/W/X permissions
- Direct work with C, Linux, executable formats, and systems-level debugging concepts

## Repository Structure

```text
low-level-linux-systems-suite/
├── 01-shell-basics/
│   ├── myshell.c
│   ├── Makefile
│   └── README.md
├── 02-shell-pipelines-job-control/
│   ├── myshell.c
│   ├── mypipeline.c
│   ├── Makefile
│   └── README.md
├── 03-x86-assembly-syscalls/
│   ├── encoder.asm
│   ├── input.txt
│   ├── Makefile
│   └── README.md
├── 04-elf-static-loader/
│   ├── loader.c
│   ├── Makefile
│   └── README.md
├── docs/
│   ├── elf-object-format.md
│   ├── execution-model.md
│   └── portfolio-notes.md
└── README.md
```

## 1. Shell Basics

A minimal command interpreter in C that focuses on command execution and standard input/output redirection.

**Topics:**

- `execvp`
- file descriptors
- `open`, `close`, `dup2`
- stdin/stdout redirection
- working-directory handling

## 2. Shell, Pipelines, and Job Control

An extended Unix-style shell exercise that works directly with Linux process-management primitives.

**Topics:**

- `fork` and `execvp`
- `waitpid`
- `pipe`, `dup`, and `dup2`
- input/output redirection
- foreground and background execution
- process-state tracking
- signal-based process control
- command history and replay (`!!` and indexed history entries)

The standalone `mypipeline.c` demonstrates the file-descriptor mechanics of connecting the output of one child process to the input of another. `myshell.c` integrates process creation, command execution, pipelines, redirection, and shell state management.

## 3. x86 Assembly and Raw Linux System Calls

A 32-bit x86 NASM exercise that interacts with Linux through the raw `int 0x80` system-call interface.

The program processes command-line arguments, opens input/output files, reads bytes, applies a simple transformation, and writes results using register-based syscall arguments.

**Topics:**

- NASM and x86 registers
- stack-based function arguments
- file descriptors
- raw `open`, `read`, `write`, and `exit` system calls
- Linux 32-bit syscall ABI

## ELF Object Inspection and Symbol Analysis

The repository also documents the ELF object-file concepts studied before the loader implementation.

**Topics:**

- ELF headers and section headers
- symbol tables
- `.text`, `.rodata`, and symbol placement
- ELF entry-point metadata
- `readelf` and `/usr/include/elf.h`
- mapping virtual addresses to file offsets
- `open`, `read`, `write`, `lseek`, and `close`

See [`docs/elf-object-format.md`](docs/elf-object-format.md).

## 4. ELF32 Static Program Loader

An educational user-space loader in C that explores how ELF executables are represented and prepared for execution.

The implementation opens an ELF32 file, inspects its ELF and program headers, derives segment permissions, works with page-aligned virtual-memory parameters, maps loadable program regions with `mmap`, and transfers control through the executable entry-point interface used by the course environment.

**Topics:**

- `Elf32_Ehdr` and `Elf32_Phdr`
- `PT_LOAD`
- file offsets and virtual addresses
- page alignment
- `mmap`
- `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`
- linker/loader relationship
- executable entry points

## How the Projects Connect

```mermaid
flowchart LR
    A[Shell command] --> B[fork / exec]
    B --> C[File descriptors]
    C --> D[Pipes and signals]
    D --> E[Raw syscalls]
    E --> F[ELF sections and symbols]
    F --> G[ELF program headers]
    G --> H[Virtual memory mapping]
    H --> I[Program entry point]
```

Together, these modules provide hands-on exposure to the layers that application software normally hides: processes, system calls, executable formats, memory mappings, and machine-level interfaces.

## Build Environment

The coursework targets a **32-bit x86 Linux environment**. Typical requirements include:

- GCC with 32-bit support (`-m32`)
- NASM
- GNU Make
- Linux or WSL with the required 32-bit development libraries

Some original course support files referenced by the Makefiles, such as `LineParser.c/.h`, startup objects, helper files, or a custom linker script, were supplied separately in the course environment and are not included in the submitted archives used to build this portfolio repository.

## Skills Demonstrated

`C` · `Linux` · `x86 Assembly` · `System Calls` · `Processes` · `Signals` · `Pipes` · `IPC` · `File Descriptors` · `ELF` · `Symbol Tables` · `Virtual Memory` · `mmap` · `NASM` · `GCC` · `Make`

## Resume-Friendly Summary

**Low-Level Linux Systems Programming Suite — C, x86 Assembly, ELF, Linux**

- Built low-level Linux components covering process creation, pipelines, I/O redirection, process control, signals, and command history using `fork`, `execvp`, `waitpid`, `pipe`, and file-descriptor manipulation.
- Worked with raw x86 Linux system calls and ELF internals, including executable structure, program headers, file/virtual-address relationships, virtual-memory mappings, segment permissions, and executable entry points.

## Author

**Mohamed Taha**  
GitHub: https://github.com/mohamedtah22  
LinkedIn: https://linkedin.com/in/mohamed-taha-02314b314
