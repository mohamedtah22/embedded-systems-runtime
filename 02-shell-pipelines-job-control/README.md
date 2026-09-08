# Shell, Pipelines, and Job Control

Extended Unix-style shell exercise written in C, focused on process creation, inter-process communication, file-descriptor manipulation, and basic shell state management.

## What It Demonstrates

- Process creation with `fork`
- Program execution with `execvp`
- Parent/child synchronization with `waitpid`
- Unix pipelines with `pipe`
- Standard-input/output redirection with `open`, `close`, `dup`, and `dup2`
- Foreground and background execution
- Process tracking and job-control concepts
- Signal-driven process control
- Command history and command replay (`!!` and indexed history entries)

## Pipeline Model

A pipeline connects the standard output of one process to the standard input of another through a kernel pipe. For a command such as:

```text
echo spider-pig | head -c 6
```

the shell creates a pipe, forks the required child processes, redirects the producer's `stdout` to the pipe write-end and the consumer's `stdin` to the pipe read-end, closes unused file descriptors, and executes both commands.

The standalone `mypipeline.c` focuses specifically on these file-descriptor mechanics, while `myshell.c` integrates pipelines into a broader shell implementation.

## Relevant Linux Interfaces

`fork(2)` · `execvp(3)` · `waitpid(2)` · `pipe(2)` · `dup(2)` · `dup2(2)` · `open(2)` · `close(2)`

## Course Support Files

The original Makefile references course-supplied `LineParser.c/.h` and `looper.c`; those files were not included in the uploaded archive.
