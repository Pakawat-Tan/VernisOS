; interrupts.asm — ISR/IRQ stubs for VernisOS x86-64 kernel
; Called by IDT; dispatches to C interrupt_dispatch(InterruptFrame*)
; System V AMD64 ABI: first argument in RDI

[BITS 64]
section .text

; _start — kernel entry point, sets stack before calling kernel_main
section .text.entry
global _start
extern kernel_main
_start:
    mov rsp, 0xF00000      ; 15 MB — above BSS end (~9.3 MB)
    xor rbp, rbp            ; clear frame pointer
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang

section .text
; Macro: exception WITHOUT error code (CPU does not push one)
; Push dummy 0 as error_code placeholder, then interrupt number
%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0        ; dummy error_code
    push qword %1       ; int_no
    jmp isr_common_stub
%endmacro

; Macro: exception WITH error code (CPU already pushed it)
; Push interrupt number on top of the error code
%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1       ; int_no  (error_code already on stack from CPU)
    jmp isr_common_stub
%endmacro

; Macro: hardware IRQ (no error code; push 0 + IRQ vector number)
%macro IRQ 2
global irq%1
irq%1:
    push qword 0        ; dummy error_code
    push qword %2       ; int_no (remapped vector: 0x20 + irq_line)
    jmp isr_common_stub
%endmacro

; ---- CPU Exception stubs (vectors 0–31) ----
ISR_NOERR 0    ; #DE Divide Error
ISR_NOERR 1    ; #DB Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; #BP Breakpoint
ISR_NOERR 4    ; #OF Overflow
ISR_NOERR 5    ; #BR Bound Range
ISR_NOERR 6    ; #UD Invalid Opcode
ISR_NOERR 7    ; #NM Device Not Available
ISR_ERR   8    ; #DF Double Fault        (error code = 0 always, but CPU pushes it)
ISR_NOERR 9    ; Coprocessor Overrun
ISR_ERR   10   ; #TS Invalid TSS
ISR_ERR   11   ; #NP Segment Not Present
ISR_ERR   12   ; #SS Stack Fault
ISR_ERR   13   ; #GP General Protection
ISR_ERR   14   ; #PF Page Fault
ISR_NOERR 15   ; Reserved
ISR_NOERR 16   ; #MF x87 FPU Error
ISR_ERR   17   ; #AC Alignment Check
ISR_NOERR 18   ; #MC Machine Check
ISR_NOERR 19   ; #XM SIMD Exception
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; ---- Hardware IRQ stubs (IRQ0-15 → vectors 0x20-0x2F) ----
IRQ  0, 0x20   ; IRQ0  timer
IRQ  1, 0x21   ; IRQ1  PS/2 keyboard
IRQ  2, 0x22
IRQ  3, 0x23
IRQ  4, 0x24
IRQ  5, 0x25
IRQ  6, 0x26
IRQ  7, 0x27
IRQ  8, 0x28
IRQ  9, 0x29
IRQ 10, 0x2A
IRQ 11, 0x2B
IRQ 12, 0x2C
IRQ 13, 0x2D
IRQ 14, 0x2E
IRQ 15, 0x2F

; ---- int 0x80 syscall ----
global isr_syscall
isr_syscall:
    push qword 0        ; dummy error_code
    push qword 0x80     ; int_no
    jmp isr_common_stub

; ---- Common stub ----
; Stack layout on entry:
;   [RSP+0]  int_no         (pushed by ISR_* macro)
;   [RSP+8]  error_code     (pushed by CPU or dummy 0)
;   [RSP+16] RIP            (pushed by CPU)
;   [RSP+24] CS             (pushed by CPU)
;   [RSP+32] RFLAGS         (pushed by CPU)
;   [RSP+40] RSP_old        (pushed by CPU if privilege change)
;   [RSP+48] SS             (pushed by CPU if privilege change)

isr_common_stub:
    ; Save all general-purpose registers (in order matching InterruptFrame)
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; RDI = pointer to InterruptFrame (first arg per System V AMD64 ABI)
    mov rdi, rsp

    ; Align stack to 16 bytes before call.
    ; 15 regs * 8 = 120 bytes pushed after CPU's frame (which was 16-aligned).
    ; 120 % 16 = 8, so RSP is now 8-byte aligned. Need to sub 8 to make it
    ; 16-byte aligned before CALL pushes the return address.
    sub rsp, 8

    extern interrupt_dispatch
    call interrupt_dispatch

    add rsp, 8

    ; Phase 18: context switch — if RAX != 0, switch to new task's stack
    test rax, rax
    jz .no_ctx_switch
    mov rsp, rax
.no_ctx_switch:

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Remove int_no and error_code we pushed in the ISR macros
    add rsp, 16

    iretq

; Mark stack as non-executable (must be at END of file after all code)
section .note.GNU-stack noalloc noexec nowrite progbits
