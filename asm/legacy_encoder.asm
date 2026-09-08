section .data
    newline db 10       ; newline character
    Infile dd 0         ; input file descriptor (default stdin = 0)
    Outfile dd 1        ; output file descriptor (default stdout = 1)

section .bss
    input resb 1        ; buffer to store input character

section .text
    global main         ; entry point
    extern strlen       ; external function declaration

; Function to prepare input file
prepInput:
    pushad
    mov eax, 5          ; sys_open syscall number
    mov ebx, ecx        ; pointer to "-i{file}"
    inc ebx             ; skip '-'
    inc ebx             ; skip 'i'
    xor ecx, ecx        ; mode = O_RDONLY (0)
    int 0x80            ; perform syscall
    mov dword [Infile], eax  ; store file descriptor
    popad
    jmp check          ; jump to check for next argument

; Function to prepare output file
prepOutput:
    pushad
    mov eax, 5          ; sys_open syscall number
    mov ebx, ecx        ; pointer to "-o{file}"
    inc ebx             ; skip '-'
    inc ebx             ; skip 'o'
    mov ecx, 1          ; mode = O_WRONLY
    or ecx, 64          ; add O_CREAT flag
    mov edx, 777o       ; permission (777 in octal)
    int 0x80            ; perform syscall
    mov dword [Outfile], eax ; store file descriptor
    popad
    jmp check          ; jump to check for next argument

; Entry point of the program
main:
    mov edi, dword [esp+8] ; argv
    mov esi, dword [esp+4] ; argc
    xor edx, edx           ; counter = 0

; Loop through command-line arguments
loop:
    mov ecx, dword [edi+edx*4] ; load argument pointer

    ; Print argv[i], get length then print
    prnt:
    pushad
    mov ebx, 1               ; file descriptor = stdout
    mov ecx, dword [edi+edx*4] ; load string to print
    push ecx
    call strlen              ; get length of string
    pop ecx
    mov edx, eax             ; length
    mov eax, 4               ; sys_write syscall number
    int 0x80                 ; perform syscall

    ; Print newline
    mov eax, 4               ; sys_write syscall number
    mov ebx, 1               ; file descriptor = stdout
    mov ecx, newline         ; newline character
    mov edx, 1               ; length of newline
    int 0x80                 ; perform syscall
    popad

    ; Check for -i and -o flags
    inp:
    cmp word [ecx], "-i"     ; compare first two characters with "-i"
    je prepInput             ; jump to prepInput if match

    out:
    cmp word [ecx], "-o"     ; compare first two characters with "-o"
    je prepOutput            ; jump to prepOutput if match

; Check if all arguments have been processed
check:
    inc edx                  ; counter += 1
    cmp edx, esi             ; compare counter with argc
    jne loop                 ; loop if not end of arguments

; Function to read character from input
encoder:
    ReadChar:
    mov eax, 3               ; sys_read syscall number
    mov ebx, dword [Infile]  ; input file descriptor
    mov ecx, input           ; where to store char
    mov edx, 1               ; number of bytes to read
    int 0x80                 ; perform syscall

    ; End of file (EOF) check, exit if no more characters to read
    eof:
    sub eax, 0               ; check return value from sys_read
    jle exit                 ; jump to exit if end of file

    ; Check if character is between 'A' - 'z'
    CheckRange:
    movzx eax, byte [input]  ; load input character
    cmp eax, 'A'             ; compare with 'A'
    jl not_in_range          ; jump if less than 'A'
    cmp eax, 'z'             ; compare with 'z'
    jg not_in_range          ; jump if greater than 'z'

    inc byte [input]         ; increment input character

    ; Print modified character to output file
    print:
    mov eax, 4               ; sys_write syscall number
    mov ebx, dword [Outfile] ; output file descriptor
    mov ecx, input           ; character to print
    mov edx, 1               ; length of character
    int 0x80                 ; perform syscall
    jmp encoder              ; jump back to read next character

    ; Jump here if character is not between 'A' - 'z'
    not_in_range:
    jmp print                ; jump to print the character

; Function to exit the program
exit:
    mov eax, 1               ; exit syscall number
    mov ebx, 0               ; successful exit status
    int 0x80                 ; perform syscall
