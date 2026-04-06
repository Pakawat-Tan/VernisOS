; VernisOS CRT0 — i686 user program entry point
; Calls main(), then calls _exit(return_value)
;
; Linked at 0x10000000 (USER_CODE_VADDR)

section .text
    global _start
    extern main

_start:
    ; Clear frame pointer for stack traces
    xor ebp, ebp

    ; Call main()
    call main

    ; Exit with return value from main (in EAX)
    mov ebx, eax        ; arg1 = exit code
    mov eax, 60          ; SYS_EXIT
    int 0x80

    ; Should never reach here
.hang:
    hlt
    jmp .hang
