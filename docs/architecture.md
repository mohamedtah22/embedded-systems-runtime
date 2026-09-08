# Architecture

The repository is organized as one execution/runtime system rather than independent lab exercises.

```mermaid
flowchart TB
    CLI[mlrt CLI]

    CLI --> SH[Unix Shell]
    SH --> PROC[Process / Job Manager]
    PROC --> FD[File Descriptors / Pipes / Redirection]
    PROC --> TRACE[ptrace Tracer]

    CLI --> ELF[ELF Inspector]
    ELF --> LOAD[Static ELF Loader]
    LOAD --> VM[mmap / Virtual Memory / BSS]

    CLI --> ALLOC[Custom Allocator]
    CLI --> NET[Loopback TCP Command Server]
    ASM[Raw x86 int 0x80 Layer] --> PROC

    CLI --> RT[Embedded Runtime Demo]
    RT --> RB[Bounded Ring Buffer]
    RT --> WD[Watchdog / Timing Metrics]

    CLI --> SERIAL[PTY Virtual UART]
    CLI --> HOST[Target Control Client]
    SERIAL --> PROTO[Framed Binary Protocol + CRC32]
    HOST --> PROTO
    PROTO --> TARGET[Embedded Target Simulator]
    TARGET --> SENSOR[Periodic Sensor Simulation]
    SENSOR --> RB2[Bounded Queue]
    RB2 --> PROCESS[Processing Task]
    PROCESS --> WD2[Watchdog + Recovery State Machine]

    CROSS[AArch64 Cross Compiler] --> ARMTARGET[ARM64 Target Binary]
    ARMTARGET --> QEMU[QEMU AArch64]
```

## Design Boundaries

- `shell/`, `elf/`, `loader/`, `tracer/`, `allocator/`, and `network/` cover the Linux execution path.
- `protocol/`, `realtime/`, `serial/`, `target/`, and `firmware/` add embedded-style runtime behavior without physical hardware.
- `platform/arm64/` demonstrates cross-target builds and QEMU execution.
- `legacy/` preserves the original coursework only for provenance; production modules do not depend on course parser/startup helpers.
- `tests/` validates both the Linux path and the embedded simulation path.
