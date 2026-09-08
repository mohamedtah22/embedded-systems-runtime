# Linux Execution Model Explored by This Project

The toolkit connects several mechanisms that are usually learned independently:

1. **Command input** becomes structured arguments and redirections.
2. **fork** creates execution contexts that initially share inherited descriptors and memory state.
3. **process groups** let the shell treat a multi-process pipeline as one foreground/background job.
4. **pipes and dup2** rewire standard streams without changing the executed programs.
5. **execvp** asks the kernel to replace the process image with an executable.
6. **ELF program headers** describe which file ranges become memory mappings.
7. **virtual memory** gives each segment its linked virtual address and R/W/X policy.
8. **BSS** demonstrates the difference between file size and in-memory size.
9. **ptrace** lets another process observe execution stops and architectural registers.
10. **TCP framing and concurrency** expose the same command-execution engine through a local client/server boundary.

The modules are separate for maintainability, but they intentionally share parsers/execution/ELF logic instead of duplicating lab implementations.
