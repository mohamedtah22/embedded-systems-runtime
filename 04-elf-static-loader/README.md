# ELF32 Static Program Loader

C implementation of an educational user-space ELF loader.

The loader opens an ELF32 executable, maps the file for inspection, iterates through program headers, derives memory protection flags for loadable segments, maps program memory, and transfers control to the executable entry point through the course startup interface.

**Concepts:** ELF32, `PT_LOAD`, program headers, virtual memory, page alignment, `mmap`, R/W/X permissions, and executable entry points.

The original Makefile references course-supplied startup files and a linker script that were not included in the uploaded archive.
