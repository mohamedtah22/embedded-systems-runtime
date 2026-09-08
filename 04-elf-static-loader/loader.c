#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <elf.h>
#include <string.h>

extern startup();

void print_phdr_addr(Elf32_Phdr *phdr, int phdr_num) {
    printf("Program header number %d at address 0x%x\n", phdr_num, phdr->p_paddr);
}

void print_phdr_information(Elf32_Phdr *phdr) {
    const char *type_str = "UNKNOWN";

    switch (phdr->p_type) {
        case PT_NULL: type_str = "NULL"; break;
        case PT_LOAD: type_str = "LOAD"; break;
        case PT_DYNAMIC: type_str = "DYNAMIC"; break;
        case PT_INTERP: type_str = "INTERP"; break;
        case PT_NOTE: type_str = "NOTE"; break;
        case PT_SHLIB: type_str = "SHLIB"; break;
        case PT_PHDR: type_str = "PHDR"; break;
        case PT_LOSUNW: type_str = "LOSUNW"; break;
        case PT_SUNWSTACK: type_str = "SUNWSTACK"; break;
        case PT_HISUNW: type_str = "HISUNW"; break;
        case PT_LOPROC: type_str = "LOPROC"; break;
        case PT_HIPROC: type_str = "HIPROC"; break;
    }
    printf("%s         0x%06x 0x%08x 0x%08x 0x%05x 0x%06x %c%c%c 0x%0x\n",
           type_str,
           phdr->p_offset,
           phdr->p_vaddr,
           phdr->p_paddr,
           phdr->p_filesz,
           phdr->p_memsz,
           phdr->p_flags & PF_R ? 'R' : ' ',
           phdr->p_flags & PF_W ? 'W' : ' ',
           phdr->p_flags & PF_X ? 'E' : ' ',
           phdr->p_align);

    char* prot_name;
    char* flag_name;
    int prot = 0;
    if (phdr->p_flags & PF_X) {prot |= PROT_EXEC;}
    if (phdr->p_flags & PF_R) {prot |= PROT_READ;}
    if (phdr->p_flags & PF_W) {prot |= PROT_WRITE;}

    int flag = MAP_PRIVATE;
    if (phdr->p_type == PT_LOAD && phdr->p_offset == 0) {flag |= MAP_SHARED;}

    switch (prot)
    {
        case 0x1: prot_name = "PROT_READ"; break;
        case 0x2: prot_name = "PROT_WRITE"; break;
        case 0x3: prot_name = "PROT_READ and PROT_WRITE"; break;
        case 0x4: prot_name = "PROT_EXEC"; break;
        case 0x5: prot_name = "PROT_READ and PROT_EXEC"; break;
        case 0x6: prot_name = "PROT_WRITE and PROT_EXEC"; break;
        case 0x7: prot_name = "PROT_READ and PROT_WRITE and PROT_EXEC"; break;
    }
    switch (flag){
        case 0x2: flag_name = "MAP_PRIVATE"; break;
        case 0x3: flag_name = "MAP_SHARED"; break;
    }
    printf("Protection flag: %s, Mapping flag %s\n\n", prot_name, flag_name);
}

void load_phdr(Elf32_Phdr *phdr, int fd){
    print_phdr_information(phdr);
    if(phdr->p_type != PT_LOAD){
        return;
    }
    int prot = 0;
    if (phdr->p_flags & PF_X) prot |= PROT_EXEC;
    if (phdr->p_flags & PF_R) prot |= PROT_READ;
    if (phdr->p_flags & PF_W) prot |= PROT_WRITE;

    int flags = MAP_PRIVATE;
    if (phdr->p_type == PT_LOAD && phdr->p_offset == 0) {flags |= MAP_SHARED;}

    int vaddr = phdr->p_vaddr & 0xfffff000;
    int offset = phdr->p_offset & 0xfffff000;
    int padding = phdr->p_vaddr & 0xfff;

    void* map = mmap((void*)vaddr, phdr->p_memsz+padding, prot, flags, fd, offset);
    if (map == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }
}

int foreach_phdr(void *map_start, void (*func)(Elf32_Phdr *,int), int arg){
    Elf32_Ehdr* elf_header = (Elf32_Ehdr*)map_start;
    Elf32_Phdr* elf_programHeader = (Elf32_Phdr *)((char *)map_start + elf_header->e_phoff);
    printf("Type         Offset   VirtAddr   PhysAddr   FileSiz MemSiz   Flg Align\n");
    for (int i = 0; i < elf_header->e_phnum; i++)
    {
        func(elf_programHeader + i, arg);
    }
    return 0;
}

int main(int argc, char** argv){
    struct stat sb;
    if(argc == 1){
        return 0;
    }
    int fd = open(argv[1],O_RDONLY);
    if(fd == -1){
        perror("Error: ");
        return 0;
    }
    if (fstat(fd, &sb) == -1){
        perror("Error: ");
        return 0;
    }
    void* map_start = mmap(NULL,sb.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    if(map_start == MAP_FAILED){
        perror("Error: ");
        return 0;
    }
    foreach_phdr(map_start,load_phdr,fd);
    Elf32_Ehdr* elf_head = (Elf32_Ehdr*)map_start;
    startup(argc-1, argv+1, (void *)(elf_head->e_entry));

    close(fd);
    return 0;
}
