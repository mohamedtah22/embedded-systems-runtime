#ifndef MLRT_ELF_PARSER_H
#define MLRT_ELF_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    unsigned char *data;
    size_t size;
    int elf_class;
    int machine;
    int type;
    uint64_t entry;
} ElfImage;

int elf_image_open(const char *path, ElfImage *image, char *err, size_t err_size);
void elf_image_close(ElfImage *image);
int elf_print_header(const ElfImage *image, int fd);
int elf_print_sections(const ElfImage *image, int fd);
int elf_print_segments(const ElfImage *image, int fd);
int elf_print_symbols(const ElfImage *image, int fd);
int elf_print_memory_map(const ElfImage *image, int fd);
int elf_vaddr_to_offset(const ElfImage *image, uint64_t vaddr, uint64_t *offset);
const char *elf_machine_name(int machine);
const char *elf_type_name(int type);

#endif
