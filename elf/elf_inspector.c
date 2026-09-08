#include "elf_parser.h"
#include "common.h"
#include "mlrt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr,
            "usage: mlrt elf FILE [--header] [--sections] [--segments] [--symbols] [--map] [--vaddr ADDRESS]\n"
            "       mlrt map FILE\n");
}

int elf_cli_main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    const char *path = argv[1];
    int header = 0, sections = 0, segments = 0, symbols = 0, map = 0;
    int requested = 0;
    unsigned long long vaddr = 0;
    int have_vaddr = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--header") == 0) header = requested = 1;
        else if (strcmp(argv[i], "--sections") == 0) sections = requested = 1;
        else if (strcmp(argv[i], "--segments") == 0) segments = requested = 1;
        else if (strcmp(argv[i], "--symbols") == 0) symbols = requested = 1;
        else if (strcmp(argv[i], "--map") == 0) map = requested = 1;
        else if (strcmp(argv[i], "--all") == 0) header = sections = segments = symbols = map = requested = 1;
        else if (strcmp(argv[i], "--vaddr") == 0 && i + 1 < argc) {
            if (mlrt_parse_u64(argv[++i], &vaddr) != 0) { fprintf(stderr, "invalid address\n"); return 2; }
            have_vaddr = requested = 1;
        } else { usage(); return 2; }
    }
    if (!requested) header = sections = segments = symbols = map = 1;

    ElfImage image;
    char err[256];
    if (elf_image_open(path, &image, err, sizeof(err)) != 0) {
        fprintf(stderr, "elf: %s\n", err);
        return 1;
    }
    if (header) elf_print_header(&image, STDOUT_FILENO);
    if (sections) elf_print_sections(&image, STDOUT_FILENO);
    if (segments) elf_print_segments(&image, STDOUT_FILENO);
    if (symbols) elf_print_symbols(&image, STDOUT_FILENO);
    if (map) elf_print_memory_map(&image, STDOUT_FILENO);
    if (have_vaddr) {
        uint64_t off;
        if (elf_vaddr_to_offset(&image, vaddr, &off) == 0) printf("vaddr 0x%llx -> file offset 0x%llx\n", vaddr, (unsigned long long)off);
        else printf("vaddr 0x%llx is not backed by file data in a PT_LOAD segment\n", vaddr);
    }
    elf_image_close(&image);
    return 0;
}
