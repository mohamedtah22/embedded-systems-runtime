# Coursework Migration Notes

This repository originally consisted of separate systems-programming lab submissions (basic shell, pipelines/job control, x86 assembly syscalls, and an ELF loader).

The current top-level project integrates those concepts into reusable modules under `shell/`, `elf/`, `loader/`, `asm/`, `tracer/`, `allocator/`, and `network/`.

`asm/legacy_encoder.asm` is retained as the original NASM exercise for provenance. The old shell and loader copies are intentionally not part of the production build; their behavior has been reimplemented in the integrated codebase without the course-supplied `LineParser` or startup helper dependencies.
