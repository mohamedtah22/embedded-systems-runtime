#include "loader.h"
#include "elf_parser.h"
#include "mlrt.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr,
            "usage: mlrt load FILE [--map] [--execute] [--force-fixed]\n"
            "  default       validate and print the mapping plan only\n"
            "  --map         map PT_LOAD segments, then unmap them\n"
            "  --execute     map and jump to the entry point (freestanding static images only)\n"
            "  --force-fixed use MAP_FIXED instead of MAP_FIXED_NOREPLACE (educational binaries only)\n");
}

int loader_cli_main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    const char *path = argv[1];
    int do_map = 0, do_execute = 0, force_fixed = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--map") == 0) do_map = 1;
        else if (strcmp(argv[i], "--execute") == 0) do_map = do_execute = 1;
        else if (strcmp(argv[i], "--force-fixed") == 0) force_fixed = 1;
        else { usage(); return 2; }
    }

    ElfImage image;
    char err[256];
    if (elf_image_open(path, &image, err, sizeof(err)) != 0) {
        fprintf(stderr, "loader: %s\n", err);
        return 1;
    }
    elf_print_header(&image, STDOUT_FILENO);
    elf_print_memory_map(&image, STDOUT_FILENO);
    if (!do_map) {
        elf_image_close(&image);
        return 0;
    }

    LoaderMappingList mappings;
    if (loader_map_image(&image, force_fixed, &mappings, err, sizeof(err)) != 0) {
        fprintf(stderr, "loader: %s\n", err);
        elf_image_close(&image);
        return 1;
    }
    printf("Mapped %zu memory region(s). Entry point: 0x%llx\n", mappings.count, (unsigned long long)image.entry);

    if (do_execute) {
        char reason[256];
        if (!loader_can_execute(&image, reason, sizeof(reason))) {
            fprintf(stderr, "loader: cannot execute image: %s\n", reason);
            loader_unmap_all(&mappings);
            elf_image_close(&image);
            return 1;
        }
        printf("Transferring control to entry point 0x%llx ...\n", (unsigned long long)image.entry);
        fflush(stdout);
        void (*entry)(void) = (void (*)(void))(uintptr_t)image.entry;
        entry();
        fprintf(stderr, "loader: entry point returned; cleaning up mappings\n");
    }

    loader_unmap_all(&mappings);
    elf_image_close(&image);
    return 0;
}
