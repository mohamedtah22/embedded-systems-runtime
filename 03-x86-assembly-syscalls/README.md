# x86 Assembly and Linux System Calls

NASM/32-bit x86 exercise demonstrating direct Linux system-call usage through `int 0x80`.

The program processes command-line options, uses raw file-related syscalls, reads input one byte at a time, transforms selected characters, and writes the result to the configured output file.

**Concepts:** x86 registers, stack arguments, NASM, file descriptors, `open`, `read`, `write`, `exit`, and the Linux 32-bit syscall ABI.

The original Makefile references a course-supplied `util.c`, which was not present in the uploaded archive.
