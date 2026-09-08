# Raw x86 Linux System Calls

This module preserves the original NASM coursework and turns it into a documented 32-bit Linux syscall layer.

- `syscalls32.asm` exposes CDECL-callable `raw_open`, `raw_read`, `raw_write`, `raw_close`, and `raw_exit` wrappers.
- `demo32.asm` is a standalone no-libc program using `int 0x80` directly.
- `legacy_encoder.asm` preserves the original assembly encoder exercise.

The i386 ABI uses `EAX` for the syscall number and `EBX/ECX/EDX/ESI/EDI/EBP` for arguments. Raw kernel errors are negative errno values, so callers should test for `< 0` rather than only `== -1`.
