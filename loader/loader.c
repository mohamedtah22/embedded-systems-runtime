#include "loader.h"
#include "common.h"

#include <elf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

typedef struct {
    uint64_t type;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
    uint32_t flags;
} GenericPhdr;

static int add_mapping(LoaderMappingList *list, void *addr, size_t length) {
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 8;
        LoaderMapping *items = realloc(list->items, cap * sizeof(*items));
        if (!items) return -1;
        list->items = items;
        list->cap = cap;
    }
    list->items[list->count++] = (LoaderMapping){.addr = addr, .length = length};
    return 0;
}

void loader_unmap_all(LoaderMappingList *mappings) {
    if (!mappings) return;
    for (size_t i = mappings->count; i > 0; --i) {
        munmap(mappings->items[i - 1].addr, mappings->items[i - 1].length);
    }
    free(mappings->items);
    memset(mappings, 0, sizeof(*mappings));
}

static int flags_to_prot(uint32_t flags) {
    int prot = 0;
    if (flags & PF_R) prot |= PROT_READ;
    if (flags & PF_W) prot |= PROT_WRITE;
    if (flags & PF_X) prot |= PROT_EXEC;
    return prot;
}

static int get_phdr(const ElfImage *image, size_t index, GenericPhdr *out) {
    if (image->elf_class == ELFCLASS32) {
        const Elf32_Ehdr *h = (const Elf32_Ehdr *)image->data;
        if (index >= h->e_phnum) return -1;
        const Elf32_Phdr *p = (const Elf32_Phdr *)(image->data + h->e_phoff + index * h->e_phentsize);
        *out = (GenericPhdr){p->p_type, p->p_offset, p->p_vaddr, p->p_filesz, p->p_memsz, p->p_align, p->p_flags};
        return 0;
    }
    const Elf64_Ehdr *h = (const Elf64_Ehdr *)image->data;
    if (index >= h->e_phnum) return -1;
    const Elf64_Phdr *p = (const Elf64_Phdr *)(image->data + h->e_phoff + index * h->e_phentsize);
    *out = (GenericPhdr){p->p_type, p->p_offset, p->p_vaddr, p->p_filesz, p->p_memsz, p->p_align, p->p_flags};
    return 0;
}

static size_t phnum(const ElfImage *image) {
    return image->elf_class == ELFCLASS32 ? ((const Elf32_Ehdr *)image->data)->e_phnum : ((const Elf64_Ehdr *)image->data)->e_phnum;
}

static int image_has_interp(const ElfImage *image) {
    for (size_t i = 0; i < phnum(image); ++i) {
        GenericPhdr p;
        get_phdr(image, i, &p);
        if (p.type == PT_INTERP) return 1;
    }
    return 0;
}

static int map_one_segment(const ElfImage *image, const GenericPhdr *p, int force_fixed,
                           LoaderMappingList *mappings, char *err, size_t err_size) {
    if (p->filesz > p->memsz) {
        snprintf(err, err_size, "PT_LOAD has p_filesz > p_memsz");
        return -1;
    }
    if (p->offset + p->filesz > image->size) {
        snprintf(err, err_size, "PT_LOAD file range exceeds ELF file size");
        return -1;
    }

    long page_long = sysconf(_SC_PAGESIZE);
    size_t page = page_long > 0 ? (size_t)page_long : 4096;
    uint64_t page_mask = (uint64_t)page - 1;
    uint64_t map_vaddr = p->vaddr & ~page_mask;
    uint64_t map_offset = p->offset & ~page_mask;
    uint64_t delta = p->vaddr - map_vaddr;
    if ((p->offset - map_offset) != delta) {
        snprintf(err, err_size, "PT_LOAD p_vaddr/p_offset are not page-congruent");
        return -1;
    }
    if (map_offset % page != 0) {
        snprintf(err, err_size, "aligned file offset is not page aligned");
        return -1;
    }

    size_t mem_total = mlrt_round_up((size_t)(delta + p->memsz), page);
    size_t file_total = p->filesz ? mlrt_round_up((size_t)(delta + p->filesz), page) : 0;
    int final_prot = flags_to_prot(p->flags);
    int temporary_prot = final_prot | PROT_WRITE;
    int fixed_flag = force_fixed ? MAP_FIXED : MAP_FIXED_NOREPLACE;

    if (file_total) {
        void *mapped = mmap((void *)(uintptr_t)map_vaddr, file_total, temporary_prot,
                            MAP_PRIVATE | fixed_flag, image->fd, (off_t)map_offset);
        if (mapped == MAP_FAILED) {
            snprintf(err, err_size, "mmap file-backed PT_LOAD at 0x%llx failed: %s",
                     (unsigned long long)map_vaddr, strerror(errno));
            return -1;
        }
        if (add_mapping(mappings, mapped, file_total) != 0) {
            munmap(mapped, file_total);
            snprintf(err, err_size, "out of memory tracking loader mappings");
            return -1;
        }

        if (p->memsz > p->filesz) {
            uintptr_t bss_start = (uintptr_t)(p->vaddr + p->filesz);
            uintptr_t file_page_end = (uintptr_t)map_vaddr + file_total;
            uintptr_t mem_end = (uintptr_t)(p->vaddr + p->memsz);
            uintptr_t zero_end = mem_end < file_page_end ? mem_end : file_page_end;
            if (zero_end > bss_start) memset((void *)bss_start, 0, zero_end - bss_start);
        }
        if (mprotect(mapped, file_total, final_prot) < 0) {
            snprintf(err, err_size, "mprotect PT_LOAD failed: %s", strerror(errno));
            return -1;
        }
    }

    if (mem_total > file_total) {
        uintptr_t anon_addr = (uintptr_t)map_vaddr + file_total;
        size_t anon_len = mem_total - file_total;
        void *mapped = mmap((void *)anon_addr, anon_len, final_prot,
                            MAP_PRIVATE | MAP_ANONYMOUS | fixed_flag, -1, 0);
        if (mapped == MAP_FAILED) {
            snprintf(err, err_size, "mmap anonymous BSS tail at 0x%llx failed: %s",
                     (unsigned long long)anon_addr, strerror(errno));
            return -1;
        }
        if (add_mapping(mappings, mapped, anon_len) != 0) {
            munmap(mapped, anon_len);
            snprintf(err, err_size, "out of memory tracking BSS mapping");
            return -1;
        }
    }

    return 0;
}

int loader_map_image(const ElfImage *image, int force_fixed, LoaderMappingList *mappings, char *err, size_t err_size) {
    memset(mappings, 0, sizeof(*mappings));
    if (image->type != ET_EXEC) {
        snprintf(err, err_size, "static loader expects ET_EXEC; got %s", elf_type_name(image->type));
        return -1;
    }
    if (image_has_interp(image)) {
        snprintf(err, err_size, "PT_INTERP present: dynamically linked executables are intentionally unsupported");
        return -1;
    }
    for (size_t i = 0; i < phnum(image); ++i) {
        GenericPhdr p;
        get_phdr(image, i, &p);
        if (p.type != PT_LOAD || p.memsz == 0) continue;
        if (map_one_segment(image, &p, force_fixed, mappings, err, err_size) != 0) {
            loader_unmap_all(mappings);
            return -1;
        }
    }
    return 0;
}

int loader_can_execute(const ElfImage *image, char *reason, size_t reason_size) {
    if (image->type != ET_EXEC) {
        snprintf(reason, reason_size, "only ET_EXEC images can be executed");
        return 0;
    }
    if (image_has_interp(image)) {
        snprintf(reason, reason_size, "dynamic PT_INTERP images are not supported");
        return 0;
    }
#if defined(__x86_64__)
    if (image->elf_class != ELFCLASS64 || image->machine != EM_X86_64) {
        snprintf(reason, reason_size, "this build can execute only ELF64 x86-64; ELF32 mapping is supported but execution requires a 32-bit build");
        return 0;
    }
#elif defined(__i386__)
    if (image->elf_class != ELFCLASS32 || image->machine != EM_386) {
        snprintf(reason, reason_size, "this build can execute only ELF32 i386");
        return 0;
    }
#else
    snprintf(reason, reason_size, "entry-point execution is implemented only for x86/x86-64 builds");
    return 0;
#endif
    snprintf(reason, reason_size, "supported");
    return 1;
}
