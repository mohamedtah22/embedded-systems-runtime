#ifndef MLRT_LOADER_H
#define MLRT_LOADER_H

#include "elf_parser.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *addr;
    size_t length;
} LoaderMapping;

typedef struct {
    LoaderMapping *items;
    size_t count;
    size_t cap;
} LoaderMappingList;

int loader_map_image(const ElfImage *image, int force_fixed, LoaderMappingList *mappings, char *err, size_t err_size);
void loader_unmap_all(LoaderMappingList *mappings);
int loader_can_execute(const ElfImage *image, char *reason, size_t reason_size);

#endif
