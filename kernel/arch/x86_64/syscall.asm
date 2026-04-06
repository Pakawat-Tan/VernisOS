; syscall.asm — SYSCALL/SYSRET entry stub for VernisOS x86-64
; SYSCALL calling convention:
;   rax = syscall number
;   rdi = arg1, rsi = arg2, rdx = arg3, r10 = arg4
;   rcx = saved RIP (by CPU), r11 = saved RFLAGS (by CPU)
; Return value in rax.

[BITS 64]
section .text

global syscall_entry
extern c_syscall_handler

syscall_entry:
    ; SYSCALL does NOT switch stack or push a frame.
    ; When called from kernel-privilege code (as we do now), RSP is still
    ; pointing to the kernel stack — safe to use directly.
    ; When user-mode processes are added, swapgs + kernel-stack swap needed here.

    ; Save registers that SYSCALL does not preserve
    push rcx    ; saved user RIP (return address)
    push r11    ; saved user RFLAGS
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Arrange arguments for c_syscall_handler(num, a1, a2, a3, a4):
    ; System V AMD64: rdi=1st, rsi=2nd, rdx=3rd, rcx=4th, r8=5th
    ;   num  = rax  (syscall number)
    ;   arg1 = rdi  (already in rdi)
    ;   arg2 = rsi  (already in rsi)
    ;   arg3 = rdx  (already in rdx)
    ;   arg4 = r10  (move to rcx, 4th parameter)
    mov r8,  r10       ; r8  = arg4
    mov rcx, rdx       ; rcx = arg3
    mov rdx, rsi       ; rdx = arg2
    mov rsi, rdi       ; rsi = arg1
    mov rdi, rax       ; rdi = syscall number

    call c_syscall_handler
    ; Return value in rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop r11    ; restore RFLAGS
    pop rcx    ; restore RIP (return address for sysretq)

    o64 sysret

; Mark stack as non-executable (must be at END of file after all code)
section .note.GNU-stack noalloc noexec nowrite progbits
