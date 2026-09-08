; Standalone 32-bit Linux demo with no libc.
; Build: nasm -f elf32 asm/demo32.asm -o build/demo32.o
;        ld -m elf_i386 -o bin/raw-syscall-demo build/demo32.o

BITS 32
SECTION .data
message db "hello from raw int 0x80 syscalls", 10
message_len equ $ - message

SECTION .text
global _start

_start:
    mov eax, 4          ; __NR_write
    mov ebx, 1          ; stdout
    mov ecx, message
    mov edx, message_len
    int 0x80

    test eax, eax       ; errors are negative, not necessarily -1
    js .error

    xor ebx, ebx        ; status = 0
    mov eax, 1          ; __NR_exit
    int 0x80

.error:
    mov ebx, 1
    mov eax, 1
    int 0x80
