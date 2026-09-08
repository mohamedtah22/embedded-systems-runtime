#include "mlrt.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *out) {
    fprintf(out,
        "Mini Linux Runtime & Execution Toolkit\n\n"
        "usage: mlrt <command> [arguments]\n\n"
        "commands:\n"
        "  shell                 interactive Unix-style shell\n"
        "  elf FILE [...]        inspect ELF headers/sections/symbols/segments\n"
        "  map FILE              visualize PT_LOAD virtual-memory layout\n"
        "  load FILE [...]       validate/map a static ELF image\n"
        "  trace ...             ptrace process debugger/tracer\n"
        "  alloc-demo            custom allocator demo and threaded stress test\n"
        "  server [...]          loopback-only TCP command server\n"
        "  client [...]          TCP client for the local command server\n"
        "  help                  show this help\n\n"
        "Execution path: command -> shell -> processes -> FDs/pipes -> ELF -> mmap -> ptrace -> TCP\n");
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? 2 : 0;
    }
    if (strcmp(argv[1], "shell") == 0) return shell_cli_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "elf") == 0) return elf_cli_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "map") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: mlrt map FILE\n"); return 2; }
        char *args[] = {"elf", argv[2], "--map", NULL};
        return elf_cli_main(3, args);
    }
    if (strcmp(argv[1], "load") == 0) return loader_cli_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "trace") == 0) return tracer_cli_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "alloc-demo") == 0) return allocator_cli_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "server") == 0) return network_server_cli_main(argc - 1, argv + 1);
    if (strcmp(argv[1], "client") == 0) return network_client_cli_main(argc - 1, argv + 1);

    fprintf(stderr, "mlrt: unknown command '%s'\n\n", argv[1]);
    usage(stderr);
    return 2;
}
