#include "elf_parser.h"

#include <elf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Decode bytes explicitly: no unaligned access or host-structure casts. */
static uint16_t u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16;
}
static uint64_t u64(const unsigned char *p) {
    return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32;
}
static int fail(char *error, size_t size, const char *format, ...) {
    if (size) {
        va_list ap;
        va_start(ap, format);
        vsnprintf(error, size, format, ap);
        va_end(ap);
    }
    return -1;
}
static int range(const ElfFile *f, uint64_t offset, uint64_t size) {
    return offset <= f->size && size <= f->size - offset;
}
static int power_of_two(uint64_t n) { return n == 0 || (n & (n - 1)) == 0; }
static int table(const ElfFile *f, uint64_t offset, size_t count, size_t stride) {
    return offset <= f->size && stride != 0 && count <= (f->size - offset) / stride;
}
static void section_at(const ElfFile *f, size_t i, ElfSection *s) {
    const unsigned char *p = f->bytes + f->shoff + i * f->shentsize;
    s->name = u32(p); s->type = u32(p + 4);
    if (f->bits == 32) {
        s->flags = u32(p + 8); s->addr = u32(p + 12);
        s->offset = u32(p + 16); s->size = u32(p + 20);
        s->link = u32(p + 24); s->info = u32(p + 28);
        s->align = u32(p + 32); s->entsize = u32(p + 36);
    } else {
        s->flags = u64(p + 8); s->addr = u64(p + 16);
        s->offset = u64(p + 24); s->size = u64(p + 32);
        s->link = u32(p + 40); s->info = u32(p + 44);
        s->align = u64(p + 48); s->entsize = u64(p + 56);
    }
}
static const char *string_at(const ElfFile *f, size_t section, uint32_t offset) {
    if (section >= f->shnum) return NULL;
    const ElfSection *s = &f->sections[section];
    if (s->type != SHT_STRTAB || offset >= s->size) return NULL;
    const char *p = (const char *)f->bytes + s->offset + offset;
    return memchr(p, '\0', (size_t)(s->size - offset)) ? p : NULL;
}
void elf_close(ElfFile *f) {
    free(f->bytes); free(f->segments); free(f->sections);
    memset(f, 0, sizeof(*f));
}
const char *elf_section_name(const ElfFile *f, size_t i) {
    if (i >= f->shnum) return NULL;
    if (f->shstrndx == SHN_UNDEF) return "";
    return string_at(f, f->shstrndx, f->sections[i].name);
}
const char *elf_symbol_name(const ElfFile *f, size_t section, const ElfSymbol *s) {
    if (section >= f->shnum) return NULL;
    return string_at(f, f->sections[section].link, s->name);
}
int elf_symbol(const ElfFile *f, size_t section, size_t i, ElfSymbol *s) {
    if (section >= f->shnum) return -1;
    const ElfSection *t = &f->sections[section];
    if ((t->type != SHT_SYMTAB && t->type != SHT_DYNSYM) ||
        !t->entsize || i >= t->size / t->entsize) return -1;
    const unsigned char *p = f->bytes + t->offset + i * t->entsize;
    s->name = u32(p);
    if (f->bits == 32) {
        s->value = u32(p + 4); s->size = u32(p + 8);
        s->info = p[12]; s->other = p[13]; s->shndx = u16(p + 14);
    } else {
        s->info = p[4]; s->other = p[5]; s->shndx = u16(p + 6);
        s->value = u64(p + 8); s->size = u64(p + 16);
    }
    return 0;
}
static int validate(ElfFile *f, char *error, size_t n) {
#define CHECK(c, ...) do { if (!(c)) return fail(error, n, __VA_ARGS__); } while (0)
    CHECK(f->size >= EI_NIDENT, "truncated ELF identification");
    const unsigned char *p = f->bytes;
    CHECK(memcmp(p, ELFMAG, SELFMAG) == 0, "invalid ELF magic");
    CHECK(p[EI_CLASS] == ELFCLASS32 || p[EI_CLASS] == ELFCLASS64, "unsupported ELF class");
    CHECK(p[EI_DATA] == ELFDATA2LSB, "unsupported byte order (little-endian required)");
    CHECK(p[EI_VERSION] == EV_CURRENT, "invalid ELF identification version");
    f->bits = p[EI_CLASS] == ELFCLASS32 ? 32 : 64;
    f->osabi = p[EI_OSABI]; f->abi_version = p[EI_ABIVERSION];
    CHECK(f->size >= (f->bits == 32 ? 52u : 64u), "truncated ELF header");
    f->type = u16(p + 16); f->machine = u16(p + 18);
    CHECK((f->bits == 32 && f->machine == EM_386) ||
          (f->bits == 64 && f->machine == EM_X86_64), "unsupported architecture/class combination");
    CHECK(u32(p + 20) == EV_CURRENT, "invalid ELF header version");
    CHECK(f->type == ET_REL || f->type == ET_EXEC || f->type == ET_DYN || f->type == ET_CORE,
          "unsupported ELF type");
    if (f->bits == 32) {
        f->entry = u32(p + 24); f->phoff = u32(p + 28); f->shoff = u32(p + 32);
        f->flags = u32(p + 36); p += 40;
    } else {
        f->entry = u64(p + 24); f->phoff = u64(p + 32); f->shoff = u64(p + 40);
        f->flags = u32(p + 48); p += 52;
    }
    f->ehsize = u16(p); f->phentsize = u16(p + 2); f->phnum = u16(p + 4);
    f->shentsize = u16(p + 6); f->shnum = u16(p + 8); f->shstrndx = u16(p + 10);
    CHECK(f->ehsize == (f->bits == 32 ? 52 : 64), "invalid ELF header size");
    CHECK(f->phnum != PN_XNUM && f->shstrndx != SHN_XINDEX && !(f->shoff && !f->shnum),
          "extended ELF numbering is not supported");
    CHECK((f->shnum != 0) == (f->shoff != 0), "inconsistent section table offset/count");
    CHECK((f->phnum != 0) == (f->phoff != 0), "inconsistent program table offset/count");
    CHECK(!f->phnum || (f->phoff >= f->ehsize && f->phentsize == (f->bits == 32 ? 32 : 56) &&
          table(f, f->phoff, f->phnum, f->phentsize)), "invalid or truncated program header table");
    CHECK(!f->shnum || (f->shoff >= f->ehsize && f->shentsize == (f->bits == 32 ? 40 : 64) &&
          table(f, f->shoff, f->shnum, f->shentsize)), "invalid or truncated section header table");
    CHECK(f->shstrndx == SHN_UNDEF || f->shstrndx < f->shnum, "invalid section name table index");
    f->segments = calloc(f->phnum ? f->phnum : 1, sizeof(*f->segments));
    f->sections = calloc(f->shnum ? f->shnum : 1, sizeof(*f->sections));
    CHECK(f->segments && f->sections, "out of memory for ELF metadata");
    for (size_t i = 0; i < f->phnum; ++i) {
        ElfSegment *s = &f->segments[i];
        p = f->bytes + f->phoff + i * f->phentsize;
        s->type = u32(p);
        if (f->bits == 32) {
            s->offset = u32(p + 4); s->vaddr = u32(p + 8); s->paddr = u32(p + 12);
            s->filesz = u32(p + 16); s->memsz = u32(p + 20);
            s->flags = u32(p + 24); s->align = u32(p + 28);
        } else {
            s->flags = u32(p + 4); s->offset = u64(p + 8); s->vaddr = u64(p + 16);
            s->paddr = u64(p + 24); s->filesz = u64(p + 32);
            s->memsz = u64(p + 40); s->align = u64(p + 48);
        }
        CHECK(range(f, s->offset, s->filesz), "program header %zu: file range outside ELF", i);
        if (s->type != PT_LOAD) continue;
        CHECK(s->filesz <= s->memsz, "LOAD %zu: filesz exceeds memsz", i);
        CHECK(s->memsz <= UINT64_MAX - s->vaddr, "LOAD %zu: address overflow", i);
        CHECK(f->bits != 32 || s->vaddr + s->memsz <= UINT64_C(0x100000000),
              "LOAD %zu: exceeds 32-bit address space", i);
        CHECK(power_of_two(s->align), "LOAD %zu: alignment is not a power of two", i);
        CHECK(s->align <= 1 || s->vaddr % s->align == s->offset % s->align,
              "LOAD %zu: incongruent address/offset alignment", i);
    }
    for (size_t i = 0; i < f->shnum; ++i) {
        ElfSection *s = &f->sections[i];
        section_at(f, i, s);
        CHECK(i != 0 || s->type == SHT_NULL, "section zero must be NULL");
        if (s->type == SHT_NULL) continue;
        CHECK(s->type == SHT_NOBITS || range(f, s->offset, s->size),
              "section %zu: file range outside ELF", i);
        CHECK(power_of_two(s->align), "section %zu: invalid alignment", i);
        CHECK(s->size <= UINT64_MAX - s->addr, "section %zu: address overflow", i);
        if (s->type == SHT_STRTAB) {
            CHECK(s->size && f->bytes[s->offset] == 0 && f->bytes[s->offset + s->size - 1] == 0,
                  "section %zu: invalid string table", i);
        }
    }
    CHECK(!f->shstrndx || f->sections[f->shstrndx].type == SHT_STRTAB, "section names must use STRTAB");
    for (size_t i = 0; i < f->shnum; ++i) {
        const ElfSection *s = &f->sections[i];
        CHECK(elf_section_name(f, i) != NULL, "section %zu: invalid name", i);
        if (s->type != SHT_SYMTAB && s->type != SHT_DYNSYM) continue;
        CHECK(s->entsize == (f->bits == 32 ? 16u : 24u) && s->size % s->entsize == 0,
              "section %zu: invalid symbol entry size", i);
        CHECK(s->link < f->shnum && f->sections[s->link].type == SHT_STRTAB,
              "section %zu: invalid symbol string table", i);
        CHECK(s->info <= s->size / s->entsize, "section %zu: invalid local symbol boundary", i);
        for (size_t j = 0; j < s->size / s->entsize; ++j) {
            ElfSymbol symbol;
            CHECK(elf_symbol(f, i, j, &symbol) == 0, "invalid symbol");
            CHECK(elf_symbol_name(f, i, &symbol), "section %zu symbol %zu: invalid name", i, j);
            CHECK(symbol.shndx != SHN_XINDEX, "extended symbol section indexes are not supported");
            CHECK(symbol.shndx >= SHN_LORESERVE || symbol.shndx < f->shnum,
                  "section %zu symbol %zu: invalid section index", i, j);
        }
    }
    return 0;
#undef CHECK
}
int elf_open(const char *path, ElfFile *out, char *error, size_t n) {
    memset(out, 0, sizeof(*out));
    FILE *stream = fopen(path, "rb");
    if (!stream) return fail(error, n, "open %s: %s", path, strerror(errno));
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream); return fail(error, n, "cannot seek input");
    }
    long size = ftell(stream);
    if (size < 0 || (uintmax_t)size > SIZE_MAX || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream); return fail(error, n, "invalid input size");
    }
    out->size = (size_t)size;
    out->bytes = malloc(out->size ? out->size : 1);
    if (!out->bytes) { fclose(stream); elf_close(out); return fail(error, n, "out of memory reading ELF"); }
    size_t got = fread(out->bytes, 1, out->size, stream);
    int read_error = ferror(stream);
    int close_error = fclose(stream);
    if (got != out->size || read_error || close_error) {
        elf_close(out); return fail(error, n, "failed reading ELF");
    }
    if (validate(out, error, n)) { elf_close(out); return -1; }
    if (n) error[0] = '\0';
    return 0;
}
ElfAddressKind elf_translate(const ElfFile *f, uint64_t address, uint64_t *offset) {
    ElfAddressKind result = ELF_ADDRESS_UNMAPPED;
    for (size_t i = 0; i < f->phnum; ++i) {
        const ElfSegment *s = &f->segments[i];
        if (s->type != PT_LOAD || address < s->vaddr || address - s->vaddr >= s->memsz) continue;
        if (result != ELF_ADDRESS_UNMAPPED) return ELF_ADDRESS_AMBIGUOUS;
        uint64_t delta = address - s->vaddr;
        if (delta < s->filesz) { result = ELF_ADDRESS_FILE; *offset = s->offset + delta; }
        else result = ELF_ADDRESS_ZERO;
    }
    return result;
}
