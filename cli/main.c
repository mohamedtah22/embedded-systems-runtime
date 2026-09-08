#include "elf_inspector.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "elf")) return elf_inspector_main(argc - 1, argv + 1);
    if (argc > 1 && !strcmp(argv[1], "memory")) return elf_memory_main(argc - 1, argv + 1);
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts("Mini Linux Runtime & Execution Toolkit 0.1.0 (ELF foundation)"); return 0;
    }
    int help = argc == 1 || (argc == 2 && !strcmp(argv[1], "--help"));
    FILE *out = help ? stdout : stderr;
    fputs("Mini Linux Runtime & Execution Toolkit\n"
          "Usage: mini-runtime <command> [options]\n"
          "  elf      Inspect ELF headers, sections, segments, symbols and addresses\n"
          "  memory   Display file-backed and zero-initialized LOAD ranges\n"
          "  --version\n"
          "Next milestones: loader, shell, tracer, allocator, networking.\n", out);
    return help ? 0 : 2;
}
