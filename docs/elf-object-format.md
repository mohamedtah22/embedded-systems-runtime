# ELF Object Format and Symbol Analysis

This note summarizes the ELF object-file concepts covered before implementing the static loader.

## ELF Overview

ELF stands for **Executable and Linkable Format** and is the binary format used by Linux for executable and object files. An ELF file stores machine code together with metadata that describes the file layout, sections, symbols, and execution-related information.

## Structures and Tools

The lab material focuses on:

- the ELF header
- section headers
- the symbol table
- `/usr/include/elf.h`
- the `readelf` utility
- file access through `open`, `read`, `write`, `lseek`, and `close`

Important ELF questions include:

- Where is the executable entry point stored?
- How many sections are present?
- What is the size and file location of `.text`?
- Where do symbols such as `_end` or `main` reside?
- What are a symbol's type and binding?
- What virtual address corresponds to a symbol or section?

## Mapping a Symbol to a File Offset

To locate a function inside the file, first determine which section contains it. Its file offset can then be calculated as:

```text
section_file_offset + function_virtual_address - section_virtual_address
```

The subtraction converts the function's virtual address into an offset relative to the beginning of its section; adding the section's file offset converts that relative position into a file position.

## Why This Matters for the Loader

ELF inspection provides the bridge between raw executable-file structure and program loading. Understanding headers, offsets, virtual addresses, sections, and symbols makes it easier to understand the later loader stage, where loadable segments are mapped into virtual memory and execution is transferred to the ELF entry point.
