# Portfolio positioning

## Implemented and tested in the integrated toolkit

**Mini Linux Runtime & Execution Toolkit — C, Linux, ELF, x86**

- Built a shared C parser for ELF32/i386 and ELF64/x86-64, exposing executable
  headers, sections, symbol tables, program headers and virtual-memory layout.
- Implemented virtual-address-to-file-offset translation that distinguishes
  file-backed data, zero-initialized BSS, unmapped ranges and ambiguous overlaps.
- Added bounds/overflow validation and automated malformed-input regression tests,
  plus comparisons against GNU readelf and ASan/UBSan test builds.

## Preserved earlier work

Original sources cover Unix process creation, pipes, file descriptors, signals,
command history, NASM raw syscalls and an educational ELF32 loader. They still
require missing course support files and have not been verified by the new build.
Discuss their implementation and limitations separately from the tested toolkit.

## Add to the resume only after implementation and verification

Full job control, N-stage shell pipelines, robust static program loading, ptrace
tracing, custom allocation and a TCP shell are planned milestones. The presence
of a roadmap is not evidence that those capabilities already work.

This is Linux user-space systems work. It does not demonstrate kernel development,
device drivers, MCU firmware, STM32, FreeRTOS, JTAG or peripheral bus protocols.
