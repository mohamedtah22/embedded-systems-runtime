; Mini Linux Runtime - 32-bit x86 raw Linux syscall layer
; NASM syntax, CDECL-callable from 32-bit C.
;
; Linux i386 int 0x80 ABI:
;   EAX = syscall number
;   EBX, ECX, EDX, ESI, EDI, EBP = arguments 1..6
;   EAX = return value. Kernel errors are returned as negative errno values.
;
; CDECL places function arguments on the stack. EBX is callee-saved, so each
; wrapper preserves it before using it as the first syscall argument register.

BITS 32
SECTION .text

global raw_open
global raw_read
global raw_write
global raw_close
global raw_exit

raw_open:                       ; int raw_open(const char *path, int flags, int mode)
    push ebx
    mov eax, 5                  ; __NR_open
    mov ebx, [esp + 8]          ; path (extra +4 because EBX was pushed)
    mov ecx, [esp + 12]         ; flags
    mov edx, [esp + 16]         ; mode
    int 0x80
    pop ebx
    ret

raw_read:                       ; int raw_read(int fd, void *buf, unsigned count)
    push ebx
    mov eax, 3                  ; __NR_read
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    int 0x80
    pop ebx
    ret

raw_write:                      ; int raw_write(int fd, const void *buf, unsigned count)
    push ebx
    mov eax, 4                  ; __NR_write
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    int 0x80
    pop ebx
    ret

raw_close:                      ; int raw_close(int fd)
    push ebx
    mov eax, 6                  ; __NR_close
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret

raw_exit:                       ; noreturn void raw_exit(int status)
    mov eax, 1                  ; __NR_exit
    mov ebx, [esp + 4]
    int 0x80
    ud2
