#define _POSIX_C_SOURCE 200809L
#include "elf_parser.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void set_error(char *err, size_t err_size, const char *fmt, const char *arg) {
    if (err && err_size) snprintf(err, err_size, fmt, arg ? arg : "");
}

static int range_ok(const ElfImage *image, uint64_t offset, uint64_t bytes) {
    if (offset > image->size) return 0;
    return bytes <= image->size - (size_t)offset;
}

const char *elf_machine_name(int machine) {
    switch (machine) {
        case EM_386: return "Intel 80386";
        case EM_X86_64: return "AMD x86-64";
        case EM_ARM: return "ARM";
        case EM_AARCH64: return "AArch64";
        case EM_RISCV: return "RISC-V";
        default: return "Unknown";
    }
}

const char *elf_type_name(int type) {
    switch (type) {
        case ET_NONE: return "NONE";
        case ET_REL: return "REL (Relocatable)";
        case ET_EXEC: return "EXEC (Executable)";
        case ET_DYN: return "DYN (Shared/PIE)";
        case ET_CORE: return "CORE";
        default: return "Unknown";
    }
}

int elf_image_open(const char *path, ElfImage *image, char *err, size_t err_size) {
    memset(image, 0, sizeof(*image));
    image->fd = -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_error(err, err_size, "open failed: %s", strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        set_error(err, err_size, "fstat failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    if (st.st_size < EI_NIDENT) {
        set_error(err, err_size, "invalid file: %s", "too small to contain an ELF header");
        close(fd);
        return -1;
    }
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        set_error(err, err_size, "mmap failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    image->fd = fd;
    image->data = map;
    image->size = (size_t)st.st_size;

    if (memcmp(image->data, ELFMAG, SELFMAG) != 0) {
        set_error(err, err_size, "not an ELF file: %s", path);
        elf_image_close(image);
        return -1;
    }
    if (image->data[EI_DATA] != ELFDATA2LSB) {
        set_error(err, err_size, "unsupported ELF endianness: %s", "only little-endian is supported");
        elf_image_close(image);
        return -1;
    }
    image->elf_class = image->data[EI_CLASS];
    if (image->elf_class == ELFCLASS32) {
        if (!range_ok(image, 0, sizeof(Elf32_Ehdr))) {
            set_error(err, err_size, "truncated ELF header: %s", path);
            elf_image_close(image);
            return -1;
        }
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        image->machine = h->e_machine;
        image->type = h->e_type;
        image->entry = h->e_entry;
        if (h->e_ehsize < sizeof(*h)) {
            set_error(err, err_size, "invalid ELF32 header size: %s", path);
            elf_image_close(image);
            return -1;
        }
        if (h->e_phnum && (!h->e_phentsize || !range_ok(image, h->e_phoff, (uint64_t)h->e_phentsize * h->e_phnum))) {
            set_error(err, err_size, "invalid ELF32 program header table: %s", path);
            elf_image_close(image);
            return -1;
        }
        if (h->e_shnum && (!h->e_shentsize || !range_ok(image, h->e_shoff, (uint64_t)h->e_shentsize * h->e_shnum))) {
            set_error(err, err_size, "invalid ELF32 section header table: %s", path);
            elf_image_close(image);
            return -1;
        }
    } else if (image->elf_class == ELFCLASS64) {
        if (!range_ok(image, 0, sizeof(Elf64_Ehdr))) {
            set_error(err, err_size, "truncated ELF header: %s", path);
            elf_image_close(image);
            return -1;
        }
        const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
        image->machine = h->e_machine;
        image->type = h->e_type;
        image->entry = h->e_entry;
        if (h->e_ehsize < sizeof(*h)) {
            set_error(err, err_size, "invalid ELF64 header size: %s", path);
            elf_image_close(image);
            return -1;
        }
        if (h->e_phnum && (!h->e_phentsize || !range_ok(image, h->e_phoff, (uint64_t)h->e_phentsize * h->e_phnum))) {
            set_error(err, err_size, "invalid ELF64 program header table: %s", path);
            elf_image_close(image);
            return -1;
        }
        if (h->e_shnum && (!h->e_shentsize || !range_ok(image, h->e_shoff, (uint64_t)h->e_shentsize * h->e_shnum))) {
            set_error(err, err_size, "invalid ELF64 section header table: %s", path);
            elf_image_close(image);
            return -1;
        }
    } else {
        set_error(err, err_size, "unsupported ELF class: %s", "expected ELF32 or ELF64");
        elf_image_close(image);
        return -1;
    }
    return 0;
}

void elf_image_close(ElfImage *image) {
    if (!image) return;
    if (image->data && image->data != MAP_FAILED) munmap(image->data, image->size);
    if (image->fd >= 0) close(image->fd);
    memset(image, 0, sizeof(*image));
    image->fd = -1;
}

int elf_print_header(const ElfImage *image, int fd) {
    dprintf(fd, "ELF Header:\n");
    dprintf(fd, "  Magic:   %02x %02x %02x %02x\n", image->data[0], image->data[1], image->data[2], image->data[3]);
    dprintf(fd, "  Class:   ELF%d\n", image->elf_class == ELFCLASS32 ? 32 : 64);
    dprintf(fd, "  Data:    little-endian\n");
    dprintf(fd, "  Type:    %s\n", elf_type_name(image->type));
    dprintf(fd, "  Machine: %s (%d)\n", elf_machine_name(image->machine), image->machine);
    dprintf(fd, "  Entry:   0x%llx\n", (unsigned long long)image->entry);
    if (image->elf_class == ELFCLASS32) {
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        dprintf(fd, "  Program headers: offset=%u count=%u entsize=%u\n", h->e_phoff, h->e_phnum, h->e_phentsize);
        dprintf(fd, "  Section headers: offset=%u count=%u entsize=%u shstrndx=%u\n", h->e_shoff, h->e_shnum, h->e_shentsize, h->e_shstrndx);
    } else {
        const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
        dprintf(fd, "  Program headers: offset=%llu count=%u entsize=%u\n", (unsigned long long)h->e_phoff, h->e_phnum, h->e_phentsize);
        dprintf(fd, "  Section headers: offset=%llu count=%u entsize=%u shstrndx=%u\n", (unsigned long long)h->e_shoff, h->e_shnum, h->e_shentsize, h->e_shstrndx);
    }
    return 0;
}

static const char *ph_type_name(uint32_t type) {
    switch (type) {
        case PT_NULL: return "NULL";
        case PT_LOAD: return "LOAD";
        case PT_DYNAMIC: return "DYNAMIC";
        case PT_INTERP: return "INTERP";
        case PT_NOTE: return "NOTE";
        case PT_PHDR: return "PHDR";
        case PT_TLS: return "TLS";
        default: return "OTHER";
    }
}

static void perm_string(uint32_t flags, char out[4]) {
    out[0] = (flags & PF_R) ? 'R' : '-';
    out[1] = (flags & PF_W) ? 'W' : '-';
    out[2] = (flags & PF_X) ? 'X' : '-';
    out[3] = '\0';
}

int elf_print_segments(const ElfImage *image, int fd) {
    dprintf(fd, "Program Headers:\n");
    dprintf(fd, "  %-3s %-9s %-12s %-18s %-10s %-10s %-4s %-8s\n", "#", "Type", "Offset", "Vaddr", "FileSz", "MemSz", "Flg", "Align");
    if (image->elf_class == ELFCLASS32) {
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        for (uint16_t i = 0; i < h->e_phnum; ++i) {
            const Elf32_Phdr *p = (const Elf32_Phdr *)(image->data + h->e_phoff + (uint64_t)i * h->e_phentsize);
            char perms[4]; perm_string(p->p_flags, perms);
            dprintf(fd, "  %-3u %-9s 0x%08x   0x%08x         0x%08x 0x%08x %-4s 0x%x\n", i, ph_type_name(p->p_type), p->p_offset, p->p_vaddr, p->p_filesz, p->p_memsz, perms, p->p_align);
        }
    } else {
        const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
        for (uint16_t i = 0; i < h->e_phnum; ++i) {
            const Elf64_Phdr *p = (const Elf64_Phdr *)(image->data + h->e_phoff + (uint64_t)i * h->e_phentsize);
            char perms[4]; perm_string(p->p_flags, perms);
            dprintf(fd, "  %-3u %-9s 0x%010llx 0x%016llx 0x%08llx 0x%08llx %-4s 0x%llx\n", i, ph_type_name(p->p_type), (unsigned long long)p->p_offset, (unsigned long long)p->p_vaddr, (unsigned long long)p->p_filesz, (unsigned long long)p->p_memsz, perms, (unsigned long long)p->p_align);
        }
    }
    return 0;
}

static const char *sh_type_name(uint32_t type) {
    switch (type) {
        case SHT_NULL: return "NULL";
        case SHT_PROGBITS: return "PROGBITS";
        case SHT_SYMTAB: return "SYMTAB";
        case SHT_STRTAB: return "STRTAB";
        case SHT_RELA: return "RELA";
        case SHT_HASH: return "HASH";
        case SHT_DYNAMIC: return "DYNAMIC";
        case SHT_NOTE: return "NOTE";
        case SHT_NOBITS: return "NOBITS";
        case SHT_REL: return "REL";
        case SHT_DYNSYM: return "DYNSYM";
        default: return "OTHER";
    }
}

static void section_flags64(uint64_t flags, char out[5]) {
    size_t n = 0;
    if (flags & SHF_ALLOC) out[n++] = 'A';
    if (flags & SHF_WRITE) out[n++] = 'W';
    if (flags & SHF_EXECINSTR) out[n++] = 'X';
    out[n] = '\0';
}

int elf_print_sections(const ElfImage *image, int fd) {
    dprintf(fd, "Section Headers:\n");
    dprintf(fd, "  %-3s %-20s %-10s %-18s %-12s %-10s %-4s\n", "#", "Name", "Type", "Address", "Offset", "Size", "Flg");
    if (image->elf_class == ELFCLASS32) {
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        const char *names = "";
        size_t names_size = 0;
        if (h->e_shstrndx < h->e_shnum) {
            const Elf32_Shdr *str = (const Elf32_Shdr *)(image->data + h->e_shoff + (uint64_t)h->e_shstrndx * h->e_shentsize);
            if (range_ok(image, str->sh_offset, str->sh_size)) { names = (const char *)(image->data + str->sh_offset); names_size = str->sh_size; }
        }
        for (uint16_t i = 0; i < h->e_shnum; ++i) {
            const Elf32_Shdr *s = (const Elf32_Shdr *)(image->data + h->e_shoff + (uint64_t)i * h->e_shentsize);
            const char *name = (s->sh_name < names_size) ? names + s->sh_name : "<bad-name>";
            char flags[5]; section_flags64(s->sh_flags, flags);
            dprintf(fd, "  %-3u %-20.20s %-10s 0x%016x 0x%010x 0x%08x %-4s\n", i, name, sh_type_name(s->sh_type), s->sh_addr, s->sh_offset, s->sh_size, flags);
        }
    } else {
        const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
        const char *names = "";
        size_t names_size = 0;
        if (h->e_shstrndx < h->e_shnum) {
            const Elf64_Shdr *str = (const Elf64_Shdr *)(image->data + h->e_shoff + (uint64_t)h->e_shstrndx * h->e_shentsize);
            if (range_ok(image, str->sh_offset, str->sh_size)) { names = (const char *)(image->data + str->sh_offset); names_size = str->sh_size; }
        }
        for (uint16_t i = 0; i < h->e_shnum; ++i) {
            const Elf64_Shdr *s = (const Elf64_Shdr *)(image->data + h->e_shoff + (uint64_t)i * h->e_shentsize);
            const char *name = (s->sh_name < names_size) ? names + s->sh_name : "<bad-name>";
            char flags[5]; section_flags64(s->sh_flags, flags);
            dprintf(fd, "  %-3u %-20.20s %-10s 0x%016llx 0x%010llx 0x%08llx %-4s\n", i, name, sh_type_name(s->sh_type), (unsigned long long)s->sh_addr, (unsigned long long)s->sh_offset, (unsigned long long)s->sh_size, flags);
        }
    }
    return 0;
}

static const char *sym_bind_name(unsigned bind) {
    switch (bind) { case STB_LOCAL: return "LOCAL"; case STB_GLOBAL: return "GLOBAL"; case STB_WEAK: return "WEAK"; default: return "OTHER"; }
}

static const char *sym_type_name(unsigned type) {
    switch (type) { case STT_NOTYPE: return "NOTYPE"; case STT_OBJECT: return "OBJECT"; case STT_FUNC: return "FUNC"; case STT_SECTION: return "SECTION"; case STT_FILE: return "FILE"; case STT_TLS: return "TLS"; default: return "OTHER"; }
}

int elf_print_symbols(const ElfImage *image, int fd) {
    int found = 0;
    if (image->elf_class == ELFCLASS32) {
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        for (uint16_t si = 0; si < h->e_shnum; ++si) {
            const Elf32_Shdr *s = (const Elf32_Shdr *)(image->data + h->e_shoff + (uint64_t)si * h->e_shentsize);
            if (s->sh_type != SHT_SYMTAB && s->sh_type != SHT_DYNSYM) continue;
            if (s->sh_link >= h->e_shnum || !s->sh_entsize || !range_ok(image, s->sh_offset, s->sh_size)) continue;
            const Elf32_Shdr *str = (const Elf32_Shdr *)(image->data + h->e_shoff + (uint64_t)s->sh_link * h->e_shentsize);
            if (!range_ok(image, str->sh_offset, str->sh_size)) continue;
            const char *strings = (const char *)(image->data + str->sh_offset);
            size_t count = s->sh_size / s->sh_entsize;
            dprintf(fd, "Symbol table section %u (%zu entries):\n", si, count);
            dprintf(fd, "  %-5s %-12s %-8s %-8s %-6s %s\n", "Num", "Value", "Size", "Type", "Bind", "Name");
            for (size_t i = 0; i < count; ++i) {
                const Elf32_Sym *sym = (const Elf32_Sym *)(image->data + s->sh_offset + i * s->sh_entsize);
                const char *name = sym->st_name < str->sh_size ? strings + sym->st_name : "<bad-name>";
                dprintf(fd, "  %-5zu 0x%08x   %-8u %-8s %-6s %s\n", i, sym->st_value, sym->st_size, sym_type_name(ELF32_ST_TYPE(sym->st_info)), sym_bind_name(ELF32_ST_BIND(sym->st_info)), name);
            }
            found = 1;
        }
    } else {
        const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
        for (uint16_t si = 0; si < h->e_shnum; ++si) {
            const Elf64_Shdr *s = (const Elf64_Shdr *)(image->data + h->e_shoff + (uint64_t)si * h->e_shentsize);
            if (s->sh_type != SHT_SYMTAB && s->sh_type != SHT_DYNSYM) continue;
            if (s->sh_link >= h->e_shnum || !s->sh_entsize || !range_ok(image, s->sh_offset, s->sh_size)) continue;
            const Elf64_Shdr *str = (const Elf64_Shdr *)(image->data + h->e_shoff + (uint64_t)s->sh_link * h->e_shentsize);
            if (!range_ok(image, str->sh_offset, str->sh_size)) continue;
            const char *strings = (const char *)(image->data + str->sh_offset);
            size_t count = s->sh_size / s->sh_entsize;
            dprintf(fd, "Symbol table section %u (%zu entries):\n", si, count);
            dprintf(fd, "  %-5s %-18s %-8s %-8s %-6s %s\n", "Num", "Value", "Size", "Type", "Bind", "Name");
            for (size_t i = 0; i < count; ++i) {
                const Elf64_Sym *sym = (const Elf64_Sym *)(image->data + s->sh_offset + i * s->sh_entsize);
                const char *name = sym->st_name < str->sh_size ? strings + sym->st_name : "<bad-name>";
                dprintf(fd, "  %-5zu 0x%016llx %-8llu %-8s %-6s %s\n", i, (unsigned long long)sym->st_value, (unsigned long long)sym->st_size, sym_type_name(ELF64_ST_TYPE(sym->st_info)), sym_bind_name(ELF64_ST_BIND(sym->st_info)), name);
            }
            found = 1;
        }
    }
    if (!found) dprintf(fd, "No symbol tables found.\n");
    return 0;
}

int elf_vaddr_to_offset(const ElfImage *image, uint64_t vaddr, uint64_t *offset) {
    if (!offset) return -1;
    if (image->elf_class == ELFCLASS32) {
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        for (uint16_t i = 0; i < h->e_phnum; ++i) {
            const Elf32_Phdr *p = (const Elf32_Phdr *)(image->data + h->e_phoff + (uint64_t)i * h->e_phentsize);
            if (p->p_type == PT_LOAD && vaddr >= p->p_vaddr && vaddr < (uint64_t)p->p_vaddr + p->p_filesz) {
                *offset = p->p_offset + (vaddr - p->p_vaddr);
                return 0;
            }
        }
    } else {
        const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
        for (uint16_t i = 0; i < h->e_phnum; ++i) {
            const Elf64_Phdr *p = (const Elf64_Phdr *)(image->data + h->e_phoff + (uint64_t)i * h->e_phentsize);
            if (p->p_type == PT_LOAD && vaddr >= p->p_vaddr && vaddr < p->p_vaddr + p->p_filesz) {
                *offset = p->p_offset + (vaddr - p->p_vaddr);
                return 0;
            }
        }
    }
    return -1;
}

int elf_print_memory_map(const ElfImage *image, int fd) {
    dprintf(fd, "Memory Mapping Plan (PT_LOAD):\n");
    dprintf(fd, "  %-7s %-18s %-12s %-10s %-10s %-5s %-10s\n", "SEGMENT", "VADDR", "FILE OFF", "FILESZ", "MEMSZ", "PERM", "ZERO-FILL");
    if (image->elf_class == ELFCLASS32) {
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        for (uint16_t i = 0; i < h->e_phnum; ++i) {
            const Elf32_Phdr *p = (const Elf32_Phdr *)(image->data + h->e_phoff + (uint64_t)i * h->e_phentsize);
            if (p->p_type != PT_LOAD) continue;
            char perms[4]; perm_string(p->p_flags, perms);
            dprintf(fd, "  %-7s 0x%016x 0x%010x %-10u %-10u %-5s %-10u\n", "LOAD", p->p_vaddr, p->p_offset, p->p_filesz, p->p_memsz, perms, p->p_memsz > p->p_filesz ? p->p_memsz - p->p_filesz : 0);
        }
    } else {
        const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
        for (uint16_t i = 0; i < h->e_phnum; ++i) {
            const Elf64_Phdr *p = (const Elf64_Phdr *)(image->data + h->e_phoff + (uint64_t)i * h->e_phentsize);
            if (p->p_type != PT_LOAD) continue;
            char perms[4]; perm_string(p->p_flags, perms);
            dprintf(fd, "  %-7s 0x%016llx 0x%010llx %-10llu %-10llu %-5s %-10llu\n", "LOAD", (unsigned long long)p->p_vaddr, (unsigned long long)p->p_offset, (unsigned long long)p->p_filesz, (unsigned long long)p->p_memsz, perms, (unsigned long long)(p->p_memsz > p->p_filesz ? p->p_memsz - p->p_filesz : 0));
        }
    }
    return 0;
}
