; VernisOS CRT0 — x86_64 user program entry point
; Calls main(), then calls _exit(return_value)
;
; Linked at 0x10000000 (USER_CODE_VADDR)

section .text
    global _start
    extern main

_start:
    ; Clear frame pointer for stack traces
    xor rbp, rbp

    ; Call main()
    call main

    ; Exit with return value from main (in RAX → RBX for syscall arg)
    mov rbx, rax        ; arg1 = exit code
    mov rax, 60          ; SYS_EXIT
    int 0x80

    ; Should never reach here
.hang:
    hlt
    jmp .hang
