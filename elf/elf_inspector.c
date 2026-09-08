#include "elf_inspector.h"
#include "elf_parser.h"

#include <elf.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Escape untrusted ELF names so they cannot inject terminal control codes. */
static void print_name(const char *s) {
    if (!s) { fputs("<invalid>", stdout); return; }
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c >= 32 && c <= 126 && c != '\\') putchar(c);
        else printf("\\x%02x", c);
    }
}
static const char *segment_type(uint32_t t) {
    switch (t) {
    case PT_NULL: return "NULL"; case PT_LOAD: return "LOAD";
    case PT_DYNAMIC: return "DYNAMIC"; case PT_INTERP: return "INTERP";
    case PT_NOTE: return "NOTE"; case PT_PHDR: return "PHDR";
    case PT_TLS: return "TLS"; case PT_GNU_STACK: return "GNU_STACK";
    case PT_GNU_RELRO: return "GNU_RELRO"; default: return "OTHER";
    }
}
static const char *section_type(uint32_t t) {
    switch (t) {
    case SHT_NULL: return "NULL"; case SHT_PROGBITS: return "PROGBITS";
    case SHT_SYMTAB: return "SYMTAB"; case SHT_STRTAB: return "STRTAB";
    case SHT_RELA: return "RELA"; case SHT_REL: return "REL";
    case SHT_NOBITS: return "NOBITS"; case SHT_DYNSYM: return "DYNSYM";
    case SHT_NOTE: return "NOTE"; case SHT_DYNAMIC: return "DYNAMIC";
    default: return "OTHER";
    }
}
static const char *symbol_type(unsigned t) {
    switch (t) {
    case STT_NOTYPE: return "NOTYPE"; case STT_OBJECT: return "OBJECT";
    case STT_FUNC: return "FUNC"; case STT_SECTION: return "SECTION";
    case STT_FILE: return "FILE"; case STT_COMMON: return "COMMON";
    case STT_TLS: return "TLS"; default: return "OTHER";
    }
}
static const char *binding(unsigned t) {
    switch (t) {
    case STB_LOCAL: return "LOCAL"; case STB_GLOBAL: return "GLOBAL";
    case STB_WEAK: return "WEAK"; default: return "OTHER";
    }
}
static void header(const ElfFile *f) {
    puts("ELF header");
    printf("  Magic: 7f 45 4c 46\n  Class: ELF%u\n  Encoding: little-endian\n", f->bits);
    printf("  Architecture: %s (%u)\n  Type: %u\n  Version: 1\n",
           f->machine == EM_386 ? "Intel 80386" : "AMD x86-64", f->machine, f->type);
    printf("  OS ABI: %u  ABI version: %u\n  Entry: 0x%" PRIx64 "\n", f->osabi, f->abi_version, f->entry);
    printf("  Flags: 0x%x\n  Header size: %u\n", f->flags, f->ehsize);
    printf("  Program headers: offset=0x%" PRIx64 " count=%zu entry-size=%u\n", f->phoff, f->phnum, f->phentsize);
    printf("  Section headers: offset=0x%" PRIx64 " count=%zu entry-size=%u names-index=%zu\n",
           f->shoff, f->shnum, f->shentsize, f->shstrndx);
}
static void segments(const ElfFile *f, int only_load) {
    puts("IDX TYPE       TYPE-ID    VADDR              END                OFFSET       FILESZ       MEMSZ        PERM ALIGN        ZERO");
    for (size_t i = 0; i < f->phnum; ++i) {
        const ElfSegment *s = &f->segments[i];
        if (only_load && s->type != PT_LOAD) continue;
        printf("%3zu %-10s 0x%08x 0x%016" PRIx64 " ", i, segment_type(s->type), s->type, s->vaddr);
        if (s->memsz <= UINT64_MAX - s->vaddr) printf("0x%016" PRIx64 " ", s->vaddr + s->memsz);
        else printf("%-18s ", "overflow");
        printf("0x%010" PRIx64 " 0x%010" PRIx64 " 0x%010" PRIx64 " %c%c%c  0x%010" PRIx64 " ",
               s->offset, s->filesz, s->memsz,
               s->flags & PF_R ? 'R' : '-', s->flags & PF_W ? 'W' : '-', s->flags & PF_X ? 'X' : '-', s->align);
        if (s->type == PT_LOAD) printf("0x%" PRIx64, s->memsz - s->filesz);
        else putchar('-');
        printf("  PADDR=0x%" PRIx64 "\n", s->paddr);
    }
}
static void sections(const ElfFile *f) {
    puts("IDX TYPE       TYPE-ID    VADDR              OFFSET       SIZE         FLAGS ALIGN        ENTSIZE LINK INFO NAME");
    for (size_t i = 0; i < f->shnum; ++i) {
        const ElfSection *s = &f->sections[i];
        printf("%3zu %-10s 0x%08x 0x%016" PRIx64 " 0x%010" PRIx64 " 0x%010" PRIx64 " %c%c%c   0x%010" PRIx64 " %7" PRIu64 " %4u %4u ",
               i, section_type(s->type), s->type, s->addr, s->offset, s->size,
               s->flags & SHF_ALLOC ? 'A' : '-', s->flags & SHF_WRITE ? 'W' : '-',
               s->flags & SHF_EXECINSTR ? 'X' : '-', s->align, s->entsize, s->link, s->info);
        print_name(elf_section_name(f, i));
        printf(" (flags=0x%" PRIx64 ")\n", s->flags);
    }
    puts("A=allocated, W=writable, X=executable. Runtime page permissions come from PT_LOAD, not section flags.");
}
static void symbols(const ElfFile *f) {
    for (size_t i = 0; i < f->shnum; ++i) {
        const ElfSection *s = &f->sections[i];
        if (s->type != SHT_SYMTAB && s->type != SHT_DYNSYM) continue;
        fputs("Symbol table: ", stdout); print_name(elf_section_name(f, i)); putchar('\n');
        puts("IDX VALUE              SIZE TYPE       BIND      SHNDX VIS NAME");
        for (size_t j = 0; j < s->size / s->entsize; ++j) {
            ElfSymbol sym;
            if (elf_symbol(f, i, j, &sym)) continue;
            printf("%3zu 0x%016" PRIx64 " %4" PRIu64 " %-10s %-9s %5u %3u ",
                   j, sym.value, sym.size, symbol_type(ELF64_ST_TYPE(sym.info)),
                   binding(ELF64_ST_BIND(sym.info)), sym.shndx, ELF64_ST_VISIBILITY(sym.other));
            print_name(elf_symbol_name(f, i, &sym));
            printf(" (type=%u bind=%u)\n", ELF64_ST_TYPE(sym.info), ELF64_ST_BIND(sym.info));
        }
    }
}
static int address_value(const char *text, uint64_t *value) {
    if (!*text || *text == '-' || *text == '+' || *text == ' ' || *text == '\t') return -1;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(text, &end, 0);
    if (errno || *end || v > UINT64_MAX) return -1;
    *value = (uint64_t)v;
    return 0;
}
int elf_memory_main(int argc, char **argv) {
    if (argc != 2) { fputs("Usage: mini-runtime memory FILE\n", stderr); return 2; }
    ElfFile file; char error[256];
    if (elf_open(argv[1], &file, error, sizeof(error))) { fprintf(stderr, "memory: %s\n", error); return 1; }
    puts("PT_LOAD layout from ELF metadata (no memory is mapped or executed)");
    puts("Ranges are [VADDR, END); ZERO bytes follow FILESZ within MEMSZ.");
    segments(&file, 1);
    elf_close(&file);
    return ferror(stdout) ? 1 : 0;
}
int elf_inspector_main(int argc, char **argv) {
    unsigned show = 0;
    int translate = 0; uint64_t address = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--header")) show |= 1;
        else if (!strcmp(argv[i], "--sections")) show |= 2;
        else if (!strcmp(argv[i], "--segments")) show |= 4;
        else if (!strcmp(argv[i], "--symbols")) show |= 8;
        else if (!strcmp(argv[i], "--all")) show = 15;
        else if (!strcmp(argv[i], "--vaddr")) {
            if (translate || ++i == argc || address_value(argv[i], &address)) goto usage;
            translate = 1;
        } else if (!strcmp(argv[i], "--")) {
            if (++i != argc - 1 || path) goto usage;
            path = argv[i];
        } else if (argv[i][0] == '-' || path) goto usage;
        else path = argv[i];
    }
    if (!path) goto usage;
    if (!show && !translate) show = 15;
    ElfFile file; char error[256];
    if (elf_open(path, &file, error, sizeof(error))) { fprintf(stderr, "elf: %s\n", error); return 1; }
    if (show & 1) header(&file);
    if (show & 2) sections(&file);
    if (show & 4) segments(&file, 0);
    if (show & 8) symbols(&file);
    int status = 0;
    if (translate) {
        uint64_t offset = 0;
        ElfAddressKind kind = elf_translate(&file, address, &offset);
        printf("0x%" PRIx64 ": ", address);
        if (kind == ELF_ADDRESS_FILE) printf("file offset 0x%" PRIx64 "\n", offset);
        else if (kind == ELF_ADDRESS_ZERO) puts("zero-initialized memory (BSS); no file offset");
        else if (kind == ELF_ADDRESS_AMBIGUOUS) { puts("ambiguous: overlapping LOAD segments"); status = 1; }
        else { puts("not covered by a LOAD segment"); status = 1; }
    }
    elf_close(&file);
    return ferror(stdout) ? 1 : status;
usage:
    fputs("Usage: mini-runtime elf [--header] [--sections] [--segments] [--symbols]\n"
          "                        [--all] [--vaddr ADDRESS] [--] FILE\n", stderr);
    return 2;
}
