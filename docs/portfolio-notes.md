# Portfolio Notes

## Suggested resume entry

**Low-Level Linux Systems Programming Suite — C, x86 Assembly, ELF, Linux**

- Built a collection of low-level Linux components including a Unix-style shell with process creation, pipelines, I/O redirection, job/process control, signals, and command history using `fork`, `execvp`, `waitpid`, `pipe`, and file-descriptor manipulation.
- Implemented x86 NASM code using raw Linux `int 0x80` system calls and developed an ELF32 static loader that parses program headers, works with page-aligned virtual-memory mappings and R/W/X segment permissions, and transfers execution to an executable entry point.

## Interview topics supported by this repository

- What happens during `fork` and `exec`?
- How does a Unix pipeline work at the file-descriptor level?
- What is the difference between a process and a program?
- How are standard input/output redirected?
- How do signals affect process state?
- How does a raw Linux syscall differ from a libc wrapper?
- What information is stored in an ELF program header?
- Why must memory mappings be page aligned?
- What is the relationship between `p_filesz` and `p_memsz`?
- How do ELF permission flags map to virtual-memory permissions?
- What does an executable entry point represent?
