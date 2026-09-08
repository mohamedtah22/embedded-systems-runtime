#ifndef MINI_ELF_PARSER_H
#define MINI_ELF_PARSER_H

#include <stddef.h>
#include <stdint.h>

/* Normalized metadata: both ELF32/i386 and ELF64/x86-64 use this API. */
typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} ElfSegment;

typedef struct {
    uint32_t name, type, link, info;
    uint64_t flags, addr, offset, size, align, entsize;
} ElfSection;

typedef struct {
    uint32_t name;
    uint8_t info, other;
    uint16_t shndx;
    uint64_t value, size;
} ElfSymbol;

typedef struct {
    unsigned char *bytes;
    size_t size;
    unsigned bits, osabi, abi_version;
    uint16_t type, machine, ehsize, phentsize, shentsize;
    uint32_t flags;
    uint64_t entry, phoff, shoff;
    size_t phnum, shnum, shstrndx;
    ElfSegment *segments;
    ElfSection *sections;
} ElfFile;

typedef enum {
    ELF_ADDRESS_FILE,
    ELF_ADDRESS_ZERO,
    ELF_ADDRESS_UNMAPPED,
    ELF_ADDRESS_AMBIGUOUS
} ElfAddressKind;

/* On failure, out is empty and error contains a diagnostic. */
int elf_open(const char *path, ElfFile *out, char *error, size_t error_size);
void elf_close(ElfFile *file);
const char *elf_section_name(const ElfFile *file, size_t index);
const char *elf_symbol_name(const ElfFile *file, size_t section, const ElfSymbol *symbol);
int elf_symbol(const ElfFile *file, size_t section, size_t index, ElfSymbol *out);
ElfAddressKind elf_translate(const ElfFile *file, uint64_t address, uint64_t *offset);
#endif
