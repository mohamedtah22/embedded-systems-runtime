#include "elf_parser.h"

#include <elf.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: test_elf ELF\n"); return 2; }
    ElfImage image;
    char err[256];
    if (elf_image_open(argv[1], &image, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err); return 1;
    }
    if (image.elf_class != ELFCLASS32 && image.elf_class != ELFCLASS64) return 1;
    if (!image.machine) return 1;
    elf_print_memory_map(&image, STDOUT_FILENO);
    uint64_t off = 0;
    if (image.entry && elf_vaddr_to_offset(&image, image.entry, &off) != 0) {
        fprintf(stderr, "entry point should translate for this test executable\n");
        elf_image_close(&image);
        return 1;
    }
    elf_image_close(&image);
    printf("test_elf: PASS\n");
    return 0;
}
